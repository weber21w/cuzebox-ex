#!/usr/bin/env python3
"""Private local Room + Identity services, with per-player emulator configs.

Run natively on Linux or under WSL. No production database or credentials used.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import shutil
import signal
import socket
import sqlite3
import subprocess
import time
import tempfile

from test_uzenet_room import cert, port


def run(*arguments):
    result = subprocess.run([str(a) for a in arguments], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"{arguments[0]} failed:\n{result.stderr}")


class LocalNetwork:
    def __init__(self, source, work, players=2, baud=115200, relay_address="127.0.0.1"):
        self.source = Path(source).resolve()
        self.work = Path(work).resolve()
        if not 1 <= players <= 4:
            raise ValueError("Choose one to four players")
        # A fresh directory prevents accidental replacement of earlier identities.
        self.work.mkdir(parents=True, exist_ok=False)
        self.players = players
        if baud not in (9600, 115200):
            raise ValueError("Choose 9600 or 115200 baud")
        socket.inet_aton(relay_address)
        self.relay_address = relay_address
        self.baud = baud
        self.processes, self.logs, self.identities = [], [], []
        self.room_port = port()
        self.runtime = tempfile.TemporaryDirectory(prefix="cuzebox-network-")
        self.identity_socket = Path(self.runtime.name) / "identity.sock"

    def launch(self, arguments, cwd=None, env=None, name="service"):
        log = (self.work / (name + ".log")).open("w", encoding="utf-8")
        self.logs.append(log)
        process = subprocess.Popen([str(a) for a in arguments], cwd=cwd, env=env,
                                   stdout=log, stderr=log)
        self.processes.append(process)
        return process

    def lookup(self, digest):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.settimeout(15)
            connection.connect(str(self.identity_socket))
            connection.sendall(b"UZCERT01" + digest)
            reply = bytearray()
            while len(reply) < 6:
                data = connection.recv(6 - len(reply))
                if not data:
                    break
                reply.extend(data)
        if len(reply) != 6:
            raise AssertionError("Identity rejected the registered certificate")
        return int.from_bytes(reply[:2], "big")

    def start(self):
        source, work = self.source, self.work
        crt, key = cert(work, "localhost")
        database = work / "accounts.db"
        now = int(time.time())
        with sqlite3.connect(database) as db:
            db.executescript((source / "uzenet-api/schema.sql").read_text())
            for i in range(self.players):
                device_crt, device_key = cert(work, f"player-{i+1}")
                public = subprocess.check_output(
                    ["openssl", "x509", "-in", str(device_crt), "-pubkey", "-noout"])
                der = subprocess.check_output(
                    ["openssl", "pkey", "-pubin", "-outform", "DER"], input=public)
                digest = hashlib.sha256(der).digest()
                uid = 1001 + i
                db.execute("INSERT INTO users(id,display_name,password_hash,status,created_at,updated_at)"
                           " VALUES(?,?,?,'active',?,?)", (uid, f"UZETEST{i+1}", "unused", now, now))
                db.executemany("INSERT INTO user_permissions(user_id,permission,granted_at) VALUES(?,?,?)",
                               [(uid, permission, now) for permission in ("room.use", "gameplay.use", "matchmaking.use")])
                device = db.execute("INSERT INTO device_codes(user_id,label,code_hash,code_last4,created_at)"
                                    " VALUES(?,?,?,?,?)",
                                    (uid, "Local emulator", secrets.token_hex(32), "0000", now)).lastrowid
                db.execute("INSERT INTO device_certificates(device_code_id,pubkey_sha256,created_at)"
                           " VALUES(?,?,?)", (device, digest, now))
                self.identities.append(dict(uid=uid, certificate=device_crt, key=device_key, digest=digest))
        identity = source / "uzenet-identity"
        run("cc", "-std=c11", "-O2", "-pthread", f'-DSOCK_PATH="{self.identity_socket}"',
            "-I" + str(identity / "tests/include"), identity / "uzenet-identity-server.c",
            "-lcrypto", "-Wl,-l:libsqlite3.so.0", "-o", work / "identity")
        process = self.launch([work / "identity"], name="identity",
                              env=dict(os.environ, UZENET_DB_PATH=str(database),
                                       UZENET_DEVICE_PEPPER=secrets.token_hex(32)))
        for _ in range(100):
            if self.identity_socket.exists():
                break
            if process.poll() is not None:
                raise RuntimeError("Identity startup failed; see identity.log")
            time.sleep(.05)
        else:
            raise RuntimeError("Identity socket was not created")
        for record in self.identities:
            assert self.lookup(record["digest"]) == record["uid"]
        # The upstream server binds all interfaces. Adapt only the bind addresses
        # in a private build copy; leave the user's checkout and authentication intact.
        room_source = (source / "uzenet-room/uzenet-room-server.c").read_text()
        binding = ".sin_addr.s_addr = INADDR_ANY;"
        if room_source.count(binding) != 2:
            raise RuntimeError("Room bind code changed; review loopback adaptation before launching")
        room_copy = work / "room-server-local.c"
        room_copy.write_text(room_source.replace("uaddr"+binding, f'uaddr.sin_addr.s_addr = inet_addr("{self.relay_address}");').replace(binding, ".sin_addr.s_addr = htonl(INADDR_LOOPBACK);"),
                             encoding="utf-8")
        run("cc", "-std=gnu11", "-O2", "-pthread", f"-DPORT={self.room_port}",
            f"-DUDP_PORT={self.room_port}", f'-DIDENTITY_PATH="{self.identity_socket}"',
            "-I" + str(source / "uzenet-room"),
            f'-DCERT_FILE="{crt}"', f'-DKEY_FILE="{key}"',
            f'-DROOM_SVC_SOCK_PATH="{Path(self.runtime.name) / "room.sock"}"',
            f'-DROOM_STATUS_PATH="{work / "room-status.json"}"',
            room_copy,
            source / "uzenet-identity/uzenet-identity-client.c",
            source / "uzenet-audit/native.c",
            "-lssl", "-lcrypto", "-o", work / "room")
        process = self.launch(["stdbuf", "-oL", "-eL", work / "room"], name="room",
                              env=dict(os.environ, UZENET_UN_RELAY_IPV4=self.relay_address))
        for _ in range(100):
            try:
                with socket.create_connection(("127.0.0.1", self.room_port), timeout=.1):
                    break
            except OSError:
                if process.poll() is not None:
                    raise RuntimeError("Room startup failed; see room.log")
                time.sleep(.05)
        else:
            raise RuntimeError("Room listener was not created")
        for i, record in enumerate(self.identities):
            folder = work / f"player-{i+1}"
            folder.mkdir()
            config = folder / "config.cfg"
            values = dict(SerialRoute=1, EspEmulationModel=1, EspAtFirmwareProfile=2,
                          Baud=self.baud, UartProfile=1, WifiMode=1, WifiName="UzenetTest",
                          WifiPass="test", EspAtTranslinkEnabled=1, EspAtTranslinkKind=3,
                          EspAtTranslinkHost="localhost", EspAtTranslinkPort=self.room_port,
                          SslAuthMode0=3, SslPkiNum0=i, SslCaNum0=i)
            values.update({f"SslCaPath{i}": crt, f"SslPkiCertPath{i}": record["certificate"],
                           f"SslPkiKeyPath{i}": record["key"]})
            config.write_text("ResidentBootloader=1\nRenderPath=3\nSpiRamPagesMode=8\n" +
                              "".join(f'{k}="{v}"\n' for k, v in values.items()), encoding="utf-8")
            record["config"] = config
        manifest = dict(room_port=self.room_port, source=str(source), identity_service="real",
                        players=[dict(uid=r["uid"], config=str(r["config"]),
                                      certificate=str(r["certificate"])) for r in self.identities])
        (work / "session.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        return self

    def stop(self):
        for process in reversed(self.processes):
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        for log in self.logs:
            log.close()
        self.runtime.cleanup()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server-root", type=Path, required=True, help="Local uzenet source checkout")
    parser.add_argument("--work-dir", type=Path, required=True, help="New directory for private test data")
    parser.add_argument("--emulator", type=Path, help="Native Linux CUzeboxEX executable; omit for servers only")
    parser.add_argument("--rom", type=Path, help="ROM to copy into each player's SD directory")
    parser.add_argument("--players", type=int, default=2)
    parser.add_argument("--headless", action="store_true")
    parser.add_argument("--baud", type=int, choices=(9600, 115200), default=115200,
                        help="Saved ESP startup baud (9600 for legacy UzeSpy)")
    args = parser.parse_args()
    if bool(args.emulator) != bool(args.rom):
        parser.error("--emulator and --rom must be supplied together")
    network = LocalNetwork(args.server_root, args.work_dir, args.players, args.baud)
    def terminate(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, terminate)
    try:
        network.start()
        for i, record in enumerate(network.identities):
            print(f'Player {i+1}: account {record["uid"]}, config {record["config"]}', flush=True)
            if args.emulator:
                folder = record["config"].parent
                rom = folder / args.rom.name
                shutil.copy2(args.rom, rom)
                api_port = port()
                command = [args.emulator.resolve(), "--config", record["config"],
                           "--api-port", str(api_port)]
                if args.headless:
                    command.append("--headless")
                command.append(rom)
                network.launch(command, cwd=folder, name=f"emulator-{i+1}")
                print(f"  Emulator API: {api_port}", flush=True)
        print(f"Room: localhost:{network.room_port}. Ctrl+C stops all test processes.", flush=True)
        while all(process.poll() is None for process in network.processes):
            time.sleep(.5)
        failed = next((process for process in network.processes
                       if process.poll() not in (None, 0)), None)
        if failed is not None:
            raise RuntimeError(f"Test process exited with status {failed.returncode}; see logs in {network.work}")
    except KeyboardInterrupt:
        pass
    finally:
        network.stop()


if __name__ == "__main__":
    main()
