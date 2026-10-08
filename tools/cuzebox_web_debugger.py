#!/usr/bin/env python3
"""CUzeBox localhost web debugger.

This program is intentionally only a frontend/transport bridge.  It never reads
or modifies emulator state directly: every debugger operation is sent through
the normal CUzeBox localhost API.
"""

from __future__ import annotations

import argparse
import json
import mimetypes
import pathlib
import threading
import urllib.parse
import webbrowser
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Dict

from cuzebox_api import CUzeBoxApi


ROOT = pathlib.Path(__file__).resolve().parent / "web_debugger"
INDEX = ROOT / "index.html"
DEBUGGER = ROOT / "debugger.html"
SD = ROOT / "sd.html"
AUDIO = ROOT / "audio.html"
SERIAL = ROOT / "serial.html"
NETWORK = ROOT / "network.html"
CONTROLS = ROOT / "controls.html"
UPLOAD_MAX = 512 * 1024 * 1024


class ApiBridge:
    def __init__(self, host: str, port: int, timeout: float) -> None:
        self.api = CUzeBoxApi(host, port, timeout)
        self.lock = threading.Lock()
        self.hello: Dict[str, Any] = {}

    def _connect_locked(self) -> None:
        if self.api.sock is None:
            self.api.connect()
            self.hello = self.api.hello or {}

    def status(self) -> Dict[str, Any]:
        with self.lock:
            try:
                self._connect_locked()
                return {
                    "ok": 1,
                    "connected": 1,
                    "api_host": self.api.host,
                    "api_port": self.api.port,
                    "hello": self.hello,
                }
            except Exception as exc:  # noqa: BLE001
                self.api.close()
                return {
                    "ok": 0,
                    "connected": 0,
                    "api_host": self.api.host,
                    "api_port": self.api.port,
                    "error": str(exc),
                }

    def command(self, command: str) -> Dict[str, Any]:
        command = command.strip()
        if not command:
            return {"ok": 0, "error": "empty API command"}
        # A browser page should never be able to smuggle another line into the
        # line-oriented API connection.
        if "\n" in command or "\r" in command:
            return {"ok": 0, "error": "API command must be a single line"}
        with self.lock:
            last_error: Exception | None = None
            for attempt in range(2):
                try:
                    self._connect_locked()
                    return self.api.command(command)
                except Exception as exc:  # noqa: BLE001
                    last_error = exc
                    self.api.close()
                    # Retry a stale/disconnected socket once.  Do not retry
                    # QUIT because success naturally closes the emulator soon
                    # afterward and replaying it is not useful.
                    if command.upper() == "QUIT" or attempt != 0:
                        break
            return {"ok": 0, "error": f"CUzeBox API unavailable: {last_error}"}

    def close(self) -> None:
        with self.lock:
            self.api.close()


class DebuggerHTTPServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address: tuple[str, int], handler: type[BaseHTTPRequestHandler], bridge: ApiBridge):
        super().__init__(address, handler)
        self.bridge = bridge
        self.upload_lock = threading.Lock()
        self.uploads: dict[int, dict[str, Any]] = {}
        self.next_upload_id = 1

    def server_close(self) -> None:
        with self.upload_lock:
            uploads = list(self.uploads.values())
            self.uploads.clear()
        for up in uploads:
            try: up["file"].close()
            except Exception: pass
            try: pathlib.Path(up["path"]).unlink()
            except FileNotFoundError: pass
        super().server_close()


class Handler(BaseHTTPRequestHandler):
    server_version = "CUzeBoxWebDebugger/0.1"

    @property
    def bridge(self) -> ApiBridge:
        return self.server.bridge  # type: ignore[attr-defined,no-any-return]

    def log_message(self, fmt: str, *args: object) -> None:
        # Keep normal operation quiet; startup already prints the useful URLs.
        if getattr(self.server, "verbose", False):
            super().log_message(fmt, *args)

    def _json(self, obj: object, status: HTTPStatus = HTTPStatus.OK) -> None:
        payload = json.dumps(obj, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; connect-src 'self'")
        self.end_headers()
        self.wfile.write(payload)

    def _serve_file(self, path: pathlib.Path) -> None:
        try:
            resolved = path.resolve()
            root = ROOT.resolve()
            if root not in resolved.parents and resolved != root:
                self.send_error(HTTPStatus.FORBIDDEN)
                return
            data = resolved.read_bytes()
        except FileNotFoundError:
            self.send_error(HTTPStatus.NOT_FOUND)
            return
        mime = mimetypes.guess_type(str(resolved))[0] or "application/octet-stream"
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; connect-src 'self'")
        self.end_headers()
        self.wfile.write(data)

    @staticmethod
    def _safe_name(name: str, fallback: str) -> str:
        base = pathlib.Path(name or fallback).name
        safe = "".join(c if c.isalnum() or c in "._-" else "_" for c in base)
        return safe or fallback

    def _memory_dump(self, parsed: urllib.parse.ParseResult) -> None:
        qs = urllib.parse.parse_qs(parsed.query)
        region = qs.get("region", ["SRAM"])[0].upper()
        if region not in {"SRAM", "IO", "FLASH", "EEPROM", "SPIRAM"}:
            self._json({"ok": 0, "error": "invalid memory region"}, HTTPStatus.BAD_REQUEST); return
        info = self.bridge.command(f"MEM_SIZE {region}")
        if not info.get("ok"):
            self._json(info, HTTPStatus.BAD_REQUEST); return
        size = int(info.get("size", 0)); addr = int(qs.get("addr", ["0"])[0], 0); requested = int(qs.get("len", ["0"])[0], 0)
        if addr < 0 or addr >= size:
            self._json({"ok": 0, "error": "memory range unavailable"}, HTTPStatus.BAD_REQUEST); return
        length = min(requested if requested > 0 else size - addr, size - addr, UPLOAD_MAX)
        name = self._safe_name(qs.get("name", [f"{region.lower()}.bin"])[0], "memory.bin")
        self.send_response(HTTPStatus.OK); self.send_header("Content-Type", "application/octet-stream"); self.send_header("Content-Length", str(length)); self.send_header("Content-Disposition", f'attachment; filename="{name}"'); self.send_header("Cache-Control", "no-store"); self.end_headers()
        off = 0
        while off < length:
            n = min(256, length - off); r = self.bridge.command(f"READ_MEM {region} {addr + off} {n}")
            vals = r.get("values") if r.get("ok") else None
            if not isinstance(vals, list) or len(vals) != n: return
            self.wfile.write(bytes(int(v) & 0xFF for v in vals)); off += n

    def _screenshot_download(self, parsed: urllib.parse.ParseResult) -> None:
        qs = urllib.parse.parse_qs(parsed.query); name = self._safe_name(qs.get("name", ["cuzebox-screenshot.bmp"])[0], "cuzebox-screenshot.bmp")
        info = self.bridge.command("SCREENSHOT_CAPTURE")
        if not info.get("ok") or int(info.get("size", 0)) <= 0:
            self._json({"ok": 0, "error": "screenshot capture failed"}, HTTPStatus.BAD_REQUEST); return
        size = int(info["size"]); width = int(info.get("width", 0)); height = int(info.get("height", 0))
        self.send_response(HTTPStatus.OK); self.send_header("Content-Type", "image/bmp"); self.send_header("Content-Length", str(size)); self.send_header("Content-Disposition", f'attachment; filename="{name}"'); self.send_header("Cache-Control", "no-store"); self.send_header("X-CUzeBox-Image-Size", f"{width}x{height}"); self.end_headers()
        try:
            off = 0
            while off < size:
                n = min(4096, size - off); r = self.bridge.command(f"SCREENSHOT_READ {off} {n}")
                vals = r.get("values") if r.get("ok") else None
                if not isinstance(vals, list) or len(vals) != n: return
                self.wfile.write(bytes(int(v) & 0xFF for v in vals)); off += n
        finally:
            self.bridge.command("SCREENSHOT_CLEAR")

    def do_GET(self) -> None:  # noqa: N802
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path in ("/", "/index.html"):
            self._serve_file(INDEX)
            return
        if parsed.path in ("/controls", "/controls/", "/controls.html"):
            self._serve_file(CONTROLS)
            return
        if parsed.path in ("/debugger", "/debugger/", "/debugger.html"):
            self._serve_file(DEBUGGER)
            return
        if parsed.path in ("/sd", "/sd/", "/sd.html"):
            self._serve_file(SD)
            return
        if parsed.path in ("/audio", "/audio/", "/audio.html"):
            self._serve_file(AUDIO)
            return
        if parsed.path in ("/serial", "/serial/", "/serial.html"):
            self._serve_file(SERIAL)
            return
        if parsed.path in ("/network", "/network/", "/network.html"):
            self._serve_file(NETWORK)
            return
        if parsed.path == "/api/status":
            self._json(self.bridge.status())
            return
        if parsed.path == "/api/memory-dump":
            self._memory_dump(parsed)
            return
        if parsed.path == "/api/screenshot-download":
            self._screenshot_download(parsed)
            return
        if parsed.path.startswith("/static/"):
            rel = parsed.path[len("/static/") :]
            self._serve_file(ROOT / rel)
            return
        self.send_error(HTTPStatus.NOT_FOUND)

    def do_POST(self) -> None:  # noqa: N802
        parsed = urllib.parse.urlparse(self.path)
        qs = urllib.parse.parse_qs(parsed.query)
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self._json({"ok": 0, "error": "invalid Content-Length"}, HTTPStatus.BAD_REQUEST); return
        if parsed.path == "/api/upload-rom/start":
            name = self._safe_name(qs.get("name", ["upload.uze"])[0], "upload.uze"); mode = qs.get("mode", ["WAIT"])[0].upper(); status = self.bridge.command("EMU_STATUS"); directory = pathlib.Path(str(status.get("rom_dir") or ".")); directory = directory.expanduser()
            if not directory.is_dir(): self._json({"ok": 0, "error": "configured ROM directory is unavailable"}, HTTPStatus.BAD_REQUEST); return
            target = directory / name
            with self.server.upload_lock:  # type: ignore[attr-defined]
                uid = self.server.next_upload_id; self.server.next_upload_id += 1  # type: ignore[attr-defined]
                if target.exists():
                    attempt = 1
                    while True:
                        candidate = directory / f"web-{uid:08d}-{attempt:03d}-{name}"
                        if not candidate.exists(): target = candidate; break
                        attempt += 1
                try: f = target.open("wb")
                except OSError as exc: self._json({"ok": 0, "error": f"cannot create uploaded ROM: {exc}"}, HTTPStatus.BAD_REQUEST); return
                self.server.uploads[uid] = {"file": f, "path": target, "bytes": 0, "run": mode == "RUN"}  # type: ignore[attr-defined]
            self._json({"ok": 1, "id": uid, "path": str(target), "max_bytes": UPLOAD_MAX}); return
        if parsed.path in ("/api/upload-rom/chunk", "/api/upload-rom/finish", "/api/upload-rom/cancel"):
            try: uid = int(qs.get("id", ["0"])[0], 0)
            except ValueError: uid = 0
            with self.server.upload_lock: up = self.server.uploads.get(uid)  # type: ignore[attr-defined]
            if not up: self._json({"ok": 0, "error": "upload not found"}, HTTPStatus.BAD_REQUEST); return
            if parsed.path == "/api/upload-rom/chunk":
                if length < 0 or int(up["bytes"]) + length > UPLOAD_MAX: self._json({"ok": 0, "error": "upload too large"}, HTTPStatus.BAD_REQUEST); return
                data = self.rfile.read(length); up["file"].write(data); up["bytes"] = int(up["bytes"]) + len(data); self._json({"ok": 1, "id": uid, "received": up["bytes"]}); return
            up["file"].close()
            with self.server.upload_lock: self.server.uploads.pop(uid, None)  # type: ignore[attr-defined]
            if parsed.path == "/api/upload-rom/cancel":
                pathlib.Path(up["path"]).unlink(missing_ok=True); self._json({"ok": 1}); return
            result = self.bridge.command(f"LOAD_ROM {'RUN' if up['run'] else 'WAIT'} {up['path']}")
            if not result.get("ok"): pathlib.Path(up["path"]).unlink(missing_ok=True)
            self._json(result); return
        if parsed.path != "/api/command":
            self.send_error(HTTPStatus.NOT_FOUND)
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self._json({"ok": 0, "error": "invalid Content-Length"}, HTTPStatus.BAD_REQUEST)
            return
        if length <= 0 or length > 16384:
            self._json({"ok": 0, "error": "invalid request size"}, HTTPStatus.BAD_REQUEST)
            return
        try:
            req = json.loads(self.rfile.read(length).decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            self._json({"ok": 0, "error": "request must be JSON"}, HTTPStatus.BAD_REQUEST)
            return
        command = req.get("command") if isinstance(req, dict) else None
        if not isinstance(command, str):
            self._json({"ok": 0, "error": "missing string field: command"}, HTTPStatus.BAD_REQUEST)
            return
        self._json(self.bridge.command(command))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Serve the CUzeBox debugger UI and proxy it through the existing CUzeBox API"
    )
    parser.add_argument("--api-host", default="127.0.0.1", help="CUzeBox API host (default: 127.0.0.1)")
    parser.add_argument("--api-port", type=int, default=24680, help="CUzeBox API port (default: 24680)")
    parser.add_argument("--listen", default="127.0.0.1", help="HTTP listen address (default: localhost only)")
    parser.add_argument("--port", type=int, default=24681, help="HTTP port (default: 24681)")
    parser.add_argument("--timeout", type=float, default=5.0, help="API command timeout in seconds")
    parser.add_argument("--no-browser", action="store_true", help="do not open the default browser")
    parser.add_argument("--page", choices=("home", "controls", "debugger", "sd", "audio", "serial", "network"), default="debugger", help="page to open initially (default: debugger)")
    parser.add_argument("--verbose", action="store_true", help="log HTTP requests")
    args = parser.parse_args()

    for page in (INDEX, CONTROLS, DEBUGGER, SD, AUDIO, SERIAL, NETWORK):
        if not page.is_file():
            raise SystemExit(f"missing web tools page: {page}")

    bridge = ApiBridge(args.api_host, args.api_port, args.timeout)
    server = DebuggerHTTPServer((args.listen, args.port), Handler, bridge)
    server.verbose = args.verbose  # type: ignore[attr-defined]
    initial_path = "" if args.page == "home" else args.page
    url = f"http://{args.listen}:{args.port}/{initial_path}"

    print(f"CUzeBox web tools: {url}")
    print(f"CUzeBox API backend: {args.api_host}:{args.api_port}")
    print("All emulator/debugger state is accessed through the CUzeBox API.")
    if args.listen not in ("127.0.0.1", "localhost", "::1"):
        print("WARNING: the web debugger is listening beyond localhost.")

    if not args.no_browser:
        # Give the listener a moment to enter serve_forever before the browser
        # tries to fetch the page on slower systems.
        threading.Timer(0.15, lambda: webbrowser.open(url)).start()

    try:
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        bridge.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
