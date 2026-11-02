# Timer1 normal-mode compare support

Timer1 supports compare-A and compare-B flags/interrupts in normal mode
(`WGM13:0=0`) as well as the existing OCR1A-based CTC path. Normal-mode matches
do not clear `TCNT1`; it continues to `0xFFFF` and wraps. Matches can coincide
with each other or overflow, with the existing AVR interrupt priority handling
all pending flags.

The scheduler selects the next compare or overflow event. No new compare
checks were added to the per-cycle CPU macros. Writing OCR1A/B or changing the
mode reschedules the next event. High-byte writes use Timer1's shared TEMP
latch and commit with the low-byte write. Flags latch even when their
interrupt is masked; enabling `TIMSK1` requests an interrupt check, and
writing a one to `TIFR1` clears the corresponding flag.

A `TCNT1` write suppresses compare matches on the following running timer
clock, including CTC counter clearing. Overflow remains independent. The
pending suppression survives save-state and rewind restoration via an optional
12-byte timer trailer; the existing CPU layout and version-1 save-state header
are unchanged. Old states without this trailer remain loadable.

The default native `FLAG_AVR_TIMER1_CLOCK=1` build models internal prescaling,
counter stop/start and GTCCR prescaler reset/hold. This is the new standard;
its distinct strict replay behavior identifier is intentional.
See [AVR hardware build options](avr-hardware-options.md) for build commands,
state/replay compatibility and the remaining unsupported timer features.

Behavior follows the [ATmega644 datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/doc2593.pdf),
particularly sections 14.7 (compare units), 14.7.2 (blocking after counter
writes), 14.8.1 (normal mode), and 14.11 (registers).

## Verification

```sh
make -f tests/preferences/Makefile TSYS=linux timer1-test
```

The regression covers flags, simultaneous events, interrupt priority, real
AVR interrupt-handler execution, counter and CPU-cycle rollover, compare
reprogramming, counter-write blocking, CTC behavior, and save-state/rewind
compatibility.

For a local performance comparison, snapshot `cu_avr.c` to
`_obj_/timer1-baseline.c` before applying a timer change, then build the
`timer1_cpu_new.exe` and `timer1_cpu_old.exe` Make targets in the selected
object directory. If the old snapshot has the legacy `uint8 cu_kbd_enabled`
declaration, correct it to `auint` to match `cu_kbd.h` before compiling the
combined test harness. Run:

```sh
python3 tools/benchmark_timer1.py PATH_TO_BASELINE PATH_TO_CURRENT
```

Both harnesses use the production `-O3` CPU optimization flags, with the core
compiled separately and core LTO disabled to isolate it from test-code layout.
The benchmark
alternates five runs, executes the same 288,000,000 emulated CPU cycles per
mode, uses CPU time, and disables rendering/audio. The local comparison showed
no median slowdown for either CTC or normal mode (CTC: 1.182 vs 1.123 seconds;
normal: 1.167 vs 1.088 seconds), with substantial run-to-run variation. These
are the separately compiled core results; combined regression/benchmark
executables produced inconsistent timings and are not used for this conclusion.
This synthetic loop is a regression check, not a guarantee about
every game or device. The standalone scheduler workload is also available
with `--benchmark`; normal-mode event counts differ because the old code
omits compare events.
