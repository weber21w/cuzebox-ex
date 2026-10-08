#!/usr/bin/env python3
"""Self-contained transport regression for the CUzeBox web debugger bridge."""

from __future__ import annotations

import json
import socket
import threading
import urllib.request

from cuzebox_web_debugger import ApiBridge, DebuggerHTTPServer, Handler


def main() -> int:
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    api_port = listener.getsockname()[1]
    listener.listen(1)

    def fake_api() -> None:
        conn, _ = listener.accept()
        conn.sendall((json.dumps({"ok": 1, "hello": "CUzeBox API", "port": api_port}) + "\n").encode())
        stream = conn.makefile("rwb")
        try:
            for raw in stream:
                command = raw.decode().strip()
                if command == "PING":
                    reply = {"ok": 1, "reply": "PONG"}
                elif command == "GET_STATE":
                    reply = {"ok": 1, "paused": 1, "frame": 42, "api_port": api_port}
                elif command == "MEM_SIZE SRAM":
                    reply = {"ok": 1, "region": "SRAM", "size": 4096}
                elif command.startswith("READ_MEM SRAM "):
                    _, _, addr, count = command.split(); addr = int(addr, 0); count = int(count, 0); reply = {"ok": 1, "addr": addr, "len": count, "values": [((addr + i) * 3 + 1) & 255 for i in range(count)]}
                else:
                    reply = {"ok": 1, "echo": command}
                stream.write((json.dumps(reply) + "\n").encode())
                stream.flush()
        finally:
            conn.close()

    threading.Thread(target=fake_api, daemon=True).start()
    bridge = ApiBridge("127.0.0.1", api_port, 2.0)
    web = DebuggerHTTPServer(("127.0.0.1", 0), Handler, bridge)
    web.verbose = False  # type: ignore[attr-defined]
    web_port = web.server_address[1]
    threading.Thread(target=web.serve_forever, daemon=True).start()

    try:
        status = json.load(urllib.request.urlopen(f"http://127.0.0.1:{web_port}/api/status"))
        request = urllib.request.Request(
            f"http://127.0.0.1:{web_port}/api/command",
            data=json.dumps({"command": "PING"}).encode(),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        ping = json.load(urllib.request.urlopen(request))
        home = urllib.request.urlopen(f"http://127.0.0.1:{web_port}/").read().decode()
        controls = urllib.request.urlopen(f"http://127.0.0.1:{web_port}/controls").read().decode()
        debugger = urllib.request.urlopen(f"http://127.0.0.1:{web_port}/debugger").read().decode()
        sdpage = urllib.request.urlopen(f"http://127.0.0.1:{web_port}/sd").read().decode()
        audiopage = urllib.request.urlopen(f"http://127.0.0.1:{web_port}/audio").read().decode()
        memdump = urllib.request.urlopen(f"http://127.0.0.1:{web_port}/api/memory-dump?region=SRAM&addr=5&len=40&name=t.bin").read()
        serial = urllib.request.urlopen(f"http://127.0.0.1:{web_port}/serial").read().decode()
        network = urllib.request.urlopen(f"http://127.0.0.1:{web_port}/network").read().decode()
        css = urllib.request.urlopen(f"http://127.0.0.1:{web_port}/static/common.css").read().decode()
        assert status.get("connected") == 1
        assert ping.get("reply") == "PONG"
        assert "<title>CUzeBox Web Tools</title>" in home
        assert "<title>CUzeBox Controls</title>" in controls and "uploadRom" in controls
        assert "<title>CUzeBox Web Debugger</title>" in debugger
        assert memdump == bytes(((5 + i) * 3 + 1) & 255 for i in range(40))
        assert "LOAD_SYMBOLS" in debugger and "WRITE_MEM" in debugger and "DEBUG_PROFILE" in debugger
        assert "sdStepSummary" in debugger and "waitDebugStepStop" in debugger and "SD_STATUS" in debugger and "SD_TRACE" in debugger and "sdTraceRows" in debugger and "sdTraceTransactions" in debugger and "selectSdTraceEvent" in debugger and "sdProtocolTimeline" in debugger and "sdPayload" in debugger and "SD_PAYLOAD" in debugger and "Filesystem context" in debugger and "SD_FS_HISTORY" in debugger and "sdFsHistory" in debugger
        assert "<title>CUzeBox SD Card</title>" in sdpage and "Command argument" in sdpage and "AVR SPI transfer" in sdpage and "SD_TRACE" in sdpage and "traceRows" in sdpage and "traceTransactions" in sdpage and "protocolTimeline" in sdpage and "payloadInspector" in sdpage and "SD_PAYLOAD" in sdpage and "Filesystem role/cluster" in sdpage and "SD_FS_HISTORY" in sdpage and "fsHistory" in sdpage
        assert "<title>CUzeBox Audio Scope</title>" in audiopage
        assert "<title>CUzeBox Serial Debugger</title>" in serial and "SERIAL_TRACE" in serial
        assert "<title>CUzeBox Network Debugger</title>" in network and "ESP_STATUS" in network and "NETPLAY_STATUS" in network
        assert ".panel" in css and ".sd-proto" in css and ".sd-tx" in css and ".sd-payload" in css and ".sd-fs-context" in css
        print("web tools bridge regression: PASS")
        return 0
    finally:
        web.shutdown()
        web.server_close()
        bridge.close()
        listener.close()


if __name__ == "__main__":
    raise SystemExit(main())
