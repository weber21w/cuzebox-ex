# Serial and Endpoints

This page covers the serial/UART development features that are especially relevant for device bring-up, emulator integration, and debugging.

## Goals

The serial stack is meant to support two different use cases at the same time:

- low-overhead normal emulation
- higher-visibility development and troubleshooting

That is why the design separates:

- UART behavior and timing
- serial endpoint backends
- optional trace and monitor tools

## UART profiles

The current UART emulation profile names are:

- `FAST`
- `BALANCED`
- `DEBUG`

### FAST

Use this when minimal host overhead matters most.

### BALANCED

Use this for development when you want better timing and more realistic behavior without going all the way to the heaviest debug path.

### DEBUG

Use this when you want the richest UART-oriented diagnostics.

## Endpoint types

The project now supports multiple serial-style backends.

### ESP Module

Virtual ESP8266-oriented serial behavior and AT command handling.

For the deeper module-side documentation, command families, SoftAP/LAN-overlay behavior, and ESP-specific debugging flow, see [ESP8266 Emulation](esp8266-emulation.md).

### Host Serial

Routes bytes to a host serial device.

### TCP Serial

A raw full-duplex TCP byte stream treated like a serial line.

Supported modes:

- `CLIENT`
- `SERVER`
- `AUTO` (default for new configurations)

Client mode is useful for tools such as bridges that already listen on a TCP port.
Server mode is useful when you want the emulator itself to accept a connection.
Auto mode is meant for linking two CUzeBox instances: it first tries to connect
to host:port, and if nobody is listening there it listens on port itself. Both
instances can therefore use the same settings and the roles sort themselves out.
If the peer goes away, auto mode keeps negotiating until it comes back. For a
remote host where both sides might start listening at the same moment, a short
randomized listen period makes them fall back to connecting until one succeeds.

The Serial backend window provides explicit connection controls:

- `CONNECT` or `RECONNECT` in client mode
- `START LISTENING` or `RESTART SERVER` in server mode
- `START AUTO` or `RESTART AUTO` in auto mode
- `DISCONNECT` in any mode

Selecting `TCP SERIAL` activates the saved endpoint immediately for compatibility
with older configurations. The explicit action button applies any edited host,
port, mode, and reconnect settings and restarts the endpoint.

### Two CUzeBox instances on one machine

Simplest: in **both** instances select `TCP SERIAL`, open `BACKEND DETAILS`, and
press `LOCAL AUTO` (127.0.0.1, port `12001`). The status line shows
`AUTO (SERVER)` in one window and `AUTO (CLIENT)` in the other, and both report
`CONNECTED`.

Explicit roles still work:

1. In the first instance press `LOCAL SERVER`. The status should become `LISTENING`.
2. In the second instance press `LOCAL CLIENT`.
3. Both instances should report `CONNECTED` before starting the link-cable game.

Note that both instances started from the same folder share `config.cfg`; the
TCP mode saved by whichever instance wrote it last is what the next launch uses.
`AUTO` avoids having to re-pick roles because of that.

On Windows the TCP Serial listener no longer sets `SO_REUSEADDR`. With it, a second
instance could bind the same port as the first and silently take half of the
incoming connections. The API and web-debugger servers follow the same rule and,
when their port is busy, a second instance moves to the next pair
(API 24682 / web 24683, then 24684 / 24685, ...). The console prints the ports used.

### Linking over the internet (no port forwarding)

Select `TCP SERIAL`, open `BACKEND DETAILS`, and press `INTERNET` in both
instances. Enter the **same room code** in both. `NEW CODE` makes a random
6-character code you can send to the other player. Then press `JOIN ROOM`.

* The first player to arrive creates the room on the uzenet relay
  (`uzenet.us`, UDP port 43810, the same relay netplay uses). The second player
  joins it. If both arrive at the same moment, one of them retries as a join.
* CUzeBox then tries a direct UDP hole punch to the other player. If it works,
  the status note reads `linked (direct)`. Otherwise the bytes keep flowing
  through the relay (`linked (via relay)`). Neither case needs a router change.
* Serial bytes travel over a small reliable, ordered stream with sequence
  numbers, acknowledgements and retransmission. Packet loss on the internet
  path shows up as delay, never as corrupted or missing bytes.
* If the other player leaves or times out, the room is kept and the link
  resumes when they rejoin with the same code.
* The emulators are not lock-stepped, on purpose. Two real Uzebox consoles have
  independent crystals, so link games already have to tolerate rate
  differences and latency. The link layer only moves bytes.

Status shows `WAITING FOR PLAYER` while you are alone in the room. The relay host
and port can be changed in the window (for a self-hosted relay). For local
testing there is a mock relay: `python3 tools/uzenet_relay_mock.py --port 43810`
(options `--no-direct`, `--loss 0.1` and `--delay-ms 80` simulate a bad path).

Config keys: `TcpSerialMode="3"`, `LinkRoom`, `LinkRelayHost`, `LinkRelayPort`.

### Finding another CUzeBox on the LAN

Press `LAN` and then `START SEARCH` in both instances. They broadcast a small
beacon on UDP port 12002 (broadcast plus multicast 239.255.85.76) and pair
automatically when exactly one other searching instance runs the same ROM. The
roles are decided from random ids; the normal TCP Serial transport then
connects them on the TCP Serial port. With several candidates, a list is shown
and you press `PAIR` on the one you want. Pairing commits only when both sides
name each other, so a third instance never steals a partner. If the partner
goes away, the instance goes back to searching.

On Windows, allow CUzeBox through the firewall when asked; otherwise LAN
beacons from other machines are blocked. Instances on the same PC still find
each other.

Config key: `TcpSerialMode="4"`.

### Bad connection simulator

This is a developer tool, so it is not in the in-emulator GUI. Use the
**Bad Connection Simulator** panel on the web Serial page (`/serial`) or the API.
It delays and damages bytes **received** by this instance before the emulated
UART sees them. It works with every TCP Serial mode. Byte order is always preserved,
as on a real cable.

| Setting | Effect |
|---|---|
| Latency ms / Jitter ms | Fixed delay plus a random 0..jitter extra per byte. Set it to the full round trip you want to simulate, or set half on each side. |
| Stall every ms / Stall ms | Roughly every N ms (randomized 0.5x to 1.5x), nothing arrives for M ms, then the backlog arrives at once, like a Wi-Fi hiccup. |
| Noise ppm | Per million bytes, flip one random bit (line noise). |
| Drop ppm | Per million bytes, lose the byte entirely. |

Counters for queued, corrupted, dropped and stalls are shown live. API:
`SERIAL_SET IMPAIR 1`, `LATENCY_MS`, `JITTER_MS`, `STALL_EVERY_MS`, `STALL_MS`,
`NOISE_PPM`, `DROP_PPM`. Config keys start with `LinkImpair...`.

### Link API summary

`SERIAL_SET TCP_MODE 0..4` (client, server, auto, internet, lan),
`SERIAL_SET LINK_ROOM <code>`, `RELAY_HOST <host>`, `RELAY_PORT <n>`,
`LAN_CHOOSE <id>`, `TCP_RESTART 1`. `SERIAL_STATUS` has a `link` object
(internet RTT, retransmits, direct/relay; LAN peers; simulator counters) and
`tcp_role`. TCP states 5 = searching LAN, 6 = waiting for the other player.

While emulation is paused (debugger, or the GUI with pause-while-open), the
link keeps being serviced, so pairing continues and a paused player does not
time out the other one.

### Link-cable timing

Raw serial routes still use the emulated AVR UART configuration. In the FAST
profile CUzeBox uses a coarse one-byte-at-a-time scheduler, but each byte now
occupies the complete configured UART frame rather than the raw UBRR divisor.
For a 9600-baud 8N1 link on the Uzebox clock this is 29760 emulated cycles per
byte. This is required by direct-UART games whose synchronization loops depend
on realistic transmit and receive readiness.

TCP client completion is also serviced directly by the TCP Serial route. It no
longer depends on the ESP AT-network timer, which is bypassed by raw serial
backends.

Polling-only UART software is also supported correctly. `TXC0` represents an
idle or completed transmitter, but it only enters the USART TX interrupt vector
when `TXCIE0` is enabled. This distinction is required by link games that poll
`UCSR0A` from a scanline service routine while leaving UART interrupts disabled.

The Serial window uses a wider 620-by-420 layout. TCP mode, connection status,
and the last socket error are displayed independently so `CONNECTED` and error
codes are not truncated.

### Loopback

A development-oriented route that loops AVR TX back to AVR RX through the UART model.

Useful for:

- self-tests
- UART timing validation
- separating endpoint problems from UART-model problems

### MIDI routes

MIDI-related serial endpoints are present for development and integration work, but Windows visibility may depend on which backend/API the host application actually sees.

## Serial Trace

Serial Trace is a RAM-backed UART-facing trace tool intended for development.

Design goals:

- default off
- cheap when disabled
- log what the AVR actually experiences
- preserve transactional visibility

Typical information shown:

- UART config changes
- TX bytes
- RX bytes
- route/status flags
- cooked transaction lines

## ESP Monitor

The ESP Monitor is a development tool window intended to expose the current ESP-side state without requiring guesswork from raw serial traffic alone.

Typical information shown:

- route and UART profile
- ESP state flags
- station / SoftAP state
- TCP serial state
- per-link information

## Performance notes

A few rules keep the serial system practical on weaker hosts:

- backend polling is cached and budgeted
- trace is default off
- endpoint work is bounded per service tick
- UART-facing improvements are profile-sensitive instead of always-on

## TODO

This page should later include:

- screenshots of Serial Trace and ESP Monitor
- exact route names as shown in UI
- detailed MIDI route notes
- example workflows with UzeSynthBridge

## HSYNC polling and TCP endpoints

The emulated AVR UART remains responsible for `RXC0`, `UDRE0`, `TXC0`, and
`UDR0` timing. Raw TCP is only a byte endpoint behind that UART. Host socket
service runs independently of AVR register polling, so a scanline renderer may
poll the USART from HSYNC without invoking host network calls. TCP bursts may
fill the endpoint queue, but the configured UART timing still controls when the
ATmega644 can observe each byte. The ESP8266 route keeps its established UART
and network path.

## Built-in ESP8266 UART hot path

The built-in ESP8266 route uses the ATmega644 USART's transmit shift register
and one-byte UDR holding register even when the UART profile is **FAST**. FAST
only reduces host-side polling/diagnostic work; it does not change AVR-visible
USART buffering.

For the ESP route specifically:

- a byte written to `UDR0` reaches the emulated ESP only after the configured
  UART frame has completed;
- `UDRE0` reopens as soon as the shift register can accept the UDR holding
  byte, so firmware can pipeline two bytes as on the ATmega644;
- `TXC0` is not visible until the final buffered frame completes;
- the first ESP-to-AVR response byte consumes one receive frame before `RXC0`
  becomes visible; and
- the FAST profile caches expensive ESP timer/network polling. At 9600-baud
  8N1 (`UBRR0=185` at 28.63636 MHz), the host-work cadence is about 7,440 AVR
  cycles instead of 2,048, while known UART-ready deadlines bypass the cache
  and remain cycle-exact. At 115200 baud the 2,048-cycle floor is retained.

This keeps HSYNC-polled firmware inexpensive on the host while preserving the
UART timing that the AVR program can observe. `tests/serial_link/esp_uart_hotpath_test.c`
locks down these properties.
