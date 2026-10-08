# Automation API memory snapshots

Memory snapshots provide a bounded way to compare emulator memory before and
after a frame sequence, debugger operation, input script, or suspected fault.
They add no work to the AVR instruction path; memory is copied only when an API
command explicitly requests it.

```text
MEMSNAP TAKE <SLOT> <REGION> <ADDR> <LEN>
MEMSNAP DIFF <SLOT> [MAX]
MEMSNAP LIST
MEMSNAP CLEAR <SLOT>
```

Four snapshot slots are available, numbered `0` through `3`. A snapshot may
cover `SRAM`, `IO`, `SPIRAM`, `EEPROM`, or `FLASH`. Length is clamped to 1024
bytes and also clamped at the end of the selected memory region.

`MEMSNAP DIFF` compares the saved bytes against current emulator memory. Its
response contains the total number of changed bytes and a bounded list of
changes, each with `addr`, `before`, and `after`. The optional `MAX` controls
how many individual changes are returned and is clamped to 128. The
`truncated` field reports whether additional changed bytes were omitted.

Snapshots retain the frame number at which they were taken. They are local to
the API server process and are not save states: CPU registers, peripherals,
timers, and other emulator state are not captured.

## Example

```text
PAUSE
MEMSNAP TAKE 0 SRAM 0x100 512
RUN_FRAMES 60
MEMSNAP DIFF 0 64
MEMSNAP LIST
MEMSNAP CLEAR 0
```

This is useful for finding counters, object arrays, dirty regions, and memory
corruption without repeatedly transferring an entire region to the client.
