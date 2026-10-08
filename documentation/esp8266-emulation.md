# ESP8266 Emulation

The ESP8266 emulation layer is one of the most important parts of CUzeBox for networked Uzebox development.

It serves two jobs at once:

- emulate an ESP8266-like UART-attached module closely enough for Uzebox software that speaks ESP-AT
- provide practical development hooks so emulator users can inspect, route, and debug serial/network behavior without guessing blindly

This page documents the current model, the major code paths, the intended workflows, and the limits that matter during development.

## Scope

In CUzeBox, "ESP8266 emulation" means the virtual module available through the `ESP MODULE` serial route.

It does **not** mean every serial backend in the emulator. The other routes:

- `HOST SERIAL`
- `TCP SERIAL`
- `HOST MIDI`
- `VIRTUAL MIDI`
- `LOOPBACK`

are serial/UART endpoints, but they are not the ESP-AT emulation itself.

That distinction matters when debugging. A UART problem, a TCP serial problem, and an ESP-AT command problem are different failure classes.

## Design goals

The ESP layer tries to balance three things:

- good enough compatibility for Uzebox software that expects an ESP-AT style module
- clear development visibility, especially around UART transactions and module state
- bounded host-side cost so normal emulation stays practical on weaker machines

This is why the implementation is split into a few major layers instead of being one monolithic file.

## Main implementation files

The current ESP-related code is mainly organized like this:

- `cu_esp_at.c`
  - AT parser
  - module boot/reset behavior
  - command handlers
  - socket-facing send/receive orchestration
  - `+IPD` generation and module-side state transitions
- `cu_esp_ap.c`
  - SoftAP helpers
  - station/AP IP configuration helpers
  - virtual LAN overlay for emulator-to-emulator discovery and relay behavior
- `cu_esp_net.c`
  - lower-level socket work
  - host serial/TCP serial endpoint service
  - network helper functions shared by the ESP-facing logic
- `cu_uart.c`
  - AVR-facing UART model
  - UART timing/profiles
  - route dispatch
  - serial trace support
- `cu_esp.h`
  - shared ESP state, constants, route enums, and helper declarations

That split is intentional.

- `cu_uart.c` answers: "what bytes does the AVR side actually see?"
- `cu_esp_at.c` answers: "how does the virtual module behave once it receives those bytes?"
- `cu_esp_ap.c` answers: "what Wi-Fi/AP/LAN-overlay state exists around the AT layer?"

## High-level model

At a high level, the data path looks like this:

1. the AVR/Uzebox code talks to a UART
2. the UART layer applies the selected UART profile and route rules
3. when the route is `ESP MODULE`, bytes go into the ESP command/runtime layer
4. the ESP layer interprets AT commands, manages link state, and produces responses
5. responses come back through the same UART model toward the AVR

That means the AVR does not talk directly to sockets. It talks to a UART-attached peripheral model, and that model may open sockets or synthesize ESP-AT responses internally.

## UART interaction

The ESP module lives behind the same UART model used by the other serial endpoints.

That is useful because it means the emulator can expose and debug problems at the level the AVR actually experiences:

- baud mismatch
- framing mismatch
- disabled TX/RX
- delayed availability
- ready flags
- staged UDR + shift-register TX behavior in every profile

Current UART profiles are:

- `FAST`
- `BALANCED`
- `DEBUG`

`FAST` is the normal/default ESP profile and still preserves AVR-visible UART buffering and frame timing. `BALANCED` and `DEBUG` add progressively more frequent host-side servicing/diagnostics; `DEBUG` is intended for maximum observability rather than speed.

See also [Serial and Endpoints](serial.md).

## Boot and reset behavior

The ESP model has its own startup/reset timing and emits module-style boot text and readiness transitions.

Important points:

- reset behavior is not treated as a raw socket reconnect
- the module has internal state that must be reinitialized on reset
- some network/AP/LAN-overlay paths are intentionally reset in coordination with module state so the emulator does not leave stale state behind

This matters when debugging because a bad session may be caused by:

- wrong UART configuration during early boot
- a legitimate AT parsing problem
- stale socket/link state that should have been reset
- a difference between a "module reset" and a "route/backend reconnect"

## AT command handling

The AT parser is implemented in `cu_esp_at.c` and covers a fairly broad set of command families.

The exact set evolves over time, but current code includes handlers for major areas such as:

### Basic module/system commands

Examples include:

- `AT`
- `ATE`
- `AT+RST`
- `AT+GMR`
- `AT+CIOBAUD`
- `AT+UART`
- `AT+IPR`
- selected `SYS*` commands

### Wi-Fi mode and join/scan commands

Examples include:

- `AT+CWMODE`
- `AT+CWJAP`
- `AT+CWQAP`
- `AT+CWLAP`
- `AT+CWLAPOPT`
- `AT+CWSAP`
- `AT+CWAUTOCONN`
- `AT+CWHOSTNAME`

### IP and network identity commands

Examples include:

- `AT+CIFSR`
- `AT+CIPSTA`
- `AT+CIPAP`
- `AT+CIPSTAMAC`
- `AT+CIPAPMAC`
- `AT+CIPDNS`
- `AT+CIPDOMAIN`
- `AT+PING`

### Connection management commands

Examples include:

- `AT+CIPSTART`
- `AT+CIPSEND`
- `AT+CIPCLOSE`
- `AT+CIPSTATUS`
- `AT+CIPMUX`
- `AT+CIPMODE`
- `AT+CIPSERVER`
- `AT+CIPSERVERMAXCONN`
- `AT+CIPSTO`
- `AT+CIPDINFO`

### Buffered receive mode

Examples include:

- `AT+CIPBUFRECVMODE`
- `AT+CIPBUFRECVLEN`
- `AT+CIPBUFRECVDATA`
- compatibility wrappers for `CIPRECV*`

### SNTP, SSL, and advanced networking

The current tree also includes handlers around:

- SNTP configuration/time queries
- selected SSL configuration commands
- mDNS
- DHCP control
- power/RF reporting and related commands

These areas are useful, but they should still be treated as implementation-defined unless you have tested the specific workflow you care about.

## User input modes

The ESP state tracks multiple user-input or parser modes, including:

- AT command mode
- send/payload mode
- passthrough-like modes
- unvarnished/raw-like handling modes

That matters because not every byte the AVR sends is interpreted as a fresh command line.

For example:

- `AT+CIPSEND` changes expectations about what the next bytes mean
- transparent/passthrough behavior changes whether bytes are packetized as AT traffic or forwarded as data

When debugging "the ESP stopped answering," always check whether the virtual module is still in the mode you think it is in.

## Socket and link model

The ESP layer tracks virtual links, protocols, and connection state separately from the AVR UART.

Important pieces include:

- single versus multi-connection mode (`CIPMUX`)
- protocol selection such as TCP, UDP, and SSL-flavored links
- link IDs
- per-link buffered receive state
- `+IPD` formatting and timing
- server/listener behavior

In other words, there are really two distinct protocols in play:

- UART between the AVR and the virtual module
- network sockets between the virtual module and the host/network

A game can have correct UART traffic and still fail because link state or socket policy is wrong.

## `+IPD` and receive behavior

Incoming network data is not delivered directly into AVR memory. It is converted into ESP-style receive indications, usually through the `+IPD` path.

The important implications are:

- AVR software sees receive data in ESP-AT terms, not as direct socket reads
- `CIPDINFO`, buffering mode, and link state change what the data looks like
- `+IPD` generation is budgeted so one busy connection does not monopolize the emulator

That budgeted design is deliberate. It helps keep host spikes under control.

## SoftAP and virtual LAN overlay

One of the more development-focused parts of the current ESP work is the virtual LAN overlay in `cu_esp_ap.c`.

This layer exists so emulator instances can behave more like discoverable/joinable ESP SoftAP peers without requiring real Wi-Fi hardware.

Current code includes support around:

- SoftAP advertisement
- station join flow
- DHCP-style lease handling
- emulator-to-emulator discovery via multicast
- relay/open/data/close control messages
- station/AP IP and MAC reporting

This is not a generic full Wi-Fi implementation. It is a targeted emulator-side overlay intended to support practical multiplayer and ESP-adjacent workflows.

## ESP Monitor

The **ESP Monitor** window is the first place to look when serial output alone is not enough.

It is intended to answer questions like:

- which serial route is active?
- which UART profile is active?
- what Wi-Fi mode is selected?
- is the module acting as station, SoftAP, or both?
- what is the current TCP serial state?
- which links are open?
- what host/port/protocol does each link think it has?
- what station/AP IP settings are active?

This is especially helpful for distinguishing:

- UART issue
- AT issue
- socket issue
- AP/LAN-overlay issue

## Serial Trace and ESP debugging

For ESP work, **Serial Trace** and **ESP Monitor** complement each other.

Use them together.

### Serial Trace tells you

- what the AVR actually transmitted and received
- whether bytes were scrambled because UART settings were wrong
- whether the timing looked transactional, such as `AT\r\n` followed by `OK\r\n`
- whether `BAD-BAUD`, `BAD-FMT`, `TX-OFF`, `RX-OFF`, or `SCRAMBLE` flags are involved

### ESP Monitor tells you

- what the virtual module thinks its internal state is
- whether joins, links, DNS, AP state, or TCP serial mode are the issue

A good rule is:

- if Serial Trace already looks wrong, fix UART first
- if Serial Trace looks sane but the module still behaves wrong, inspect ESP Monitor next

## Typical development workflows

### Bringing up ESP-AT code in a ROM

A practical sequence is:

1. select `ESP MODULE` as the serial route
2. start with UART profile `BALANCED`
3. open **Serial Trace**
4. reset the ROM and watch the initial transaction
5. verify simple commands such as `AT` and `AT+GMR`
6. move on to join, IP, and socket commands
7. open **ESP Monitor** if the AT dialogue looks sane but state seems wrong

### Distinguishing UART bugs from ESP bugs

If you suspect the ROM is not even talking to the module correctly:

1. use `LOOPBACK` or another known-good serial route to validate basic UART behavior
2. switch back to `ESP MODULE`
3. compare the traces

If the UART path is bad, fixing the ESP layer will not help.

### Comparing direct serial bridges with ESP emulation

If a project is meant to talk to an external bridge such as UzeSynthBridge, use `TCP SERIAL` or another direct route.

If a project is meant to talk to an ESP8266-style AT module, use `ESP MODULE`.

Those are different workflows and should not be conflated.

## Accuracy notes and limits

The ESP layer is meant to be useful and debuggable, not a cycle-perfect clone of a physical ESP8266.

Important limits to keep in mind:

- the AT firmware personality is emulator-defined, not guaranteed to match any one real firmware build exactly
- command coverage is broad but not guaranteed complete
- timing is shaped to be practical, not to reproduce every undocumented edge case of real firmware
- UART behavior is profile-based and intentionally trades strictness versus overhead
- already-consumed host audio or OS-managed network timing cannot be perfectly "rolled back" in the way pure game state can

That last point matters if your ROM combines ESP/network activity with rollback-sensitive gameplay systems.

## Troubleshooting checklist

When ESP behavior looks wrong, check in this order:

1. **Is the route actually `ESP MODULE`?**
2. **Does Serial Trace show sane command/response traffic?**
3. **Is the UART profile too strict or too loose for what you are testing?**
4. **Is the module in the mode you think it is in?**
   - AT mode
   - send mode
   - passthrough mode
5. **Does ESP Monitor show the expected Wi-Fi/link/IP state?**
6. **Are you testing a supported AT command path, or an unverified one?**
7. **Is the problem really a LAN-overlay or socket issue rather than AT parsing?**

## Recommended future expansion for this document

The most useful follow-up additions would be:

- screenshots of ESP Monitor and Serial Trace during a healthy ESP session
- a tested table of command families with notes on known-good workflows
- a small cookbook for:
  - `AT`/`OK`
  - join AP
  - query IP
  - open TCP connection
  - send and receive data through `+IPD`
- notes on how the LAN overlay should be used during multiplayer development

## Firmware compatibility personalities

CUzeBox can expose two ESP8266 AT-command personalities from **Devices -> ESP Firmware**:

- `NONOS AT 1.7 LEGACY` preserves compatibility with software written against the older CUzeBox/NONOS behavior.
- `ESP-AT 2.3.0.0` is the default for new configurations and targets the current Espressif ESP8266 ESP-AT command surface.

The 2.3 personality is not just a different `AT+GMR` string. It enables the modern command syntax and state model, including five simultaneous link IDs (`0` through `4`), passive receive commands, extended Wi-Fi state, modern SSL controls, IPv6-aware TCP/UDP operation, persistent transparent links, and the stock ESP8266 MQTT command family.

### TLS

CUzeBox's ESP-AT TLS transport is backed by the host OpenSSL implementation. The 2.3 AT layer exposes ESP-style SNI, Common Name verification, ALPN, PSK configuration, and `CIPSSLCCIPHER`; ESP/mbedTLS cipher IDs are translated to OpenSSL selections rather than exposing host-specific cipher names over the emulated UART.

### MQTT

The ESP8266 2.3 personality includes a native MQTT 3.1.1 client for the stock ESP8266 transports:

- scheme `1`: MQTT over TCP
- scheme `6`: MQTT over WebSocket

MQTT has its own host connection and therefore does not consume one of the five normal `CIP` link IDs. Long credential input and `MQTTPUBRAW` use binary/raw UART input just as the module does. Automatic reconnect also restores subscriptions.

### IPv6

When `AT+CIPV6=1` is enabled, TCP and UDP links can use IPv6 addresses. IPv6 peer addresses are reflected through the normal status and receive-information paths, and `CIPDOMAIN` can request IPv6 resolution.

### Savestate compatibility

The five-link ESP state is larger than the historical four-link raw state structure. New savestates preserve all five links normally. When CUzeBox loads an older savestate containing the historical 132,512-byte ESP block, the rest of the machine state is restored while incompatible volatile ESP socket/UART state is reset instead of misinterpreting the old structure.

## ESP-AT regression tests

Focused compatibility tests live in `tests/esp_at/` and can be run with:

```bash
tests/esp_at/run.sh
```

They cover AT 2.3 command formatting/state, a real fifth localhost socket, IPv6 localhost/DNS operation, MQTT over TCP and WebSocket, MQTT reconnect/resubscribe, and legacy ESP savestate migration. The test runner accepts additional compiler flags through `EXTRA_CFLAGS`, which is useful for sanitizer runs.

## AVR UART timing

The ESP8266 personality is attached behind the normal ATmega644 USART model.
The default FAST UART profile does **not** bypass the UDR holding register or
character timing: AVR-to-ESP bytes are committed to the ESP parser at the end
of their UART frame, and ESP-to-AVR bytes become visible through `RXC0` after a
receive frame. Host/network maintenance is rate-limited separately so frequent
`UCSR0A` polling does not translate into equally frequent socket/timer work.

### ESP UART/AT hot-path optimizations

The FAST profile keeps UART timing and host networking on separate schedules.
At 9600 baud, the lightweight UART deadline cache is refreshed at about one
quarter of a character, while heavier ESP/network maintenance runs at about one
character (bounded between 8,192 and 32,768 AVR cycles). A known RX/TX deadline
always overrides those caches, so reduced host polling cannot make an AVR UART
flag late.

Additional hot-path rules are:

- `cu_esp_uzebox_write_ready()` advances only the TX shifter and computes
  `UDRE0/TXC0`; it does not poll RX sockets, DNS, MQTT, or the AT receive queue.
- UART bit/frame timing is cached from UBRR/U2X/frame-format state instead of
  being recomputed on every status read.
- Desktop ESP links use one zero-timeout `select()` readiness query for all five
  `CIP` sockets; `recv()` is called only on links reported readable. This also
  preserves EOF detection because a closed TCP socket is readable to `select()`.
- MQTT is skipped completely by the network tick while it is disconnected and
  has no reconnect work pending.
- Normal builds do not print each AT command or every UART-register
  reconfiguration. Those verbose traces are compile-time opt-ins.
- Integer AT response fields use a small direct decimal formatter rather than
  `snprintf()`.
- AT command names are scanned once and matched exactly through a sorted lookup
  table. Legacy `_CUR` / `_DEF` spellings are retained, while accidental prefix
  matches such as an unknown command beginning with a valid command name are
  rejected.
