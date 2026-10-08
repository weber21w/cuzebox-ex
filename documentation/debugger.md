# Debugger

The debugger is intended to be a practical development tool, not just a stop-and-peek window.

This page summarizes the current feature direction.

## Build and runtime direction

The debugger should remain a build option, with development defaults favoring convenience and release builds able to trim it later.

Key goal:

- negligible burden when disabled or inactive
- richer features only when explicitly armed or opened

## Disassembly annotations

The disassembly view can now include value-oriented comments for many instructions.

Examples:

```asm
LDI r18,04      ; r18=04
LD r16,Y+       ; r16=[0152]=20, Y->0153
OUT UCSR0B,r16  ; UCSR0B=18
BRBC 1,target   ; taken -> target
```

These annotations are intended to make stepping and inspection faster by showing live consequences of instructions directly in the code view.

## Symbol support

The debugger now supports multiple symbol sources:

- manual text symbol files
- imported `.elf`
- imported `.map`
- cached imported symbols
- manual label overrides

The practical label precedence is:

1. manual label override
2. imported/cached symbol
3. built-in I/O name
4. raw address

## Watches and watchpoints

### Watches

Use watches to keep important SRAM or I/O values visible while stepping or paused.

### Watchpoints

Use watchpoints to break on selected reads or writes.

Current design goal:

- cheap early-out when no watchpoints are armed
- explicit monitoring only where it is needed

## Per-ROM debugger profiles

Debugger state is now intended to persist per ROM under a directory shaped like:

```text
debugger/<sanitized-rom-name>_<crc32>/
```

Typical contents include:

- debugger config
- symbol cache
- manual labels

This keeps project-specific debugging work attached to the ROM instead of treating each session as disposable.

## Navigation controls

Useful run-control features now include:

- step instruction
- step frame
- step over
- step out
- run to cursor / run to target

These are important because symbols and annotations are much more useful when navigation is fast.

## Beam-aware timing debugger

The web debugger can inspect the current emulated raster position without
arming a history capture. Optional PORTC capture and instruction-to-beam history
are independently armed and independently removable with `FLAG_BEAM_CAPTURE=0`
and `FLAG_BEAM_HISTORY=0`. Raster breakpoints can target a row/cycle or an actual
SYNC rise/fall, which is useful when diagnosing custom video modes whose sync is
itself wrong. The scanline profiler and instruction map share the same timing
gate so inactive diagnostics stay out of the instruction handler.

## SD protocol debugger

The debugger can inspect SD state while paused or after every single AVR
instruction. `FLAG_SD_TRACE` adds an independently armed 256-event protocol
history plus targeted break triggers for command numbers, sectors, initialization
failures, CRC rejects, and response-latency thresholds. Armed history groups
related command/data phases by transaction ID and records exact cycle, PC and
raster coordinates for each of the six command bytes plus data-token, payload-end,
CRC-end, multiblock-sector and busy-release boundaries. Selecting a historical
phase in the web debugger pins that row/cycle on the beam view and targets its
AVR PC in disassembly. Payload bytes themselves are not individually recorded.

History and triggers share one `sd_debug_active` check at the SD-byte boundary;
break requests feed the existing debugger event-stop flag, so SD tracing does
not add a second post-instruction stop test. Set `FLAG_SD_TRACE=0` to remove the
SD trace gate, history ring, and trigger machinery while retaining ordinary SD
status/timing inspection.

Filesystem-aware breakpoints can stop on reads or writes to FAT metadata, the
root directory, subdirectories, regular-file data, or a case-insensitive owner
path substring. VFAT ownership is looked up only when one of those criteria is
armed and only at the relevant SD command/sector boundary.

`FLAG_SD_FAULT` adds eight deterministic fault-rule slots independently of the
trace/history ring. A rule can delay R1, delay a read token, extend write busy,
inject R1 bits, force command CRC or data CRC failure, or reject a matching
command. Rules can be one-shot or persistent and can filter by CMD and sector.
Faults are evaluated only in the SD protocol phase that can consume that fault;
there is no new per-instruction stop test. Fault injections are annotated in SD
history when history is armed, but history is not required for a fault rule to
operate. Set `FLAG_SD_FAULT=0` to remove the rule table and injection checks.

`FLAG_SD_REPLAY` adds an explicitly armed deterministic card-visible recorder and
replayer. It records SPI transfer start and end as separate events, along with CS
transitions and SD resets, preserving the exact order even when CS changes during
an in-flight byte. Replay rebases relative event timing to the current CPU cycle,
substitutes captured MISO, verifies MOSI/CS/reset behavior, and bypasses VFAT. A
mismatch requests the same debugger event-stop mechanism used by other debugger
triggers. Capture storage is allocated only when record/load is used; setting
`FLAG_SD_REPLAY=0` removes the replay storage and SD receive/send hooks entirely.

## Good next additions

Likely future debugger work:

- call stack view
- interrupt viewer
- changed-register highlighting
- persistent window/layout state
- richer symbol browser
- API-controlled breakpoints/watchpoints

## TODO

Add screenshots and exact UI walkthroughs for:

- annotation view
- symbols section
- watches section
- watchpoints section
- profile section


## Native audio DAC tracing

`FLAG_AUDIO_TRACE` controls the optional OCR2A history and rail/threshold break triggers. It defaults to `FLAG_DEBUGGER`; setting it to 0 removes the DAC event gate, history ring, and break logic entirely. Events correlate final 8-bit Uzebox PWM samples with AVR PC/cycle and raster position.
