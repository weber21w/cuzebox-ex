# Uzenet portable firmware core

This directory contains the portable UART v4 firmware core copied from
`C:\uzenet-master\custom-firmware`. The source checkout did not provide a Git
revision; `snapshot.json` records the synchronization date and each included
source/header's SHA-256 hash after CRLF-to-LF normalization (`sha256-lf`).
`tools/test_uzenet.py` verifies complete source coverage and those hashes before testing.
The upstream project license is included as `LICENSE`.

The portable sources are unchanged. Emulator platform adaptation lives in
`cu_esp_uzenet.c` and `cu_esp_uzenet_net.inc`, outside this directory. The latter
adapts the firmware's `firmware/un_net.inc`; it substitutes host sockets,
OpenSSL and file-backed profile storage for ESP8266 platform APIs.

Update the portable sources and manifest together, then rerun the adapter and
AVR integration tests. This is a pinned protocol implementation, not a build-time
dependency on a developer's local firmware directory.
