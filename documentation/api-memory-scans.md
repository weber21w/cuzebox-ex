# Automation API memory scans

Memory scans provide a persistent, iterative search over an emulator memory
range. They are intended for locating counters, health values, object state,
flags, and other variables whose address is not yet known.

The scan engine is API-side tooling. It performs no work in the AVR instruction
path and reads memory only when a `MEMSCAN` command is issued.

```text
MEMSCAN START <SLOT> <REGION> <ADDR> <LEN> [ANY|VALUE|OP VALUE]
MEMSCAN REFINE <SLOT> <OP> [VALUE]
MEMSCAN RESULTS <SLOT> [OFFSET] [COUNT]
MEMSCAN STATUS
MEMSCAN CLEAR <SLOT|ALL>
```

Four independent slots are available, numbered `0` through `3`. Each slot can
scan up to 4096 bytes from `SRAM`, `IO`, `SPIRAM`, `EEPROM`, or `FLASH`.

## Starting a scan

`MEMSCAN START` records the current byte values and establishes the initial
candidate set.

- omit the final argument, or use `ANY`, for an unknown initial value
- provide a numeric byte value for an exact initial match
- provide `EQ`, `NE`, `LT`, `LE`, `GT`, or `GE` followed by a byte value for a
  relational initial filter

Examples:

```text
MEMSCAN START 0 SRAM 0x100 4096 ANY
MEMSCAN START 1 SRAM 0x100 2048 100
MEMSCAN START 2 SPIRAM 0 4096 GE 0x80
```

## Refining candidates

`MEMSCAN REFINE` removes candidates that no longer satisfy the selected test.
Relational operators compare the current value against a supplied byte:

- `EQ` or `==`
- `NE` or `!=`
- `LT` or `<`
- `LE` or `<=`
- `GT` or `>`
- `GE` or `>=`

Change operators compare the current value with the value recorded by the
previous `START` or `REFINE` command:

- `CHANGED` or `CHG`
- `UNCHANGED` or `SAME`
- `INCREASED` or `INC`
- `DECREASED` or `DEC`

After every refinement, current values become the baseline for the next
change-based refinement.

## Reading results

`MEMSCAN RESULTS` returns candidate addresses with both the last recorded value
and the current value. Results are paged by candidate ordinal rather than by
memory address. The default page size is 32 and the maximum is 128.

```text
MEMSCAN RESULTS 0
MEMSCAN RESULTS 0 32 32
```

The response includes `remaining`, `returned`, `next_offset`, and `truncated`
fields so an automation client can enumerate large result sets safely.

## Example unknown-value search

```text
PAUSE
MEMSCAN START 0 SRAM 0x100 4096 ANY
RESUME
# Change the in-game quantity being investigated.
PAUSE
MEMSCAN REFINE 0 DECREASED
RESUME
# Change it again in the same direction.
PAUSE
MEMSCAN REFINE 0 DECREASED
MEMSCAN RESULTS 0 0 64
```

This workflow narrows an initially unknown 8-bit value without adding
instrumentation to the emulated program or the AVR core.
