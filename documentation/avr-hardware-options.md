# AVR hardware models

Two independent build flags select additional ATmega644 behavior. Both default
to `1` in native builds, matching the hardware-tested ESP8266 client build.
This is the new native standard; the different strict replay behavior identifier
is intentional. The Emscripten target retains `0` for its separate browser
UART/GPIO overlay unless its builder explicitly selects a combination. These are compile options,
not `.uze` hints or runtime settings.

| Flag | Behavior when enabled |
| --- | --- |
| `FLAG_AVR_TIMER1_CLOCK=1` | Timer1 internal `/1`, `/8`, `/64`, `/256`, `/1024` clocks, stopped counter, free-running divider phase, and GTCCR synchronous prescaler reset/hold. |
| `FLAG_AVR_GPIO=1` | PORT latch retention for input pins, digital PIN readback from DDR/latch/external inputs, internal pullups and MCUCR.PUD, and a separate effective PORTC pixel output. |

For example, build an emulator for timing tools such as UNTestRig:

```sh
make FLAG_AVR_TIMER1_CLOCK=1 FLAG_AVR_GPIO=1 \
  OBD=_obj_/hardware OUT=_obj_/hardware/cuzebox
```

Add the normal platform and feature options for the desired build, such as
`TSYS=linux FLAG_OPENSSL=1`. Leaving either flag out selects its default of `1`.
Existing SEI/RETI interrupt delays, PINx write-one toggling, Timer1
normal/CTC compare corrections and the Timer2 normal-mode counter (below)
remain enabled independently of these options.

Timer1 retains the existing compare-latch convention and interrupt priority.
Compare blocking after a TCNT1 write lasts one actual timer clock, including
when prescaled or stopped. Timer events remain scheduled rather than polled
on every CPU cycle. The disabled build compiles out the new clock arithmetic.

GPIO output pins read back their latch. Input pins use explicit external
digital levels when supplied; otherwise they use enabled pullups, or zero for
an undriven input without a pullup. Controller inputs and UzeNet's PA7 signal
retain their existing peripheral connections. The ESP programming/reset pins
retain the board pullups. `cu_avr_gpio_set_input(port, mask, value)` supplies
external test inputs on ports A through D (`0` through `3`); a zero mask releases
them. It is a no-op in builds with the GPIO flag disabled.

PORTC keeps its full latch separately from the DDR-masked pixel value. The pixel
value is cached at writes, including the optimized PIXEL instruction, so the
video loop does not resolve GPIO for each pixel. Both instruction decoders use
the same behavior.

With both flags disabled, the separately compiled Linux `-O3` core's `.text`
section matches the baseline byte for byte. No new instructions remain in that
core's timer, I/O or rendering paths.

## Timer2 in normal mode

In every build, Timer2 counts in normal mode (WGM2 = 0) at the clock CS22..20
selects. TCNT2 and TOV2 are brought up to date from the cycle counter when
TCNT2 or TIFR2 is read or a Timer2 register is written, so only those accesses
pay for it. A wrap sets TOV2, and writing a one to it clears it. The PWM modes,
which drive Uzebox audio through OCR2A, behave as before, and CTC is not
counted. TOV2 is a flag only: the overflow interrupt, the compare matches,
ASSR's asynchronous clock and GTCCR.PSRASY are not modelled. TCNT2 is in the
CPU structure, so save states keep their layout; after a restore or a reset the
count goes on from TCNT2, with a fresh prescaler phase. `timer2-test`
(`tests/bootloader/timer2_regression.c`) covers it.

## Save states and replay

Enabled builds append a 24-byte `CZH1` hardware trailer to save states, carrying
the compiled feature mask, divider phase and external GPIO inputs. The CPU
structure and version-1 save-state header remain unchanged. Builds with both
flags disabled keep the original state layout and replay behavior identifier.

Each enabled combination has a distinct replay behavior identifier. Strict
movie/checkpoint validation requires the same hardware features. Restoring an
identified state from a different combination fails before changing machine
state. General state loading can import older states without the hardware
trailer, starting with a fresh divider phase and no external test inputs; this
does not guarantee timing equivalence with the original recording.

## Scope and checks

These options do not implement a Timer0 general counter, Timer1 external
T1 edges, PWM, input capture, force compare, OC pin waveforms, GPIO synchronizer
delay, electrical contention, or additional external/pin-change interrupts.
Timer1 external clock selections stay stopped in the enabled clock model.

The model follows the [ATmega644 datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/doc2593.pdf),
particularly general digital I/O, Timer1 and the shared Timer0/Timer1 prescaler.
Run the tests for each flag combination using a separate object directory:

```sh
make -f tests/preferences/Makefile TSYS=linux \
  FLAG_AVR_TIMER1_CLOCK=1 FLAG_AVR_GPIO=1 OBD=_obj_/hardware \
  interrupt-test timer1-test timer2-test hardware-test storage-test input-movie-test
```

Checks cover internal divider deadlines, stop/restart and divisor changes,
GTCCR reset/hold, CPU-cycle wrap, divided compare blocking, CTC repetition,
snapshot phase restoration, incompatible feature masks, GPIO pulls and mixed
directions, both PIXEL decoders, and existing interrupt/movie regressions.
UNTestRig's actual AVR ROM also passes LAN discovery, polled AT/SPI/SD tests,
and restoration of video/communications with both flags enabled. Physical
hardware timing comparison remains a separate measurement.
