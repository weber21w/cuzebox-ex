# Automation API watchpoints

CUzeBox already had efficient debugger watchpoint support in the AVR core. This
API layer makes that support scriptable for automated debugging and regression
tests.

Watchpoints are compiled behind `ENABLE_DEBUGGER`. The SRAM/I/O hot path has one
shared `cu_debug_mem_active` gate used by both watchpoints and optional memory
history. When neither consumer is armed, no slot scan or history handler is
entered. With the debugger compiled out, the gate and event call are not present
in the memory-access path at all.

## Commands

```text
WATCH SET <SLOT> <SRAM|IO> <R|W|RW> <START> [END]
WATCH CLEAR <SLOT|ALL>
WATCH LIST
WATCH HIT [CLEAR]
```

`SLOT` is zero-based. The current core exposes 8 slots. `START` and `END` are
byte addresses. `END` is optional and defaults to `START`.

Regions:

- `SRAM`: AVR internal SRAM, byte addresses `0x000` through `0xfff`
- `IO`: AVR I/O register addresses `0x00` through `0xff`

Modes:

- `R`: break/report reads
- `W`: break/report writes
- `RW`: break/report both reads and writes

## Example

```text
WATCH CLEAR ALL
WATCH SET 0 IO W 0x2b
RUN_FRAMES 1
WATCH HIT CLEAR
```

A hit response includes:

- `slot`
- `region`
- `mode`
- `addr`
- `value`
- `pc`

`pc` is an AVR word address, matching the debugger and profiler APIs.

## Memory access history

When `ENABLE_MEMORY_TRACE` is built, the same gate can arm a 4096-event SRAM/I/O
history ring. `FLAG_MEMORY_TRACE` defaults to `FLAG_DEBUGGER`; set it to `0` to
retain normal debugger/watchpoint support without compiling the history ring.

```text
MEM_TRACE STATUS
MEM_TRACE ENABLE
MEM_TRACE DISABLE
MEM_TRACE CLEAR
MEM_TRACE READ [SEQ] [COUNT]
```

Each event reports sequence number, absolute AVR cycle, raster row/cycle, PC,
region, read/write mode, address and byte value. History is disabled after reset
and begins recording only after `MEM_TRACE ENABLE`.
