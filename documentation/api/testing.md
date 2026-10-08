# Automated testing

This page describes the intended testing workflow built on top of the API.

## Philosophy

The first goal is not to create a giant testing framework.
The first goal is to make emulator-driven regression tests practical.

A good test should be able to:

- launch the emulator with a known ROM
- control input deterministically
- run for a known number of frames
- inspect memory or symbols
- decide pass or fail

## Current helper tools

The project includes starter automation support such as:

- `release-test.sh`
- `tools/api_test_runner.py`

These are intended as a base for scripted tests.

## Example workflow

A useful pattern looks like this:

1. launch emulator with API enabled
2. connect to localhost API port
3. `LOAD_ROM WAIT <path>`
4. verify `GET_ROM_INFO`
5. `QUEUE_INPUT ...` or `SET_INPUT ...`
6. `RUN_FRAMES <count>` or `RESUME`
7. `WAIT_FRAME ...` or `WAIT_MEM ...`
8. `READ_SYMBOL ...` or `READ_MEM ...`
9. report success/failure

## Why `LOAD_ROM WAIT` matters

For automation, start timing should be explicit.
If a ROM loads and begins running immediately, the test harness may miss its exact start point.

`LOAD_ROM WAIT` lets the harness define the moment timing begins.

## Example test ideas

### Title-screen test

- load ROM in wait mode
- queue Start button
- run 60 frames
- verify a mode/state variable changed

### Movement test

- load ROM
- queue Right for 10 frames
- verify player X increased

### UART/device test ROM

- load a ROM built specifically for automated verification
- wait until a result byte changes
- assert pass/fail code in SRAM or I/O

## Suggested directory layout

A good starter layout could be:

```text
tests/
  roms/
  scripts/
  outputs/
```

## Future improvements

Good next steps for testing support include:

- API breakpoint/watchpoint commands
- symbol-aware wait conditions
- headless or reduced-UI test mode
- richer screenshot-based regression checks
- save-state-based test setup

## TODO

Add real end-to-end example scripts once a few test ROMs or stable target programs are selected.

## SD timing stress testing

Use `tools/sd_stress_runner.py` for repeatable card-timing sweeps over the local
API. Supported model keys are `init_ms`, `cmd_wait_bytes`, `read_wait_bytes`,
`write_busy_ms`, `cs_high_ms`, `init_min_byte_cycles`, and
`init_max_byte_cycles`. `--sweep` accepts comma lists or inclusive `start:end`
/`start:end:step` ranges. `--threshold setting=low:high` binary-searches the
largest passing value. `--conformance` implies SD tracing and fails a case on
reconstructed protocol/conformance errors as well as ROM-run failure. Add
`--expect-mem REGION:ADDR:OP:VALUE` when a ROM has a
stable memory-visible success flag; otherwise completion of the requested
`RUN_FRAMES` budget is the success criterion. The runner refuses active SD
faults and SD protocol/filesystem break conditions by default; use
`--allow-faults` / `--allow-breaks` only when they are intentionally part of
the test. With `--trace`, the original trace enable state is restored on exit.
