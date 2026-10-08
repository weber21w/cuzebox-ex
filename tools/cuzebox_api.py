#!/usr/bin/env python3
"""Small standard-library client for the CUzeBox localhost command API."""

from __future__ import annotations

import json
import socket
import time
from typing import Any, Dict, List


class CUzeBoxApi:
    def __init__(self, host: str = "127.0.0.1", port: int = 24680, timeout: float = 5.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock: socket.socket | None = None
        self.hello: Dict[str, Any] | None = None

    def connect(self) -> None:
        if self.sock is not None:
            return
        self.sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        self.sock.settimeout(self.timeout)
        self.hello = self._recv_json()

    def close(self) -> None:
        if self.sock is not None:
            self.sock.close()
            self.sock = None

    def _recv_line(self) -> str:
        if self.sock is None:
            raise RuntimeError("API socket not connected")
        chunks: List[bytes] = []
        while True:
            ch = self.sock.recv(1)
            if not ch:
                raise RuntimeError("API connection closed")
            if ch == b"\n":
                break
            chunks.append(ch)
        return b"".join(chunks).decode("utf-8", errors="replace").strip()

    def _recv_json(self) -> Dict[str, Any]:
        line = self._recv_line()
        if not line:
            return {}
        return json.loads(line)

    def command(self, text: str) -> Dict[str, Any]:
        if self.sock is None:
            self.connect()
        assert self.sock is not None
        self.sock.sendall(text.encode("utf-8") + b"\n")
        return self._recv_json()

    def wait_ready(self, timeout: float = 10.0) -> Dict[str, Any]:
        deadline = time.time() + timeout
        last_err: Exception | None = None
        while time.time() < deadline:
            try:
                self.close()
                self.connect()
                return self.hello or {}
            except Exception as exc:  # noqa: BLE001
                last_err = exc
                time.sleep(0.1)
        raise RuntimeError(f"API did not become ready: {last_err}")

    def get_state(self) -> Dict[str, Any]:
        return self.command("GET_STATE")

    def wait_run_complete(self, timeout: float = 10.0) -> Dict[str, Any]:
        deadline = time.time() + timeout
        state = self.get_state()
        while time.time() < deadline:
            remaining = int(state.get("run_frames_remaining", 0))
            paused = int(state.get("paused", 0)) != 0
            if remaining == 0 and paused:
                return state
            time.sleep(0.01)
            state = self.get_state()
        raise RuntimeError("Timed out waiting for RUN_FRAMES to complete")
