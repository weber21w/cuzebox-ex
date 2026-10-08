# API overview

The automation API is meant to support:

- automated testing
- scripting
- tooling integration
- debugger-assisted workflows

The central requirement is that it should have minimal burden when turned off.

## Design goals

- build-time option
- runtime enable/disable
- centralized implementation
- no per-instruction API burden
- frame-oriented control model
- localhost-only by default

## Current development defaults

For development convenience, the current direction is:

- API server build flag default on
- `ApiServer=1` in config for now

Before release, the plan is to make runtime default off.

## Transport style

The current API direction uses a simple localhost TCP command server.

This keeps the entry cost low for:

- shell scripts
- Python tools
- custom harnesses
- local developer tools

## Main use cases

### ROM inspection

Query loaded ROM information such as title, identity, and CRC.

### Debug state queries

Read memory, registers, and symbol-backed values to determine whether an automated test passed.

### Input automation

Inject controller state and hold it for a specified number of frames.

### Deterministic frame control

Load a ROM in a waiting state, then begin timing explicitly with a run command.

That is why commands such as `LOAD_ROM WAIT ...` matter.

## Good testing model

A strong first testing workflow looks like:

1. launch emulator with API enabled
2. load ROM and wait
3. start execution intentionally
4. queue input sequences
5. wait for frame or memory conditions
6. read symbols or memory
7. report pass/fail

For more detail, see [Automated Testing](testing.md).

## Web debugger

The browser debugger is an API client, not a second control interface.  See
[Web debugger](../web-debugger.md).  New browser-facing emulator functionality
should be exposed through the API first so scripts and other external tools get
the same capability.

## Web-tools diagnostic commands

The browser migration adds API operations that are also available to external
clients:

- `WRITE_REG <R0..R31> <VALUE>` — edit a general AVR register while paused
- `WRITE_MEM <REGION> <ADDR> <VALUE> [VALUE...]` — edit writable memory; direct Flash writes are rejected
- `DEBUG_PROFILE STATUS|SAVE|RELOAD` — inspect/persist ROM-specific debugger state
- `SERIAL_STATUS` — serial route, UART/backend counters, TCP diagnostics and trace status
- `SERIAL_SET <SETTING> <VALUE>` — serial/ESP/TCP/MIDI backend configuration
- `SERIAL_TRACE STATUS|ON|OFF|CLEAR|READ|EXPORT` — live serial trace access
- `TCP_DIAG STATUS|RESET|FLUSH` — TCP-serial diagnostic counters/log handling
- `ESP_STATUS` — ESP core, Wi-Fi, server and link/socket state
- `NETPLAY_STATUS` — transport, latency and rollback/runtime state

The serial and network commands are present when their corresponding emulator
features are compiled. `HELP` advertises the complete development command set.
