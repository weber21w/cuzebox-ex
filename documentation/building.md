# Building

This page is a starter guide for build-time options and local development workflow.

## General build notes

The project uses make-based builds with feature flags defined in `Make_config.mk` and `Make_defines.mk`.

The intent is:

- keep major optional features behind build flags
- keep defaults convenient for development
- make it possible to trim release builds later

## Notable build flags

These are the most relevant feature flags to document right now.

### Debugger

```make
FLAG_DEBUGGER=1
```

Current intent:

- default on for development
- `FLAG_DEBUGGER=0` removes debugger memory/timing hot-path hooks from the compiled AVR core
- should be possible to disable cleanly for lean builds

### Memory history

```make
FLAG_MEMORY_TRACE=$(FLAG_DEBUGGER)
```

This controls the historical SRAM/I/O trace independently of the rest of the
debugger. Set it to `0` to keep breakpoints/watchpoints and the debugger UI but
omit the history ring and its event writer. The flag has no effect when
`FLAG_DEBUGGER=0`.

### Beam diagnostics

```make
FLAG_BEAM_CAPTURE=$(FLAG_DEBUGGER)
FLAG_BEAM_HISTORY=$(FLAG_DEBUGGER)
```

`FLAG_BEAM_CAPTURE=0` removes the per-cycle PORTC capture buffer and its hot-path
capture branch while retaining the debugger/API live raster position. The live
position is derived from the existing absolute AVR cycle and a line-start
timestamp, so it does not need a continuously incremented debugger counter.

`FLAG_BEAM_HISTORY=0` removes the 4096-entry instruction-to-raster history ring.
Raster breakpoints and scanline profiling remain available. Both flags are
ignored when `FLAG_DEBUGGER=0`.

### SD protocol trace

```make
FLAG_SD_TRACE=$(FLAG_DEBUGGER)
```

This controls the optional 256-entry SD transaction history and SD-specific
break triggers. With the feature built but history/triggers disarmed, the SD
byte path pays one normally-not-taken shared gate. `FLAG_SD_TRACE=0` removes
that gate, the history ring, and the trigger state entirely while preserving
`SD_STATUS` and the runtime timing-model page. The AVR instruction loop does
not gain an SD-specific branch: SD triggers reuse the debugger's existing
post-instruction event-stop gate.

### SD fault injection

```make
FLAG_SD_FAULT=$(FLAG_DEBUGGER)
```

This controls deterministic SD fault injection independently of protocol-history
capture. Eight runtime rule slots can delay R1 or data-token delivery, extend
write busy time, inject R1 error bits, force command/data CRC errors, or reject a
matched command. Rules can be one-shot or persistent and can filter by CMD and
sector. With the feature built but no rule armed, checks occur only at the
protocol phases that can actually be faulted; there is no AVR-instruction or
AVR-cycle fault branch. `FLAG_SD_FAULT=0` compiles the rules and all injection
checks out. The flag has no effect when `FLAG_DEBUGGER=0`.

Filesystem-aware SD breakpoints remain part of `FLAG_SD_TRACE`; VFAT ownership
is resolved only at relevant command/sector boundaries when a filesystem break
criterion is armed.

### Deterministic SD record/replay

```make
FLAG_SD_REPLAY=$(FLAG_DEBUGGER)
```

This controls the optional card-visible SD interaction recorder/replayer. Capture
is explicitly armed and storage is allocated dynamically only when recording or
loading an `.sdr` file. Each SPI transfer is represented by separate `BYTE_START`
(MISO sample) and `BYTE_END` (MOSI delivery) events, so a CS transition occurring
while a byte is in flight retains its exact ordering. CS changes and SD resets are
recorded as independent events with relative CPU-cycle timestamps.

Replay restores the captured initial SD state at a new absolute cycle, validates
the AVR's MOSI/CS/reset sequence and relative timing, and returns the captured
MISO stream without touching VFAT or the normal SD state machine. A mismatch
requests the debugger's existing event stop. `FLAG_SD_REPLAY=0` removes the event
storage and all receive/send replay hooks entirely. When built but idle, no event
buffer is allocated.

### API server

```make
FLAG_API_SERVER=1
```

Current intent:

- default on for development right now
- expected to default off before release

### Remote ROMs

```make
FLAG_REMOTE_ROMS=1
FLAG_REMOTE_ROMS_LIBCURL=1
```

Current behavior:

- `FLAG_REMOTE_ROMS=1` keeps the feature compiled in
- `FLAG_REMOTE_ROMS_LIBCURL=1` asks the build to use libcurl when development files are available
- if libcurl headers and link flags are not found, the build now falls back automatically to the older shell `curl` helper path instead of failing at compile time

This is especially helpful on Windows/MinGW setups where `curl/curl.h` may not be installed even though the standalone `curl` command exists.

If you want to force the fallback path explicitly, set:

```make
FLAG_REMOTE_ROMS_LIBCURL=0
```

## Config defaults for development

A few runtime config defaults are development-oriented for now and may change before release.

Examples:

- `ApiServer=1` for current development convenience
- expected to become `0` by default before release

## Documentation-related build idea

If you want nice local docs rendering, the included `mkdocs.yml` can be used independently of the emulator build.

Example:

```bash
pip install mkdocs-material
mkdocs serve
```

## TODO

This page should later document:

- platform-specific package requirements
- SDL-related dependencies
- Windows notes
- Linux notes
- Emscripten/Web notes
- example release build commands
