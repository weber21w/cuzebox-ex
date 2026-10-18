#!/usr/bin/env python3
"""Record/replay a self-contained movie through the headless API."""
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from cuzebox_api import CUzeBoxApi

ROOT = Path(__file__).resolve().parents[1]

def main():
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="movie-api-", dir=ROOT / "_obj_") as temp:
        folder = Path(temp)
        (folder / "config.cfg").write_text("ApiServer=0\nResidentBootloader=0\nFrameRateLimiter=0\n", encoding="utf-8")
        # Infinite AVR loop; input/state validation is exercised by the C suite.
        header = bytearray(512); header[:6] = b"UZEBOX"; header[6] = 1
        header[8:12] = struct.pack("<I", 2)
        (folder / "game.uze").write_bytes(header + bytes([0xFF, 0xCF]))
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0)); port = probe.getsockname()[1]
        process = subprocess.Popen([str(exe), "--headless", "--paused", "--unthrottled", "--api-port", str(port), str(folder / "game.uze")],
            cwd=folder, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        client = CUzeBoxApi(port=port, timeout=20)
        try:
            client.wait_ready(20)
            def command(text):
                result = client.command(text)
                assert result.get("ok"), (text, result)
                return result
            movie = folder / "input with spaces.cic"
            command("INPUT_MOVIE RECORD RESET " + str(movie))
            command("RUN_FRAMES 256"); client.wait_run_complete(20)
            command("PAUSE"); command("INPUT_MOVIE STOP")
            expected = command("READ_REGS")
            data = movie.read_bytes()
            assert data[:8] == b"CZINPUT\0"
            offset = 16; frames = 0; state_chunk = None
            while offset < len(data):
                tag, size, checksum = struct.unpack_from("<4sII", data, offset)
                payload = data[offset+12:offset+12+size]
                assert len(payload) == size and zlib.crc32(payload) == checksum
                if tag == b"STAT":
                    state_chunk = offset
                frames += tag == b"FRAM"
                offset += 12 + size
            assert frames >= 256 and tag == b"END!" and offset == len(data)
            # Valid container CRC with an overflowing nested snapshot length
            # must be rejected before the machine state is touched.
            damaged = bytearray(data)
            stat_size = struct.unpack_from("<I", damaged, state_chunk + 4)[0]
            struct.pack_into("<I", damaged, state_chunk + 12 + 16, 0xFFFFFFF0)
            struct.pack_into("<I", damaged, state_chunk + 8,
                             zlib.crc32(damaged[state_chunk+12:state_chunk+12+stat_size]))
            (folder / "bad-state.cic").write_bytes(damaged)
            assert not client.command("INPUT_MOVIE PLAY " + str(folder / "bad-state.cic")).get("ok")
            assert command("READ_REGS") == expected
            oversized = bytearray(data); struct.pack_into("<I", oversized, 20, 0xFFFFFFFF)
            (folder / "oversized.cic").write_bytes(oversized)
            assert not client.command("INPUT_MOVIE PLAY " + str(folder / "oversized.cic")).get("ok")
            assert command("READ_REGS") == expected
            command("INPUT_MOVIE PLAY " + str(movie))
            command("RUN_FRAMES 100000")  # EOF must cancel an oversized run request.
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                status = command("INPUT_MOVIE STATUS")
                if not status["active"]:
                    break
                time.sleep(.01)
            assert not status["active"] and status["status"] == "Input replay complete.", status
            final = command("GET_STATE")
            assert final["paused"] and final["run_frames_remaining"] == 0
            assert command("READ_REGS") == expected
            command("SCREENSHOT_CAPTURE")
            # Earlier CZINPUT recordings remain valid with their .cap suffix.
            old_name = folder / "versioned.cap"
            old_name.write_bytes(data)
            command("INPUT_MOVIE PLAY " + str(old_name))
            command("PAUSE"); command("INPUT_MOVIE STOP")
            (folder / "legacy.cap").write_bytes(b"\0\0")
            rejected = client.command("INPUT_MOVIE PLAY " + str(folder / "legacy.cap"))
            assert not rejected.get("ok") and "legacy" in str(rejected).lower()
            command("QUIT"); process.wait(timeout=10)
            assert process.returncode == 0
        finally:
            client.close()
            if process.poll() is None:
                process.terminate(); process.wait(timeout=10)
    print("Input movie headless API: PASS")

if __name__ == "__main__":
    main()
