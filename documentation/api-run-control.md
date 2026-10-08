# Automation API disassembly and run control

This page documents the API commands that expose debugger navigation without
adding new work to the AVR interpreter hot path. These commands are intended for
external tools that want source-like navigation, run-to-cursor behavior, or a
compact view of the current PC.

All program addresses are AVR word addresses, matching the on-screen debugger,
breakpoint commands, and profiler commands.

## Commands

```text
DISASM <ADDR|PC> [COUNT]
RUN_STATUS
RUN_TO <ADDR|PC>
STEP_INTO
STEP_OVER
STEP_OUT
```

`DISASM` returns a bounded JSON list of decoded AVR instructions. `ADDR` may be
a number such as `0x1200`, `PC`, or `.`. `COUNT` defaults to 16 and is clamped to
64 entries. Multiword instructions advance by their decoded word length, so the
returned list follows instruction boundaries rather than raw flash words.

Each `DISASM` entry includes:

- `addr` - AVR word address
- `words` - instruction length in words
- `word0` and `word1` - raw program words for tools that want hex views
- `text` - the debugger's formatted disassembly, including loaded labels and
  live annotations where available

`RUN_STATUS` returns the paused state, current frame, current run-control status
string, active temporary run target, and last breakpoint-hit address.

`RUN_TO` arms the existing temporary breakpoint/run-to-cursor path and resumes
execution. `RUN_TO PC` is accepted but mainly useful as a sanity check.

`STEP_INTO`, `STEP_OVER`, and `STEP_OUT` use the same debugger run-control code
as the UI. `STEP_OVER` falls back to a single-instruction step when the current
instruction is not call-like. `STEP_OUT` requires a return address to be visible
on the emulated stack.

## Examples

```text
PAUSE
DISASM PC 8
STEP_OVER
RUN_STATUS
```

```text
DISASM 0x1000 24
RUN_TO 0x1234
RUN_STATUS
READ_REGS
```

These commands complement `BREAK`, `WATCH`, and `PROFILE_TOP`: a tool can find a
hot PC, disassemble around it, set watchpoints, then run directly to the code of
interest.
