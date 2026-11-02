#!/usr/bin/env python3
"""Run an offscreen API session with deliberately unavailable host devices."""
import os
import pathlib
import socket
import subprocess
import sys
import tempfile
import time

from cuzebox_api import CUzeBoxApi

ROOT = pathlib.Path(__file__).resolve().parent.parent


def main():
    executable = pathlib.Path(sys.argv[1]).resolve()
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    with tempfile.TemporaryDirectory(prefix="headless-api-", dir=ROOT / "_obj_") as temp:
        directory = pathlib.Path(temp)
        config = "ApiServer=0\nApiServerPort=7431\nFrameRateLimiter=0\n"
        (directory / "config.cfg").write_text(config, encoding="utf-8")
        env = dict(os.environ, SDL_VIDEODRIVER="invalid-headless-test", SDL_AUDIODRIVER="invalid-headless-test")
        process = subprocess.Popen([str(executable), "--headless", "--paused", "--api-port", str(port)],
                                   cwd=directory, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        client = CUzeBoxApi(port=port)
        try:
            client.wait_ready(15)
            def command(text):
                result = client.command(text)
                assert result.get("ok"), (text, result)
                return result
            boot = command("SD_PAYLOAD SECTOR 0 0 16")
            assert boot["stream_valid"] and boot["stream"][:3] == [0xEB, 0x3C, 0x90], boot
            assert boot["fs"]["role"] == "BOOT", boot
            assert command("EMU_STATUS")["tv_sync_pll"] == 0
            assert command("EMU_STATUS")["video_dump_omit_messages"] == 1
            command("EMU_SET video_dump_omit_messages 0")
            assert command("EMU_STATUS")["video_dump_omit_messages"] == 0
            assert command("EMU_STATUS")["tv_sync_recovery"] == 100
            command("EMU_SET tv_sync_recovery 0")
            assert command("EMU_STATUS")["tv_sync_recovery"] == 25
            command("EMU_SET tv_sync_recovery 9999")
            assert command("EMU_STATUS")["tv_sync_recovery"] == 800
            assert command("EMU_STATUS")["tv_sync_strength"] == 100
            command("EMU_SET tv_sync_strength 999")
            assert command("EMU_STATUS")["tv_sync_strength"] == 100
            command("EMU_SET tv_sync_strength 35")
            command("EMU_SET tv_sync_recovery 400")
            command("EMU_SET tv_sync_pll 1")
            command("CONFIG SAVE")
            command("EMU_SET video_dump_omit_messages 1")
            assert "TvSyncPll=1" in (directory / "config.cfg").read_text()
            command("EMU_SET tv_sync_pll 0")
            command("EMU_SET tv_sync_strength 0")
            assert command("EMU_STATUS")["tv_sync_strength"] == 0
            command("EMU_SET tv_sync_recovery 100")
            command("CONFIG RELOAD")
            assert command("EMU_STATUS")["video_dump_omit_messages"] == 0
            command("EMU_SET video_dump_omit_messages 1")
            assert command("EMU_STATUS")["tv_sync_pll"] == 1
            assert command("EMU_STATUS")["tv_sync_recovery"] == 400
            assert command("EMU_STATUS")["tv_sync_strength"] == 35
            command("EMU_SET tv_sync_pll 0")
            state = command("GET_STATE")
            assert state["paused"]
            time.sleep(.15)
            assert command("GET_STATE")["frame"] == state["frame"], "paused execution advanced"
            start = time.monotonic()
            command("RUN_FRAMES 12")
            finished = client.wait_run_complete()
            assert .1 < time.monotonic() - start < 5, "default headless pacing ignored"
            assert finished["frame"] == state["frame"] + 12
            info = command("SCREENSHOT_CAPTURE")
            assert info["width"] > 0 and info["height"] > 0 and info["size"] > 54
            screenshot = command("SCREENSHOT_READ 0 54")
            assert info["format"] == "png" and screenshot["values"][:8] == [137,80,78,71,13,10,26,10], "in-memory PNG inaccessible"
            assert command("READ_REGS")
            before = command("AUDIO_SCOPE SOURCE 262")["samples"]
            time.sleep(.1)
            assert command("AUDIO_SCOPE SOURCE 262")["samples"] == before, "paused guest samples overwritten"
            assert command("AUDIO_SCOPE STATUS")["output_frames_total"] == 0
            command("AUDIO_SCOPE ENABLE")
            command("RUN_FRAMES 4")
            client.wait_run_complete()
            total = command("AUDIO_SCOPE STATUS")["output_frames_total"]
            assert total > 0
            assert command("AUDIO_SCOPE OUTPUT 128")["count"] == 128
            time.sleep(.1)
            assert command("AUDIO_SCOPE STATUS")["output_frames_total"] == total, "paused output kept running"
            command("AUDIO_SCOPE DISABLE")
            assert command("AUDIO_SCOPE STATUS")["output_frames_total"] == 0
            command("QUIT")
            process.wait(timeout=10)
            assert process.returncode == 0
            after = (directory / "config.cfg").read_text(encoding="utf-8")
            # The legacy ESP loader may append its missing defaults at startup.
            # CLI overrides must preserve the existing preference values.
            values = dict(line.split("=", 1) for line in after.splitlines() if "=" in line and not line.startswith("#"))
            for line in config.splitlines():
                key, value = line.split("=", 1)
                assert values[key] == value, "CLI changed saved preference: " + key
        finally:
            client.close()
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
    print("Headless API integration: PASS")


if __name__ == "__main__":
    main()
