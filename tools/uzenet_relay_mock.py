#!/usr/bin/env python3
"""Minimal uzenet relay (UZRL v1) for local testing of CUzeBox netplay / link.

Implements the subset CUzeBox uses: CREATE/JOIN rooms by code, PEER_INFO with
each side's public endpoint, RELAY_DATA forwarding, HEARTBEAT, LEAVE and
PUNCH_OK. It is NOT the production server; it exists so the internet link can
be exercised without reaching uzenet.us.

    python3 tools/uzenet_relay_mock.py --port 43810 [--no-direct] [--loss 0.1] [--delay-ms 80]

--no-direct   advertise peers without ALLOW_DIRECT, forcing relay-only traffic
--loss P      drop this fraction of relayed data packets (tests retransmission)
--delay-ms N  delay relayed data by N ms (plus --jitter-ms random)
"""
import argparse, heapq, random, socket, struct, time

MAGIC = 0x555A524C
HDR = struct.Struct('>IBBBBIIIHH')          # 24 bytes
T_CREATE, T_JOIN, T_HB, T_LEAVE, T_PUNCH_OK, T_LIST = 0x10, 0x11, 0x12, 0x13, 0x14, 0x15
T_RELAY = 0x20
T_CREATED, T_JOINED, T_PEER_INFO, T_PEER_LEFT, T_PUNCH_ST, T_FROM_PEER, T_HB_ACK, T_ROOM_LIST, T_ERROR = 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x3F
F_DIRECT, F_RELAY = 0x01, 0x02
E_ROOM_EXISTS, E_NOT_FOUND, E_FULL, E_APP = 3, 4, 5, 6


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=43810)
    ap.add_argument('--bind', default='0.0.0.0')
    ap.add_argument('--no-direct', action='store_true')
    ap.add_argument('--loss', type=float, default=0.0)
    ap.add_argument('--delay-ms', type=int, default=0)
    ap.add_argument('--jitter-ms', type=int, default=0)
    ap.add_argument('--quiet', action='store_true')
    a = ap.parse_args()

    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind((a.bind, a.port))
    s.settimeout(0.005)
    rooms = {}       # code -> {'app': id, 'members': [addr...], 'id': n}
    members = {}     # addr -> {'room': code, 'sid': n, 'seen': t, 'name': b''}
    next_id = [1]
    pending = []     # (due, seq, data, addr)
    stats = {'relayed': 0, 'dropped': 0}

    def log(*x):
        if not a.quiet:
            print('[relay]', *x, flush=True)

    def send(addr, typ, payload=b'', room_id=0, sid=0, flags=0):
        h = HDR.pack(MAGIC, 1, typ, flags, HDR.size, sid, room_id, 0, len(payload), 0)
        s.sendto(h + payload, addr)

    def err(addr, code, msg):
        send(addr, T_ERROR, struct.pack('>H48s', code, msg.encode()[:47]))

    def peer_info(to, other):
        m = members[other]
        ip = socket.inet_aton(other[0])
        flags = F_RELAY | (0 if a.no_direct else F_DIRECT)
        # peer_ipv4_be / peer_port_be are network order in the C struct: pack raw.
        payload = struct.pack('>I', m['sid']) + ip + struct.pack('>H', other[1]) + bytes([0, flags]) + \
            m['name'][:16].ljust(16, b'\0') + bytes([4, 4, 0, 0]) + ip.ljust(16, b'\0')
        r = rooms[members[to]['room']]
        send(to, T_PEER_INFO, payload, r['id'], members[to]['sid'])

    def drop_member(addr, notify=True):
        m = members.pop(addr, None)
        if not m:
            return
        r = rooms.get(m['room'])
        if r and addr in r['members']:
            r['members'].remove(addr)
            for o in r['members']:
                if notify:
                    send(o, T_PEER_LEFT, b'', r['id'], members[o]['sid'])
            if not r['members']:
                rooms.pop(m['room'], None)
        log('left', addr)

    while True:
        now = time.time()
        while pending and pending[0][0] <= now:
            _, _, data, to = heapq.heappop(pending)
            s.sendto(data, to)
        for addr, m in list(members.items()):
            if now - m['seen'] > 15:
                drop_member(addr)
        try:
            buf, addr = s.recvfrom(4096)
        except socket.timeout:
            continue
        except OSError:
            continue
        if len(buf) < HDR.size:
            continue
        magic, ver, typ, flags, hlen, sid, rid, seq, plen, _ = HDR.unpack_from(buf)
        if magic != MAGIC or ver != 1 or hlen != HDR.size or hlen + plen > len(buf):
            continue
        p = buf[hlen:hlen + plen]
        if addr in members:
            members[addr]['seen'] = now
        if typ in (T_CREATE, T_JOIN):
            code = p[0:8].rstrip(b'\0').decode(errors='replace')
            app = struct.unpack('>I', p[8:12])[0]
            name = p[12:28].rstrip(b'\0')
            if addr in members:
                if members[addr]['room'] == code:   # retransmitted request
                    r = rooms[code]
                    resp = p[0:8] + struct.pack('>IBBBBI', app, r['members'].index(addr), len(r['members']), 1, 1, members[addr]['sid'])
                    send(addr, T_CREATED if r['members'][0] == addr else T_JOINED, resp, r['id'], members[addr]['sid'])
                    continue
                drop_member(addr)
            if typ == T_CREATE:
                if code in rooms:
                    err(addr, E_ROOM_EXISTS, 'room exists'); continue
                rooms[code] = {'app': app, 'members': [], 'id': next_id[0]}
                next_id[0] += 1
            else:
                if code not in rooms:
                    err(addr, E_NOT_FOUND, 'room not found'); continue
                if rooms[code]['app'] != app:
                    err(addr, E_APP, 'app mismatch'); continue
                if len(rooms[code]['members']) >= 2:
                    err(addr, E_FULL, 'room full'); continue
            r = rooms[code]
            m = {'room': code, 'sid': next_id[0], 'seen': now, 'name': name}
            next_id[0] += 1
            members[addr] = m
            r['members'].append(addr)
            resp = p[0:8] + struct.pack('>IBBBBI', app, len(r['members']) - 1, len(r['members']), 1, 1, m['sid'])
            send(addr, T_CREATED if typ == T_CREATE else T_JOINED, resp, r['id'], m['sid'])
            log('created' if typ == T_CREATE else 'joined', code, addr)
            if len(r['members']) == 2:
                x, y = r['members']
                peer_info(x, y)
                peer_info(y, x)
        elif typ == T_RELAY and addr in members:
            r = rooms.get(members[addr]['room'])
            if not r:
                continue
            for o in r['members']:
                if o == addr:
                    continue
                if a.loss and random.random() < a.loss:
                    stats['dropped'] += 1
                    continue
                stats['relayed'] += 1
                h = HDR.pack(MAGIC, 1, T_FROM_PEER, 0, HDR.size, members[o]['sid'], r['id'], 0, len(p), 0)
                delay = (a.delay_ms + (random.randint(0, a.jitter_ms) if a.jitter_ms else 0)) / 1000.0
                if delay > 0:
                    heapq.heappush(pending, (now + delay, random.random(), h + p, o))
                else:
                    s.sendto(h + p, o)
        elif typ == T_HB and addr in members:
            send(addr, T_HB_ACK, p[0:4].ljust(4, b'\0') + struct.pack('>I', int(now * 1000) & 0xFFFFFFFF))
        elif typ == T_LEAVE:
            drop_member(addr)
        elif typ == T_PUNCH_OK and addr in members:
            log('punch ok from', addr)


if __name__ == '__main__':
    main()
