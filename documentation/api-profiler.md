# Automation API profiler

The profiler is a lightweight developer-only feature compiled behind
`ENABLE_DEBUGGER`. It counts how often each AVR program word address executes
and the number of AVR cycles consumed by instructions starting at that address.

Typical use from an API client:

```text
PROFILE CLEAR
PROFILE ON
RUN_FRAMES 120
PROFILE OFF
PROFILE_TOP 16 CYCLES
```

Commands:

- `PROFILE ON` enables counting.
- `PROFILE OFF` disables counting without clearing accumulated results.
- `PROFILE CLEAR` or `PROFILE RESET` clears accumulated results.
- `READ_PROFILE <ADDR> [LEN]` returns raw per-PC samples starting at AVR word
  address `<ADDR>`. `LEN` is clamped to 128 samples.
- `PROFILE_TOP [COUNT] [CYCLES|HITS]` scans the profile table and returns the
  hottest entries. `COUNT` defaults to 16 and is clamped to 32. Sorting defaults
  to `CYCLES`.

All addresses are AVR word addresses, matching the debugger PC display and the
symbol table addresses used elsewhere in CUzeBox.


## Watchpoint companion commands

The API also exposes the debugger watchpoint engine, which is useful when a hot
address from `PROFILE_TOP` needs to be tied back to data or I/O activity.

Example:

```text
WATCH CLEAR ALL
WATCH SET 0 SRAM W 0x0100 0x01ff
RUN_FRAMES 1
WATCH HIT CLEAR
```

The returned hit includes the watch slot, region, access mode, address, value,
and AVR PC word address. Only `SRAM` and `IO` watchpoints are supported because
those are the regions the AVR core can observe directly as reads and writes
happen.
