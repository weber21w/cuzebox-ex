/*
 * cu_link_net.inc - network transports and impairment for the raw serial link
 *
 * Included by cu_esp_net.c (same translation unit, everything static). Three
 * independent pieces:
 *
 *   1. cu_lk_*  : internet link through the uzenet relay (UDP). Both players
 *                 enter the same room code. The first to arrive creates the
 *                 room, the second joins it. A direct UDP hole punch is tried;
 *                 if it does not work the bytes stay on the relay. Serial
 *                 bytes ride on a small reliable, ordered stream (sequence
 *                 numbers, cumulative + selective acks, retransmission).
 *
 *   2. cu_lan_* : LAN discovery. Instances broadcast a small beacon. When
 *                 exactly one other searching instance runs the same ROM (or
 *                 the user picks one), both decide roles from their random
 *                 ids and the normal TCP Serial transport connects them.
 *
 *   3. cu_imp_* : receive-side impairment simulator: latency + jitter,
 *                 periodic stalls, and serial line noise (bit flips and
 *                 dropped bytes). Order is always preserved, like a real wire.
 *
 * The link layer deliberately does NOT try to lock-step the two emulators:
 * real Uzebox consoles have independent crystals, so link games must already
 * tolerate rate differences and latency. This layer only moves bytes.
 */

#include "uzenet_relay_protocol.h"

/* ------------------------------------------------------------------------- */
/* Shared helpers                                                             */
/* ------------------------------------------------------------------------- */

#define CU_LK_TIME_GE(a, b) ((sint32)((uint32)(a) - (uint32)(b)) >= 0)

static uint32 cu_lk_rand_state = 0u;

static uint32 cu_lk_rand(void)
{
	uint32 x = cu_lk_rand_state;
	if(x == 0u){
		x = cu_esp_now_ms() ^ ((uint32)cu_tcp_diag_pid() * 2654435761u) ^ 0xA5C3E1F7u;
		if(x == 0u)
			x = 0x12345678u;
	}
	x ^= x << 13; x ^= x >> 17; x ^= x << 5;
	cu_lk_rand_state = x;
	return x;
}

static void cu_lk_close(ESP_SOCKET *s)
{
	if(*s == ESP_INVALID_SOCKET || *s == (ESP_SOCKET)-1)
		return;
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	closesocket(*s);
#else
	close(*s);
#endif
	*s = ESP_INVALID_SOCKET;
}

static void cu_lk_nonblock(ESP_SOCKET s)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	u_long mode = 1u;
	ioctlsocket(s, FIONBIO, &mode);
#else
	int fl = fcntl(s, F_GETFL, 0);
	if(fl >= 0)
		fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif
}

/* Windows reports an ICMP "port unreachable" (e.g. after a hole-punch probe
 * to a closed port) as WSAECONNRESET on the NEXT recvfrom of a UDP socket.
 * Turn that off, and treat it as "try again" if it still shows up. */
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#endif

static void cu_lk_udp_setup(ESP_SOCKET s)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	BOOL off = FALSE;
	DWORD ret = 0;
	(void)WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof(off), NULL, 0, &ret, NULL, NULL);
#else
	(void)s;
#endif
	cu_lk_nonblock(s);
}

static boole cu_lk_udp_soft_error(void)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	int e = WSAGetLastError();
	return (e == WSAECONNRESET || e == WSAENETRESET || e == WSAEMSGSIZE) ? TRUE : FALSE;
#else
	return (errno == ECONNREFUSED || errno == EINTR) ? TRUE : FALSE;
#endif
}

static boole cu_lk_resolve_v4(char const *host, uint32 port, struct sockaddr_in *out)
{
	struct addrinfo hints, *res = NULL;
	char portbuf[16];
	if(host == NULL || host[0] == 0 || port == 0u || port > 65535u)
		return FALSE;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	snprintf(portbuf, sizeof(portbuf), "%u", (unsigned)port);
	if(getaddrinfo(host, portbuf, &hints, &res) != 0 || res == NULL)
		return FALSE;
	memcpy(out, res->ai_addr, sizeof(*out));
	freeaddrinfo(res);
	return TRUE;
}

static boole cu_lk_addr_eq(struct sockaddr_in const *a, struct sockaddr_in const *b)
{
	return (a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port) ? TRUE : FALSE;
}

static void cu_lk_put32(uint8 *p, uint32 v){ p[0] = (uint8)(v >> 24); p[1] = (uint8)(v >> 16); p[2] = (uint8)(v >> 8); p[3] = (uint8)v; }
static void cu_lk_put16(uint8 *p, uint32 v){ p[0] = (uint8)(v >> 8); p[1] = (uint8)v; }
static uint32 cu_lk_get32(uint8 const *p){ return ((uint32)p[0] << 24) | ((uint32)p[1] << 16) | ((uint32)p[2] << 8) | (uint32)p[3]; }
static uint32 cu_lk_get16(uint8 const *p){ return ((uint32)p[0] << 8) | (uint32)p[1]; }

/* ------------------------------------------------------------------------- */
/* 3. Impairment simulator (receive side)                                     */
/* ------------------------------------------------------------------------- */

#define CU_IMP_CAP 16384u

typedef struct {
	uint32 enabled;
	uint32 latency_ms;
	uint32 jitter_ms;
	uint32 stall_every_ms;   /* average interval between stalls, 0 = off */
	uint32 stall_ms;         /* stall length */
	uint32 noise_ppm;        /* bytes with one flipped bit, per million */
	uint32 drop_ppm;         /* bytes lost, per million */
} cu_imp_cfg_t;

static cu_imp_cfg_t cu_imp_cfg = { 0u, 0u, 0u, 0u, 0u, 0u, 0u };

static struct {
	uint32 due[CU_IMP_CAP];
	uint8 b[CU_IMP_CAP];
	uint32 head, count;
	uint32 last_due;
	uint32 stall_until;
	uint32 next_stall_at;
	uint32 stall_armed;
	/* statistics */
	uint32 delayed, corrupted, dropped, overflow, stalls;
} cu_imp;

static boole cu_imp_active(void)
{
	return (cu_imp_cfg.enabled || cu_imp.count != 0u) ? TRUE : FALSE;
}

static void cu_imp_reset(void)
{
	memset(&cu_imp, 0, sizeof(cu_imp));
}

static void cu_imp_push(uint8 const *buf, uint32 n, uint32 now)
{
	uint32 i;
	for(i = 0u; i < n; i++){
		uint32 due, tail;
		if(cu_imp.count >= CU_IMP_CAP){
			cu_imp.overflow++;
			continue;
		}
		due = now;
		if(cu_imp_cfg.enabled){
			due += cu_imp_cfg.latency_ms;
			if(cu_imp_cfg.jitter_ms != 0u)
				due += cu_lk_rand() % (cu_imp_cfg.jitter_ms + 1u);
		}
		/* A serial wire never reorders: a byte cannot overtake the previous one. */
		if(cu_imp.count != 0u && !CU_LK_TIME_GE(due, cu_imp.last_due))
			due = cu_imp.last_due;
		tail = (cu_imp.head + cu_imp.count) % CU_IMP_CAP;
		cu_imp.due[tail] = due;
		cu_imp.b[tail] = buf[i];
		cu_imp.count++;
		cu_imp.last_due = due;
		cu_imp.delayed++;
	}
}

/* Release due bytes (at most max). Returns number written to out. */
static uint32 cu_imp_pop(uint8 *out, uint32 max, uint32 now)
{
	uint32 n = 0u;
	if(cu_imp_cfg.enabled && cu_imp_cfg.stall_every_ms != 0u && cu_imp_cfg.stall_ms != 0u){
		if(!cu_imp.stall_armed){
			cu_imp.next_stall_at = now + cu_imp_cfg.stall_every_ms / 2u + cu_lk_rand() % (cu_imp_cfg.stall_every_ms + 1u);
			cu_imp.stall_armed = 1u;
		}else if(CU_LK_TIME_GE(now, cu_imp.next_stall_at)){
			cu_imp.stall_until = now + cu_imp_cfg.stall_ms;
			cu_imp.next_stall_at = cu_imp.stall_until + cu_imp_cfg.stall_every_ms / 2u + cu_lk_rand() % (cu_imp_cfg.stall_every_ms + 1u);
			cu_imp.stalls++;
		}
		if(!CU_LK_TIME_GE(now, cu_imp.stall_until))
			return 0u;
	}else{
		cu_imp.stall_armed = 0u;
	}
	while(n < max && cu_imp.count != 0u && CU_LK_TIME_GE(now, cu_imp.due[cu_imp.head])){
		uint8 b = cu_imp.b[cu_imp.head];
		cu_imp.head = (cu_imp.head + 1u) % CU_IMP_CAP;
		cu_imp.count--;
		if(cu_imp_cfg.enabled && cu_imp_cfg.drop_ppm != 0u && (cu_lk_rand() % 1000000u) < cu_imp_cfg.drop_ppm){
			cu_imp.dropped++;
			continue;
		}
		if(cu_imp_cfg.enabled && cu_imp_cfg.noise_ppm != 0u && (cu_lk_rand() % 1000000u) < cu_imp_cfg.noise_ppm){
			b ^= (uint8)(1u << (cu_lk_rand() & 7u));
			cu_imp.corrupted++;
		}
		out[n++] = b;
	}
	return n;
}

/* ------------------------------------------------------------------------- */
/* 1. Reliable stream over the uzenet relay / direct UDP                      */
/* ------------------------------------------------------------------------- */

#define CU_LK_MAGIC        0x555A4C4Bu   /* "UZLK" */
#define CU_LK_VERSION      1u
#define CU_LK_HDR_LEN      36u
#define CU_LK_MAX_DATA     480u
#define CU_LK_WIN          64u
#define CU_LK_RXFIFO       16384u
#define CU_LK_APP_ID       0x4C494E4Bu   /* "LINK": keeps link rooms apart from netplay rooms */

#define CU_LK_T_HELLO 1u
#define CU_LK_T_DATA  2u
#define CU_LK_T_ACK   3u
#define CU_LK_T_BYE   4u
#define CU_LK_T_PUNCH 5u

#define CU_LK_PH_IDLE      0u
#define CU_LK_PH_JOINING   1u
#define CU_LK_PH_CREATING  2u
#define CU_LK_PH_WAIT_PEER 3u
#define CU_LK_PH_PEER      4u   /* peer known, link handshake / connected */
#define CU_LK_PH_RETRY     5u

#define CU_LK_REQ_RETRY_MS     1000u
#define CU_LK_HEARTBEAT_MS     3000u
#define CU_LK_HELLO_MS         250u
#define CU_LK_KEEPALIVE_MS     1000u
#define CU_LK_PEER_TIMEOUT_MS  10000u
#define CU_LK_PUNCH_MS         250u
#define CU_LK_PUNCH_WINDOW_MS  6000u
#define CU_LK_DIRECT_STALE_MS  3000u
#define CU_LK_RETRY_WAIT_MS    3000u

typedef struct {
	uint32 seq;
	uint32 len;
	uint32 sent_ms;
	uint32 retx;
	uint32 sacked;
	uint8 data[CU_LK_MAX_DATA];
} cu_lk_slot_t;

typedef struct {
	uint32 valid;
	uint32 len;
	uint8 data[CU_LK_MAX_DATA];
} cu_lk_rslot_t;

static struct {
	ESP_SOCKET sock;
	uint32 phase;
	uint32 phase_since;
	char relay_host[64];
	uint32 relay_port;
	struct sockaddr_in relay_addr;
	char room[UZRL_ROOM_CODE_LEN + 1];
	char name[UZRL_NAME_LEN + 1];
	uint32 relay_session_id;
	uint32 relay_room_id;
	uint32 relay_tx_seq;
	uint32 owner;
	uint32 last_req_ms;
	uint32 last_hb_ms;
	uint32 retry_at;
	/* direct path */
	struct sockaddr_in peer_addr;
	uint32 direct_candidate;
	uint32 direct_ok;
	uint32 punch_ok_sent;
	uint32 punch_until;
	uint32 last_punch_ms;
	uint32 last_direct_rx;
	/* link session */
	uint32 my_session;
	uint32 peer_session;
	uint32 connected;
	uint32 last_hello_ms;
	uint32 last_rx_ms;
	uint32 last_tx_ms;
	uint32 ack_pending;
	uint32 peer_ts;          /* newest timestamp seen from the peer, echoed back */
	/* sender */
	uint32 tx_next;
	cu_lk_slot_t tx[CU_LK_WIN];
	uint32 tx_head, tx_count;
	uint32 srtt, rto;
	/* receiver */
	uint32 rx_next;
	cu_lk_rslot_t rx[CU_LK_WIN];
	uint8 fifo[CU_LK_RXFIFO];
	uint32 fifo_head, fifo_count;
	/* stats */
	uint32 pkts_tx, pkts_rx, retransmits, relay_tx, direct_tx, bad_pkts;
	char notice[64];
} cu_lk;

static void cu_lk_notice(char const *msg)
{
	size_t n = strlen(msg);
	if(n >= sizeof(cu_lk.notice))
		n = sizeof(cu_lk.notice) - 1u;
	memcpy(cu_lk.notice, msg, n);
	cu_lk.notice[n] = 0;
	print_message("Link: %s\n", msg);
}

static void cu_lk_set_phase(uint32 ph, uint32 now)
{
	cu_lk.phase = ph;
	cu_lk.phase_since = now;
}

static void cu_lk_stream_reset(void)
{
	cu_lk.connected = 0u;
	cu_lk.peer_session = 0u;
	cu_lk.ack_pending = 0u;
	cu_lk.tx_next = 1u;
	cu_lk.tx_head = 0u;
	cu_lk.tx_count = 0u;
	cu_lk.rx_next = 1u;
	cu_lk.fifo_head = 0u;
	cu_lk.fifo_count = 0u;
	cu_lk.srtt = 0u;
	cu_lk.rto = 300u;
	memset(cu_lk.rx, 0, sizeof(cu_lk.rx));
	do { cu_lk.my_session = cu_lk_rand(); } while(cu_lk.my_session == 0u);
}

static void cu_lk_send_raw(void const *buf, uint32 len, struct sockaddr_in const *to)
{
	if(cu_lk.sock == ESP_INVALID_SOCKET)
		return;
	(void)sendto(cu_lk.sock, (char const *)buf, (int)len, 0, (struct sockaddr const *)to, (socklen_t)sizeof(*to));
}

static void cu_lk_relay_send(uint32 type, void const *payload, uint32 plen)
{
	uint8 buf[sizeof(uzrl_header_t) + UZRL_MAX_RELAY_PAYLOAD];
	uzrl_header_t h;
	if(plen > UZRL_MAX_RELAY_PAYLOAD)
		return;
	memset(&h, 0, sizeof(h));
	h.magic_be = htonl(UZRL_MAGIC_U32);
	h.version = UZRL_VERSION;
	h.type = (uint8)type;
	h.flags = UZRL_FLAG_ALLOW_DIRECT | UZRL_FLAG_ALLOW_RELAY;
	h.header_len = (uint8)sizeof(h);
	h.session_id_be = htonl(cu_lk.relay_session_id);
	h.room_id_be = htonl(cu_lk.relay_room_id);
	h.seq_be = htonl(cu_lk.relay_tx_seq++);
	h.payload_len_be = htons((uint16)plen);
	memcpy(buf, &h, sizeof(h));
	if(plen != 0u)
		memcpy(buf + sizeof(h), payload, plen);
	cu_lk_send_raw(buf, (uint32)sizeof(h) + plen, &cu_lk.relay_addr);
}

static void cu_lk_relay_request(uint32 type, uint32 now)
{
	uzrl_room_req_v2_t req;
	memset(&req, 0, sizeof(req));
	memcpy(req.base.room_code, cu_lk.room, UZRL_ROOM_CODE_LEN);
	req.base.app_id_be = htonl(CU_LK_APP_ID);
	memcpy(req.base.name, cu_lk.name, UZRL_NAME_LEN);
	req.base.want_direct = 1u;
	req.base.want_relay = 1u;
	snprintf(req.game_title, sizeof(req.game_title), "[LINK] serial cable");
	if(type == UZRLT_CREATE_REQ)
		cu_lk_relay_send(type, &req, (uint32)sizeof(req));
	else
		cu_lk_relay_send(type, &req.base, (uint32)sizeof(req.base));
	cu_lk.last_req_ms = now;
}

static uint32 cu_lk_sack_bits(void)
{
	uint32 bits = 0u, i;
	for(i = 0u; i < 32u && i + 1u < CU_LK_WIN; i++){
		uint32 seq = cu_lk.rx_next + 1u + i;
		cu_lk_rslot_t *r = &cu_lk.rx[seq % CU_LK_WIN];
		if(r->valid && r->valid == seq)
			bits |= (1u << i);
	}
	return bits;
}

static boole cu_lk_direct_usable(uint32 now)
{
	return (cu_lk.direct_ok && !CU_LK_TIME_GE(now, cu_lk.last_direct_rx + CU_LK_DIRECT_STALE_MS)) ? TRUE : FALSE;
}

static void cu_lk_send_pkt(uint32 type, uint32 seq, uint8 const *data, uint32 len, uint32 now)
{
	uint8 pkt[CU_LK_HDR_LEN + CU_LK_MAX_DATA];
	if(len > CU_LK_MAX_DATA)
		len = CU_LK_MAX_DATA;
	cu_lk_put32(pkt + 0, CU_LK_MAGIC);
	pkt[4] = (uint8)CU_LK_VERSION;
	pkt[5] = (uint8)type;
	cu_lk_put16(pkt + 6, len);
	cu_lk_put32(pkt + 8, cu_lk.my_session);
	cu_lk_put32(pkt + 12, cu_lk.peer_session);
	cu_lk_put32(pkt + 16, seq);
	cu_lk_put32(pkt + 20, cu_lk.rx_next);
	cu_lk_put32(pkt + 24, cu_lk_sack_bits());
	cu_lk_put32(pkt + 28, now | 1u);      /* never 0: 0 means "no echo" */
	cu_lk_put32(pkt + 32, cu_lk.peer_ts);
	if(len != 0u)
		memcpy(pkt + CU_LK_HDR_LEN, data, len);
	cu_lk.pkts_tx++;
	cu_lk.last_tx_ms = now;
	cu_lk.ack_pending = 0u;
	if(type == CU_LK_T_PUNCH){
		cu_lk_send_raw(pkt, CU_LK_HDR_LEN + len, &cu_lk.peer_addr);
		return;
	}
	if(cu_lk_direct_usable(now)){
		cu_lk.direct_tx++;
		cu_lk_send_raw(pkt, CU_LK_HDR_LEN + len, &cu_lk.peer_addr);
	}else{
		cu_lk.relay_tx++;
		cu_lk_relay_send(UZRLT_RELAY_DATA, pkt, CU_LK_HDR_LEN + len);
	}
}

static void cu_lk_on_ack(uint32 ack, uint32 sack, uint32 now)
{
	/* Cumulative: everything below ack has been received in order. */
	while(cu_lk.tx_count != 0u){
		cu_lk_slot_t *s = &cu_lk.tx[cu_lk.tx_head];
		if((sint32)(s->seq - ack) >= 0)
			break;
		cu_lk.tx_head = (cu_lk.tx_head + 1u) % CU_LK_WIN;
		cu_lk.tx_count--;
	}
	/* Selective: mark later packets the peer already holds. */
	if(sack != 0u){
		uint32 i;
		for(i = 0u; i < cu_lk.tx_count; i++){
			cu_lk_slot_t *s = &cu_lk.tx[(cu_lk.tx_head + i) % CU_LK_WIN];
			uint32 off = s->seq - ack;
			if(off >= 1u && off <= 32u && (sack & (1u << (off - 1u))))
				s->sacked = 1u;
		}
	}
}

static void cu_lk_fifo_put(uint8 const *d, uint32 n)
{
	uint32 i;
	for(i = 0u; i < n; i++){
		cu_lk.fifo[(cu_lk.fifo_head + cu_lk.fifo_count) % CU_LK_RXFIFO] = d[i];
		cu_lk.fifo_count++;
	}
}

static void cu_lk_on_data(uint32 seq, uint8 const *d, uint32 len)
{
	cu_lk.ack_pending = 1u;
	if((sint32)(seq - cu_lk.rx_next) < 0)
		return; /* duplicate */
	if((seq - cu_lk.rx_next) >= CU_LK_WIN)
		return; /* outside window; sender will retransmit */
	if(seq != cu_lk.rx_next){
		cu_lk_rslot_t *r = &cu_lk.rx[seq % CU_LK_WIN];
		r->valid = seq;
		r->len = len;
		memcpy(r->data, d, len);
		return;
	}
	if(CU_LK_RXFIFO - cu_lk.fifo_count < len)
		return; /* no room: drop, retransmission brings it back */
	cu_lk_fifo_put(d, len);
	cu_lk.rx_next++;
	for(;;){
		cu_lk_rslot_t *r = &cu_lk.rx[cu_lk.rx_next % CU_LK_WIN];
		if(r->valid != cu_lk.rx_next || CU_LK_RXFIFO - cu_lk.fifo_count < r->len)
			break;
		cu_lk_fifo_put(r->data, r->len);
		r->valid = 0u;
		cu_lk.rx_next++;
	}
}

static void cu_lk_on_link_pkt(uint8 const *p, uint32 n, boole direct, uint32 now)
{
	uint32 type, len, sess, peer_sess, seq, ack, sack;
	if(n < CU_LK_HDR_LEN || cu_lk_get32(p) != CU_LK_MAGIC || p[4] != CU_LK_VERSION){
		cu_lk.bad_pkts++;
		return;
	}
	type = p[5];
	len = cu_lk_get16(p + 6);
	if(len > CU_LK_MAX_DATA || CU_LK_HDR_LEN + len > n){
		cu_lk.bad_pkts++;
		return;
	}
	sess = cu_lk_get32(p + 8);
	peer_sess = cu_lk_get32(p + 12);
	seq = cu_lk_get32(p + 16);
	ack = cu_lk_get32(p + 20);
	sack = cu_lk_get32(p + 24);
	if(sess == 0u)
		return;
	cu_lk.pkts_rx++;
	if(direct){
		cu_lk.last_direct_rx = now;
		if(!cu_lk.direct_ok){
			cu_lk.direct_ok = 1u;
			cu_lk_notice("direct UDP path established");
			if(!cu_lk.punch_ok_sent){
				cu_lk_relay_send(UZRLT_PUNCH_OK, NULL, 0u);
				cu_lk.punch_ok_sent = 1u;
			}
		}
	}
	if(type == CU_LK_T_PUNCH)
		return;
	if(cu_lk.phase != CU_LK_PH_PEER)
		return;
	if(cu_lk.peer_session != 0u && sess != cu_lk.peer_session){
		/* Peer restarted its link: start a fresh stream. */
		uint32 keep = cu_lk.my_session;
		cu_lk_stream_reset();
		cu_lk.my_session = keep;
		cu_lk_notice("peer restarted, resynchronizing stream");
	}
	if(type == CU_LK_T_BYE){
		cu_lk_stream_reset();
		cu_lk_notice("peer closed the link");
		return;
	}
	cu_lk.peer_session = sess;
	cu_lk.last_rx_ms = now;
	{
		/* RTT from timestamp echo (immune to reordering and retransmission). */
		uint32 ts = cu_lk_get32(p + 28), echo = cu_lk_get32(p + 32);
		if(ts != 0u)
			cu_lk.peer_ts = ts;
		if(echo != 0u && peer_sess == cu_lk.my_session){
			uint32 sample = now - echo;
			if(sample < 60000u){
				cu_lk.srtt = (cu_lk.srtt == 0u) ? sample : (cu_lk.srtt * 7u + sample) / 8u;
				cu_lk.rto = cu_lk.srtt * 2u + 60u;
				if(cu_lk.rto < 100u) cu_lk.rto = 100u;
				if(cu_lk.rto > 3000u) cu_lk.rto = 3000u;
			}
		}
	}
	if(!cu_lk.connected){
		if(peer_sess != cu_lk.my_session){
			/* Peer has not heard from us yet: answer its HELLO right away. */
			cu_lk_send_pkt(CU_LK_T_HELLO, 0u, NULL, 0u, now);
			cu_lk.last_hello_ms = now;
			return;
		}
		cu_lk.connected = 1u;
		cu_lk_send_pkt(CU_LK_T_HELLO, 0u, NULL, 0u, now);
		cu_lk_notice(cu_lk_direct_usable(now) ? "linked (direct)" : "linked (via relay)");
	}else if(peer_sess != cu_lk.my_session){
		return;
	}
	cu_lk_on_ack(ack, sack, now);
	if(type == CU_LK_T_DATA)
		cu_lk_on_data(seq, p + CU_LK_HDR_LEN, len);
}

static void cu_lk_on_relay_pkt(uint8 const *buf, uint32 got, uint32 now)
{
	uzrl_header_t h;
	uint32 plen;
	uint8 const *payload;
	if(got < (uint32)sizeof(h))
		return;
	memcpy(&h, buf, sizeof(h));
	if(ntohl(h.magic_be) != UZRL_MAGIC_U32 || h.version != UZRL_VERSION || h.header_len != sizeof(h))
		return;
	plen = ntohs(h.payload_len_be);
	if((uint32)h.header_len + plen > got)
		return;
	payload = buf + h.header_len;
	switch(h.type){
	case UZRLT_ROOM_CREATED:
	case UZRLT_ROOM_JOINED:
		if(plen >= sizeof(uzrl_room_resp_t)){
			uzrl_room_resp_t r;
			memcpy(&r, payload, sizeof(r));
			cu_lk.relay_session_id = ntohl(r.assigned_session_id_be);
			cu_lk.relay_room_id = ntohl(h.room_id_be);
			cu_lk.owner = (h.type == UZRLT_ROOM_CREATED) ? 1u : 0u;
			if(cu_lk.phase == CU_LK_PH_JOINING || cu_lk.phase == CU_LK_PH_CREATING){
				cu_lk_set_phase(CU_LK_PH_WAIT_PEER, now);
				cu_lk_notice(cu_lk.owner ? "room created, waiting for the other player" : "room joined, waiting for peer info");
			}
			cu_lk.last_hb_ms = now;
		}
		break;
	case UZRLT_PEER_INFO:
		if(plen >= 28u){
			uzrl_peer_info_t info;
			memset(&info, 0, sizeof(info));
			memcpy(&info, payload, (plen < (uint32)sizeof(info)) ? plen : (uint32)sizeof(info));
			memset(&cu_lk.peer_addr, 0, sizeof(cu_lk.peer_addr));
			cu_lk.peer_addr.sin_family = AF_INET;
			cu_lk.peer_addr.sin_addr.s_addr = info.peer_ipv4_be;
			cu_lk.peer_addr.sin_port = info.peer_port_be;
			cu_lk.direct_candidate = ((info.flags & UZRL_FLAG_ALLOW_DIRECT) && info.peer_ipv4_be != 0u && info.peer_port_be != 0u) ? 1u : 0u;
			cu_lk.direct_ok = 0u;
			cu_lk.punch_ok_sent = 0u;
			cu_lk.punch_until = now + CU_LK_PUNCH_WINDOW_MS;
			cu_lk.last_punch_ms = 0u;
			if(cu_lk.phase != CU_LK_PH_PEER){
				cu_lk_stream_reset();
				cu_lk_set_phase(CU_LK_PH_PEER, now);
				cu_lk.last_hello_ms = 0u;
				cu_lk.last_rx_ms = now;
				cu_lk_notice(cu_lk.direct_candidate ? "peer found, trying direct UDP" : "peer found, using relay");
			}
		}
		break;
	case UZRLT_RELAY_FROM_PEER:
		cu_lk_on_link_pkt(payload, plen, FALSE, now);
		break;
	case UZRLT_PEER_LEFT:
		if(cu_lk.phase == CU_LK_PH_PEER){
			cu_lk_stream_reset();
			cu_lk.direct_ok = 0u;
			cu_lk.direct_candidate = 0u;
			cu_lk_set_phase(CU_LK_PH_WAIT_PEER, now);
			cu_lk_notice("other player left the room, waiting");
		}
		break;
	case UZRLT_ERROR:
		if(plen >= sizeof(uzrl_error_t)){
			uzrl_error_t e;
			uint32 code;
			memcpy(&e, payload, sizeof(e));
			e.message[sizeof(e.message) - 1u] = 0;
			code = ntohs(e.code_be);
			if(cu_lk.phase == CU_LK_PH_JOINING && (code == UZRL_ERR_ROOM_NOT_FOUND || code == UZRL_ERR_NO_ROOM)){
				/* Nobody created this room yet: we are first, create it. */
				cu_lk_set_phase(CU_LK_PH_CREATING, now);
				cu_lk_relay_request(UZRLT_CREATE_REQ, now);
			}else if(cu_lk.phase == CU_LK_PH_CREATING && code == UZRL_ERR_ROOM_EXISTS){
				/* The other player created it a moment ago: join it. */
				cu_lk_set_phase(CU_LK_PH_JOINING, now);
				cu_lk_relay_request(UZRLT_JOIN_REQ, now);
			}else if(cu_lk.phase == CU_LK_PH_JOINING || cu_lk.phase == CU_LK_PH_CREATING){
				char msg[64];
				snprintf(msg, sizeof(msg), "relay: %s", e.message[0] ? e.message : "error");
				cu_lk_notice(msg);
				cu_lk_set_phase(CU_LK_PH_RETRY, now);
				cu_lk.retry_at = now + CU_LK_RETRY_WAIT_MS;
			}
		}
		break;
	default:
		break;
	}
}

static void cu_lk_stop(void)
{
	if(cu_lk.sock != ESP_INVALID_SOCKET){
		if(cu_lk.phase == CU_LK_PH_PEER && cu_lk.peer_session != 0u)
			cu_lk_send_pkt(CU_LK_T_BYE, 0u, NULL, 0u, cu_esp_now_ms());
		if(cu_lk.relay_session_id != 0u)
			cu_lk_relay_send(UZRLT_LEAVE, NULL, 0u);
	}
	cu_lk_close(&cu_lk.sock);
	cu_lk.phase = CU_LK_PH_IDLE;
	cu_lk.connected = 0u;
}

/* Normalizes a room code: upper case A-Z/0-9, at most 8 characters. */
static void cu_lk_normalize_room(char *dst, uint32 cap, char const *src)
{
	uint32 o = 0u;
	if(cap == 0u)
		return;
	while(src != NULL && *src != 0 && o + 1u < cap && o < UZRL_ROOM_CODE_LEN){
		char c = *src++;
		if(c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
		if((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
			dst[o++] = c;
	}
	dst[o] = 0;
}

static boole cu_lk_start(char const *relay_host, uint32 relay_port, char const *room, uint32 now)
{
	struct sockaddr_in local;
	ESP_SOCKET s;
	cu_lk_stop();
	{
		ESP_SOCKET keep = ESP_INVALID_SOCKET;
		memset(&cu_lk, 0, sizeof(cu_lk));
		cu_lk.sock = keep;
	}
	snprintf(cu_lk.relay_host, sizeof(cu_lk.relay_host), "%s", (relay_host && relay_host[0]) ? relay_host : "uzenet.us");
	cu_lk.relay_port = relay_port ? relay_port : 43810u;
	cu_lk_normalize_room(cu_lk.room, sizeof(cu_lk.room), room);
	snprintf(cu_lk.name, sizeof(cu_lk.name), "CUzeBox-%04X", (unsigned)(cu_lk_rand() & 0xFFFFu));
	cu_lk_stream_reset();
	if(cu_lk.room[0] == 0){
		cu_lk_notice("enter a room code first");
		return FALSE;
	}
	if(!cu_lk_resolve_v4(cu_lk.relay_host, cu_lk.relay_port, &cu_lk.relay_addr)){
		cu_lk_notice("cannot resolve relay host");
		cu_lk_set_phase(CU_LK_PH_RETRY, now);
		cu_lk.retry_at = now + CU_LK_RETRY_WAIT_MS;
		return FALSE;
	}
	s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if(s == ESP_INVALID_SOCKET || s == (ESP_SOCKET)-1){
		cu_lk_notice("cannot open UDP socket");
		return FALSE;
	}
	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(INADDR_ANY);
	local.sin_port = 0;
	if(bind(s, (struct sockaddr *)&local, (socklen_t)sizeof(local)) != 0){
		cu_lk_close(&s);
		cu_lk_notice("cannot bind UDP socket");
		return FALSE;
	}
	cu_lk_udp_setup(s);
	cu_lk.sock = s;
	cu_lk_set_phase(CU_LK_PH_JOINING, now);
	cu_lk_relay_request(UZRLT_JOIN_REQ, now);
	{
		char msg[112];
		snprintf(msg, sizeof(msg), "joining room %s via %s", cu_lk.room, cu_lk.relay_host);
		cu_lk_notice(msg);
	}
	return TRUE;
}

/* Accepts up to n bytes of serial data for transmission; returns bytes taken. */
static uint32 cu_lk_write(uint8 const *buf, uint32 n, uint32 now)
{
	uint32 used = 0u;
	if(!cu_lk.connected)
		return 0u;
	while(used < n && cu_lk.tx_count < CU_LK_WIN){
		cu_lk_slot_t *s = &cu_lk.tx[(cu_lk.tx_head + cu_lk.tx_count) % CU_LK_WIN];
		uint32 chunk = n - used;
		if(chunk > CU_LK_MAX_DATA)
			chunk = CU_LK_MAX_DATA;
		s->seq = cu_lk.tx_next++;
		s->len = chunk;
		s->retx = 0u;
		s->sacked = 0u;
		memcpy(s->data, buf + used, chunk);
		s->sent_ms = now;
		cu_lk.tx_count++;
		used += chunk;
		cu_lk_send_pkt(CU_LK_T_DATA, s->seq, s->data, s->len, now);
	}
	return used;
}

static uint32 cu_lk_read(uint8 *out, uint32 max)
{
	uint32 n = 0u;
	while(n < max && cu_lk.fifo_count != 0u){
		out[n++] = cu_lk.fifo[cu_lk.fifo_head];
		cu_lk.fifo_head = (cu_lk.fifo_head + 1u) % CU_LK_RXFIFO;
		cu_lk.fifo_count--;
	}
	return n;
}

static void cu_lk_service(uint32 now)
{
	uint8 buf[2048];
	uint32 i, resent = 0u;
	if(cu_lk.phase == CU_LK_PH_IDLE)
		return;
	if(cu_lk.phase == CU_LK_PH_RETRY){
		if(!CU_LK_TIME_GE(now, cu_lk.retry_at))
			return;
		if(cu_lk.sock == ESP_INVALID_SOCKET){
			char room[UZRL_ROOM_CODE_LEN + 1], host[64];
			uint32 port = cu_lk.relay_port;
			memcpy(room, cu_lk.room, sizeof(room));
			memcpy(host, cu_lk.relay_host, sizeof(host));
			(void)cu_lk_start(host, port, room, now);
			return;
		}
		cu_lk.relay_session_id = 0u;
		cu_lk.relay_room_id = 0u;
		cu_lk_set_phase(CU_LK_PH_JOINING, now);
		cu_lk_relay_request(UZRLT_JOIN_REQ, now);
		return;
	}
	if(cu_lk.sock == ESP_INVALID_SOCKET)
		return;

	for(i = 0u; i < 256u; i++){
		struct sockaddr_in from;
		socklen_t fl = (socklen_t)sizeof(from);
		int r = (int)recvfrom(cu_lk.sock, (char *)buf, (int)sizeof(buf), 0, (struct sockaddr *)&from, &fl);
		if(r < 0 && cu_lk_udp_soft_error())
			continue;
		if(r <= 0)
			break;
		if(cu_lk_addr_eq(&from, &cu_lk.relay_addr))
			cu_lk_on_relay_pkt(buf, (uint32)r, now);
		else if(cu_lk.direct_candidate && cu_lk_addr_eq(&from, &cu_lk.peer_addr))
			cu_lk_on_link_pkt(buf, (uint32)r, TRUE, now);
		else if(cu_lk.direct_candidate && from.sin_addr.s_addr == cu_lk.peer_addr.sin_addr.s_addr &&
		        (uint32)r >= CU_LK_HDR_LEN && cu_lk_get32(buf) == CU_LK_MAGIC){
			/* NAT changed the peer's source port: follow it. */
			cu_lk.peer_addr.sin_port = from.sin_port;
			cu_lk_on_link_pkt(buf, (uint32)r, TRUE, now);
		}
	}

	/* Relay housekeeping. */
	if((cu_lk.phase == CU_LK_PH_JOINING || cu_lk.phase == CU_LK_PH_CREATING) &&
	   CU_LK_TIME_GE(now, cu_lk.last_req_ms + CU_LK_REQ_RETRY_MS)){
		if(CU_LK_TIME_GE(now, cu_lk.phase_since + 10000u)){
			cu_lk_notice("relay not answering, retrying");
			cu_lk_set_phase(CU_LK_PH_RETRY, now);
			cu_lk.retry_at = now + CU_LK_RETRY_WAIT_MS;
			return;
		}
		cu_lk_relay_request((cu_lk.phase == CU_LK_PH_CREATING) ? UZRLT_CREATE_REQ : UZRLT_JOIN_REQ, now);
	}
	if((cu_lk.phase == CU_LK_PH_WAIT_PEER || cu_lk.phase == CU_LK_PH_PEER) &&
	   CU_LK_TIME_GE(now, cu_lk.last_hb_ms + CU_LK_HEARTBEAT_MS)){
		uint8 echo[4];
		cu_lk_put32(echo, now);
		cu_lk_relay_send(UZRLT_HEARTBEAT, echo, 4u);
		cu_lk.last_hb_ms = now;
	}
	if(cu_lk.phase != CU_LK_PH_PEER)
		return;

	/* Hole punching. */
	if(cu_lk.direct_candidate && !cu_lk.direct_ok && !CU_LK_TIME_GE(now, cu_lk.punch_until) &&
	   CU_LK_TIME_GE(now, cu_lk.last_punch_ms + CU_LK_PUNCH_MS)){
		cu_lk_send_pkt(CU_LK_T_PUNCH, 0u, NULL, 0u, now);
		cu_lk.last_punch_ms = now;
	}
	if(cu_lk.direct_ok && CU_LK_TIME_GE(now, cu_lk.last_direct_rx + CU_LK_DIRECT_STALE_MS)){
		cu_lk.direct_ok = 0u;
		cu_lk_notice("direct path went quiet, back on relay");
	}

	/* Handshake. */
	if(!cu_lk.connected){
		if(CU_LK_TIME_GE(now, cu_lk.last_hello_ms + CU_LK_HELLO_MS)){
			cu_lk_send_pkt(CU_LK_T_HELLO, 0u, NULL, 0u, now);
			cu_lk.last_hello_ms = now;
		}
		return;
	}

	/* Peer liveness. */
	if(CU_LK_TIME_GE(now, cu_lk.last_rx_ms + CU_LK_PEER_TIMEOUT_MS)){
		cu_lk_stream_reset();
		cu_lk_notice("peer timed out, waiting for it to come back");
		return;
	}

	/* Retransmission: holes before the highest SACKed packet go out early
	 * (fast retransmit); anything else after RTO with exponential backoff. */
	{
		uint32 highest_sacked = 0u, have_sacked = 0u;
		for(i = 0u; i < cu_lk.tx_count; i++){
			cu_lk_slot_t *s = &cu_lk.tx[(cu_lk.tx_head + i) % CU_LK_WIN];
			if(s->sacked){ highest_sacked = s->seq; have_sacked = 1u; }
		}
		for(i = 0u; i < cu_lk.tx_count && resent < 8u; i++){
			cu_lk_slot_t *s = &cu_lk.tx[(cu_lk.tx_head + i) % CU_LK_WIN];
			uint32 rto = cu_lk.rto << ((s->retx > 4u) ? 4u : s->retx);
			uint32 fast = (cu_lk.srtt != 0u) ? cu_lk.srtt : cu_lk.rto;
			boole hole = (have_sacked && (sint32)(s->seq - highest_sacked) < 0) ? TRUE : FALSE;
			if(s->sacked)
				continue;
			if(rto > 5000u) rto = 5000u;
			if(CU_LK_TIME_GE(now, s->sent_ms + rto) || (hole && CU_LK_TIME_GE(now, s->sent_ms + fast))){
				s->retx++;
				s->sent_ms = now;
				cu_lk.retransmits++;
				resent++;
				cu_lk_send_pkt(CU_LK_T_DATA, s->seq, s->data, s->len, now);
			}
		}
	}

	if(cu_lk.ack_pending || CU_LK_TIME_GE(now, cu_lk.last_tx_ms + CU_LK_KEEPALIVE_MS))
		cu_lk_send_pkt(CU_LK_T_ACK, 0u, NULL, 0u, now);
}

/* ------------------------------------------------------------------------- */
/* 2. LAN discovery                                                           */
/* ------------------------------------------------------------------------- */

#define CU_LAN_MAGIC        0x555A4C44u   /* "UZLD" */
#define CU_LAN_VERSION      1u
#define CU_LAN_BEACON_LEN   40u
#define CU_LAN_MAX_PEERS    8u
#define CU_LAN_BEACON_MS    500u
#define CU_LAN_PEER_TTL_MS  2500u
#define CU_LAN_PAIR_TIMEOUT 8000u
#define CU_LAN_DEFAULT_PORT 12002u
#define CU_LAN_MCAST_GROUP  0xEFFF554Cu   /* 239.255.85.76 */

#define CU_LAN_F_SEARCHING  0x01u
#define CU_LAN_F_LINKED     0x02u

typedef struct {
	uint32 used;
	uint32 id;
	uint32 rom;
	uint32 flags;
	uint32 want;
	uint32 tcp_port;
	uint32 ip_be;
	uint32 last_seen;
	char name[16];
} cu_lan_peer_t;

static struct {
	ESP_SOCKET sock;
	uint32 active;
	uint32 port;
	uint32 my_id;
	uint32 rom;
	uint32 tcp_port;
	uint32 want;            /* peer chosen by the user, 0 = automatic */
	uint32 proposed_id;     /* peer we offered to pair with (waiting for it to offer back) */
	uint32 proposed_at;
	uint32 backoff_until;
	uint32 paired_id;       /* peer we decided to link with, 0 = searching */
	uint32 paired_ip_be;
	uint32 paired_port;
	uint32 paired_server;   /* 1 = we listen, 0 = we connect */
	uint32 paired_at;
	uint32 linked;
	uint32 last_beacon;
	uint32 beacons_rx;
	cu_lan_peer_t peers[CU_LAN_MAX_PEERS];
	char name[16];
} cu_lan;

static void cu_lan_stop(void)
{
	cu_lk_close(&cu_lan.sock);
	cu_lan.active = 0u;
	cu_lan.paired_id = 0u;
	cu_lan.linked = 0u;
}

static boole cu_lan_start(uint32 disc_port, uint32 tcp_port, uint32 rom)
{
	struct sockaddr_in sa;
	ESP_SOCKET s;
	int one = 1;
	cu_lan_stop();
	memset(cu_lan.peers, 0, sizeof(cu_lan.peers));
	cu_lan.port = disc_port ? disc_port : CU_LAN_DEFAULT_PORT;
	cu_lan.tcp_port = tcp_port;
	cu_lan.rom = rom;
	cu_lan.want = 0u;
	cu_lan.paired_id = 0u;
	cu_lan.linked = 0u;
	cu_lan.last_beacon = 0u;
	do { cu_lan.my_id = cu_lk_rand(); } while(cu_lan.my_id == 0u);
	snprintf(cu_lan.name, sizeof(cu_lan.name), "CUzeBox-%04X", (unsigned)(cu_lan.my_id & 0xFFFFu));
	s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if(s == ESP_INVALID_SOCKET || s == (ESP_SOCKET)-1)
		return FALSE;
	/* Several CUzeBox instances on one machine all listen for beacons, so
	 * the discovery port is shared (UDP broadcast reaches every socket). */
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char const *)&one, (int)sizeof(one));
#ifdef SO_REUSEPORT
	setsockopt(s, SOL_SOCKET, SO_REUSEPORT, (char const *)&one, (int)sizeof(one));
#endif
	setsockopt(s, SOL_SOCKET, SO_BROADCAST, (char const *)&one, (int)sizeof(one));
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons((uint16)cu_lan.port);
	if(bind(s, (struct sockaddr *)&sa, (socklen_t)sizeof(sa)) != 0){
		cu_lk_close(&s);
		return FALSE;
	}
	{
		struct ip_mreq mr;
		int loop = 1, ttl = 1; /* int: Windows wants a DWORD, Linux accepts int */
		memset(&mr, 0, sizeof(mr));
		mr.imr_multiaddr.s_addr = htonl(CU_LAN_MCAST_GROUP);
		mr.imr_interface.s_addr = htonl(INADDR_ANY);
		(void)setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char const *)&mr, (int)sizeof(mr));
		(void)setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, (char const *)&loop, (int)sizeof(loop));
		(void)setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, (char const *)&ttl, (int)sizeof(ttl));
	}
	cu_lk_udp_setup(s);
	cu_lan.sock = s;
	cu_lan.active = 1u;
	print_message("Link: LAN discovery on UDP %u (id %08X)\n", (unsigned)cu_lan.port, (unsigned)cu_lan.my_id);
	return TRUE;
}

static void cu_lan_send_beacon(uint32 now)
{
	uint8 b[CU_LAN_BEACON_LEN];
	struct sockaddr_in to;
	uint32 flags = 0u;
	if(cu_lan.sock == ESP_INVALID_SOCKET)
		return;
	if(cu_lan.linked)
		flags |= CU_LAN_F_LINKED;
	else
		flags |= CU_LAN_F_SEARCHING;
	memset(b, 0, sizeof(b));
	cu_lk_put32(b + 0, CU_LAN_MAGIC);
	b[4] = (uint8)CU_LAN_VERSION;
	b[5] = (uint8)flags;
	cu_lk_put16(b + 6, cu_lan.tcp_port);
	cu_lk_put32(b + 8, cu_lan.my_id);
	cu_lk_put32(b + 12, cu_lan.rom);
	cu_lk_put32(b + 16, cu_lan.paired_id ? cu_lan.paired_id : (cu_lan.proposed_id ? cu_lan.proposed_id : cu_lan.want));
	memcpy(b + 20, cu_lan.name, 16);
	memset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_port = htons((uint16)cu_lan.port);
	to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	(void)sendto(cu_lan.sock, (char const *)b, (int)sizeof(b), 0, (struct sockaddr *)&to, (socklen_t)sizeof(to));
	/* Multicast copy (loopback enabled): reaches every instance on this
	 * machine even where limited broadcast is not looped back locally. */
	to.sin_addr.s_addr = htonl(CU_LAN_MCAST_GROUP);
	(void)sendto(cu_lan.sock, (char const *)b, (int)sizeof(b), 0, (struct sockaddr *)&to, (socklen_t)sizeof(to));
	cu_lan.last_beacon = now;
}

static void cu_lan_on_beacon(uint8 const *b, uint32 n, struct sockaddr_in const *from, uint32 now)
{
	uint32 id, i, slot = CU_LAN_MAX_PEERS, oldest = 0u;
	if(n < CU_LAN_BEACON_LEN || cu_lk_get32(b) != CU_LAN_MAGIC || b[4] != CU_LAN_VERSION)
		return;
	id = cu_lk_get32(b + 8);
	if(id == 0u || id == cu_lan.my_id)
		return;
	cu_lan.beacons_rx++;
	for(i = 0u; i < CU_LAN_MAX_PEERS; i++){
		if(cu_lan.peers[i].used && cu_lan.peers[i].id == id){ slot = i; break; }
	}
	if(slot == CU_LAN_MAX_PEERS){
		for(i = 0u; i < CU_LAN_MAX_PEERS; i++){
			if(!cu_lan.peers[i].used){ slot = i; break; }
			if(slot == CU_LAN_MAX_PEERS || (sint32)(cu_lan.peers[i].last_seen - oldest) < 0){
				oldest = cu_lan.peers[i].last_seen;
				slot = i;
			}
		}
	}
	{
		cu_lan_peer_t *p = &cu_lan.peers[slot];
		uint32 ip = from->sin_addr.s_addr;
		/* Prefer a real interface address over the loopback copy. */
		if(p->used && p->id == id && (ntohl(ip) >> 24) == 127u && (ntohl(p->ip_be) >> 24) != 127u &&
		   !CU_LK_TIME_GE(now, p->last_seen + CU_LAN_PEER_TTL_MS))
			ip = p->ip_be;
		p->used = 1u;
		p->id = id;
		p->flags = b[5];
		p->tcp_port = cu_lk_get16(b + 6);
		p->rom = cu_lk_get32(b + 12);
		p->want = cu_lk_get32(b + 16);
		p->ip_be = ip;
		p->last_seen = now;
		memcpy(p->name, b + 20, 16);
		p->name[15] = 0;
	}
}

static cu_lan_peer_t *cu_lan_find(uint32 id, uint32 now)
{
	uint32 i;
	for(i = 0u; i < CU_LAN_MAX_PEERS; i++){
		cu_lan_peer_t *p = &cu_lan.peers[i];
		if(p->used && p->id == id && !CU_LK_TIME_GE(now, p->last_seen + CU_LAN_PEER_TTL_MS))
			return p;
	}
	return NULL;
}

/* Returns TRUE when a new pairing decision was made this call. */
static boole cu_lan_service(uint32 now)
{
	uint8 buf[256];
	uint32 i;
	cu_lan_peer_t *target = NULL;
	if(!cu_lan.active)
		return FALSE;
	for(i = 0u; i < 64u; i++){
		struct sockaddr_in from;
		socklen_t fl = (socklen_t)sizeof(from);
		int r = (int)recvfrom(cu_lan.sock, (char *)buf, (int)sizeof(buf), 0, (struct sockaddr *)&from, &fl);
		if(r < 0 && cu_lk_udp_soft_error())
			continue;
		if(r <= 0)
			break;
		cu_lan_on_beacon(buf, (uint32)r, &from, now);
	}
	if(cu_lan.last_beacon == 0u || CU_LK_TIME_GE(now, cu_lan.last_beacon + CU_LAN_BEACON_MS))
		cu_lan_send_beacon(now);
	if(cu_lan.paired_id != 0u)
		return FALSE;

	/* Choose a partner: the one the user picked, else one that offered to
	 * pair with us, else the only other searching instance running the same
	 * ROM that has not offered itself to someone else. Pairing only commits
	 * once both sides name each other in their beacons, so three or more
	 * instances never end up sharing one partner. */
	if(cu_lan.want != 0u){
		target = cu_lan_find(cu_lan.want, now);
		if(target != NULL && !(target->flags & CU_LAN_F_SEARCHING))
			target = NULL;
	}else{
		uint32 count = 0u;
		cu_lan_peer_t *only = NULL;
		for(i = 0u; i < CU_LAN_MAX_PEERS; i++){
			cu_lan_peer_t *p = &cu_lan.peers[i];
			if(!p->used || CU_LK_TIME_GE(now, p->last_seen + CU_LAN_PEER_TTL_MS))
				continue;
			if(!(p->flags & CU_LAN_F_SEARCHING) || p->rom != cu_lan.rom)
				continue;
			if(p->want == cu_lan.my_id){ target = p; break; }
			if(p->want != 0u)
				continue; /* busy offering itself to someone else */
			count++;
			only = p;
		}
		if(target == NULL && count == 1u && CU_LK_TIME_GE(now, cu_lan.backoff_until))
			target = only;
	}
	if(target == NULL){
		if(cu_lan.proposed_id != 0u){
			cu_lan.proposed_id = 0u;
			cu_lan.backoff_until = now + 300u + cu_lk_rand() % 1200u;
		}
		return FALSE;
	}
	if(cu_lan.proposed_id != target->id){
		cu_lan.proposed_id = target->id;
		cu_lan.proposed_at = now;
		cu_lan_send_beacon(now);
	}
	if(target->want != cu_lan.my_id){
		if(CU_LK_TIME_GE(now, cu_lan.proposed_at + 3000u)){
			/* Not answered: withdraw and try again a little later. */
			cu_lan.proposed_id = 0u;
			cu_lan.backoff_until = now + 500u + cu_lk_rand() % 1500u;
		}
		return FALSE;
	}
	cu_lan.proposed_id = 0u;
	cu_lan.paired_id = target->id;
	cu_lan.paired_ip_be = target->ip_be;
	cu_lan.paired_server = (cu_lan.my_id > target->id) ? 1u : 0u;
	cu_lan.paired_port = cu_lan.paired_server ? cu_lan.tcp_port : target->tcp_port;
	cu_lan.paired_at = now;
	{
		struct in_addr a;
		a.s_addr = target->ip_be;
		print_message("Link: LAN pairing with %s (%s), we are %s\n", target->name, inet_ntoa(a),
			cu_lan.paired_server ? "server" : "client");
		(void)a;
	}
	cu_lan_send_beacon(now);
	return TRUE;
}

static void cu_lan_unpair(void)
{
	cu_lan.paired_id = 0u;
	cu_lan.proposed_id = 0u;
	cu_lan.linked = 0u;
	cu_lan.backoff_until = cu_esp_now_ms() + 300u + cu_lk_rand() % 1200u;
}
