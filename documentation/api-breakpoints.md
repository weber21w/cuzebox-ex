# Automation API breakpoints

This API layer exposes the existing debugger breakpoint helpers to external
test runners and development tools. It is meant for scripted debugging: set a
PC breakpoint, run the emulator, then inspect registers, memory, profiler data,
or watchpoint hits after execution stops.

Breakpoints use AVR word addresses, matching the debugger PC display and the
profiler API.

## Commands

```text
BREAK SET <ADDR>
BREAK CLEAR <ADDR|ALL>
BREAK LIST [START] [COUNT]
BREAK NEXT [START]
BREAK TEMP SET <ADDR>
BREAK TEMP CLEAR
BREAK TEMP LIST
BREAK STEP [COUNT]
BREAK STEP_CLEAR
```

`BREAK SET` and `BREAK CLEAR` modify the persistent breakpoint map. Addresses
are masked to the AVR program word-address range.

`BREAK LIST` returns persistent breakpoints at or after `START`. `COUNT`
defaults to 64 and is clamped to 128 so the API response remains bounded.

`BREAK NEXT` is a compact query for UI clients that want to jump to the next
armed breakpoint.

Temporary breakpoints are separate from the persistent map. They are useful for
run-to-cursor, step-over, and step-out style tools, and the AVR core clears them
automatically when they fire.

`BREAK STEP [COUNT]` arms the existing instruction-step countdown and resumes
execution. When `COUNT` instructions have executed, the core stops with a debug
break. Use `BREAK STEP_CLEAR` to cancel a pending instruction-step request.

## Examples

```text
BREAK CLEAR ALL
BREAK SET 0x1234
RESUME
READ_REGS
```

```text
BREAK TEMP SET 0x1400
RESUME
BREAK TEMP LIST
```

```text
BREAK STEP 10
READ_REGS
```

These commands pair well with `PROFILE_TOP`, `READ_MEM`, and `WATCH HIT CLEAR`
for lightweight scripted debugging without adding new overhead to the already
fast AVR interpreter path.
