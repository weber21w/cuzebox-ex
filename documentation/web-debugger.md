# Web debugger and diagnostics

CUzeBox uses a localhost browser UI for the full development debugger and the
larger serial/network diagnostic views. This replaces the space-constrained
MicroUI debugger surfaces as the normal interface.

## Architecture

The browser does **not** have a private path into emulator state:

```text
Browser -> embedded localhost web server -> CUzeBox API -> emulator/debugger core
```

The web server is compiled into CUzeBox and runs on its own thread, but it still
uses ordinary localhost API connections for emulator state and control. CPU
state, memory, run control, serial state, ESP state and netplay state are all
obtained through `api_server.c`. New diagnostics should be added to the API
first and then presented by the browser. The Python bridge remains available as
a development/fallback server and follows the same rule.

## Routes

With the bridge on its default port (`24681`):

```text
http://localhost:24681/          Web tools home
http://localhost:24681/controls  Emulator controls and file loading
http://localhost:24681/debugger  CPU/source/debugger
http://localhost:24681/sd        SD-card timing/model controls
http://localhost:24681/audio     Audio oscilloscope / clipping diagnostics
http://localhost:24681/serial    UART/serial configuration and trace
http://localhost:24681/network   ESP/Wi-Fi/socket and netplay diagnostics
```

When `ENABLE_API_SERVER`/`ENABLE_DEBUGGER` are built in, CUzeBox's MicroUI
Tools menu uses these pages instead of opening the old large debugger windows.
The serial backend-details control likewise links to `/serial`.

When the API server is enabled, CUzeBox starts the embedded web server itself;
no Python process is required. `make web-debugger`, `make web-debugger-no-browser`,
`make web-tools`, `make web-controls`, `make web-sd`, `make web-audio`, `make web-serial`, and `make web-network` remain available for
standalone frontend development.

The CUzeBox API defaults to `127.0.0.1:24680`; the embedded web server defaults
to `127.0.0.1:24681`. Both are localhost-only by default. If those ports are
already taken (for example by the other instance of a link-cable pair), the
second instance uses 24682/24683, then 24684/24685, and so on; the console and
the on-screen message show the ports actually used.

The embedded bridge keeps one persistent connection to the API for page
commands (plus one per open page for the live event stream), so browsing the
tools no longer produces a stream of API CLIENT CONNECTED/DISCONNECTED
messages. The page header shows "API connected" as long as CUzeBox answers;
a command that CUzeBox answers with an error (for example a feature not built
in) is reported in the console but does not mark the API as disconnected.

## Controls and browser file transfer

The Controls page mirrors normal emulator controls without removing them from
the native GUI. ROMs can still be loaded by host path or recent-ROM entry, and
a browser file picker can upload a ROM in bounded chunks. The embedded server
asks `EMU_STATUS` for the configured ROM directory, writes a collision-safe
persistent copy there, then loads that copy using the normal `LOAD_ROM` API.
Uploads are limited to 512 MiB and incomplete uploads are removed on cancel or
shutdown.

The debugger can download a selected memory range or an entire SRAM, I/O,
EEPROM, Flash, or SPI-RAM region. `/api/memory-dump` streams the response while
issuing bounded `READ_MEM` calls over a persistent API connection, so a large
SPI-RAM export does not require buffering the whole image in the web server.
`MEM_SIZE`/`MEM_REGIONS` supply the actual active region sizes.

The Controls page also offers a browser screenshot download. It uses
`SCREENSHOT_CAPTURE` plus bounded `SCREENSHOT_READ` requests, so the BMP is
constructed and streamed entirely in memory with no transient host file. The
existing native/host screenshot action remains available separately.

## Debugger page

The debugger page includes:

- pause/run/reset, frame stepping and instruction step-into/over/out
- run-to address
- AVR registers with changed-value highlighting and register editing
- ELF/DWARF source following and source-line breakpoints
- disassembly and instruction breakpoints
- raw stack and heuristic call stack
- breakpoint and read/write watchpoint management
- opt-in SRAM/I/O memory-access history with cycle, raster position, PC, address and value
- persistent browser-side value-watch layout, with all values read via API
- SRAM, I/O, EEPROM and SPI RAM editing; Flash is read-only
- raw memory hex/ASCII view
- 1/2/4/8-bpp graphical memory preview using API-read bytes
- debugger profile status/save/reload
- profiler controls and top-address statistics
- cycle-domain scanline beam view with a live scanline/cycle marker even while
  capture is disarmed; arming PORTC capture adds one BBGGGRRR sample per AVR cycle
  for the exact 1440-cycle active-video window and porch/sync regions
- separately armed instruction-span map: records PC, absolute cycle and start/end
  raster positions per executed AVR instruction; hover correlates a beam cycle to
  its PC/cycle cost and Shift-click jumps disassembly to that producer
- raster breakpoints placed numerically or by clicking a beam cycle; active-video
  start/end presets use cycles 299/1739, while actual SYNC-rise/SYNC-fall modes
  trigger from the emulated PORTB edge rather than assuming ideal line timing
- opt-in cycle logic analyzer aligned to the same 1820-cycle ruler, with SYNC,
  SPI-RAM/SD chip select, controller LATCH/CLOCK/DATA, reconstructed SPI
  SCK/MOSI/MISO, IRQ-active intervals, Timer1 compare/overflow pulses, UART
  byte events, and optional high-volume PORTC write events
- browser-side VCD export of the captured analyzer ring; SPI edges are rebuilt
  from each exact transfer start/duration plus CPOL/CPHA/DORD and TX/RX bytes
- opt-in scanline CPU-budget heatmap with actual line length, 1820-cycle slack
  or overrun, instruction-cycle count, and dominant-PC attribution
- live SD-card state alongside the CPU registers: card/packet/command phases,
  current sector and packet byte, R1 and next MISO byte, CS/last-byte age,
  initialization or write-busy deadline, and the AVR SPI transfer/TX/RX registers
- instruction stepping snapshots SD state before arming the step, waits until the
  emulator is paused again, then shows the exact SD state-machine delta caused by
  that completed Step Into/Over/Out operation
- raw API console

Use **Auto ELF** to load the `.elf` beside the active ROM. CUzeBox parses its
DWARF 2-4 line table; the HTTP bridge does not invoke `addr2line` or inspect
emulator state itself.


## SD-card timing and live-state page

The native Hardware configuration page now exposes the same SD timing preset
selector and an **SD DETAILS...** button that opens this page. The SD page
provides SLOW, NORMAL, FAST and CUSTOM runtime models. **NORMAL** uses the
original CUzeBox/Uzem timing values exactly: 500 ms initialization,
zero command-response wait bytes, two read/inter-sector wait bytes, 100 ms
write busy time, zero required CS-high delay, and the original 16..2296-cycle
initialization byte-cadence window. Selecting a preset or editing a custom value
resets the emulated card so the model starts from a clean initialization state.

`SD_STATUS` is a read-only snapshot rather than a trace. It exposes the existing
SD and AVR SPI state only when requested, including command/packet phases, command
argument, R1 flags, next MISO byte, sector/packet offset, CS age, last-byte age,
pending initialization/write-busy deadline, and active AVR SPI transfer state.
Therefore leaving all tracing tools disarmed does not add an SD hot-path event or
history writer merely to make the status panel available.

Both the main debugger and `/sd` render that snapshot as a compact protocol
timeline. The command lane fills the six command bytes as they are received,
including the four argument bytes and expected CRC7. The response lane shows
stuff-byte wait, R1 and four-byte extended responses. The block lane shows data
token wait, the 512-byte payload position, CRC16 bytes, write response token and
write-busy interval. A fourth lane shows the active AVR SPI byte and bit-level
transfer progress. The browser remembers the most recently observed command
packet so it remains visible after the card advances into the data phase. Step
Into/Over/Out refreshes the same `SD_STATUS` snapshot after the CPU has really
paused, keeping the protocol timeline synchronized to the completed AVR
instruction without requiring history capture.

The debugger and SD page also expose an independently armed SD protocol trace.
It records completed command/R1 transactions, multiblock sector transitions,
initialization failures, data-CRC rejects, response-latency events, and command
aborts. While history is armed, data transactions additionally record only their
meaningful protocol boundaries: data-token arrival, completion of byte 512, CRC
completion, sector advance, and write-busy release. It deliberately does not
record 512 per-byte payload entries.

History is grouped by transaction ID in expandable CMD-to-data views. All six
command bytes retain their exact CPU cycle, AVR PC, scanline and beam-cycle
position, so gaps between command bytes remain visible rather than being inferred
as back-to-back SPI traffic. The CRC byte and expected CRC7 are shown together,
and the R1/data phase entries carry CRC16 and write-response details when
applicable. In the main debugger, selecting any historical phase sets the
disassembly target and pins its historical row/cycle on the beam display; the
raster-break fields are populated with the same position for immediate re-run.

Break conditions can target any CMD number, a sector, initialization failure,
CRC error, or an R1 response-latency threshold. CMD17/CMD18/CMD24/CMD25 have
direct checkboxes in the UI. A protocol-triggered stop occurs at the end of the
AVR instruction that delivered the relevant SPI byte, so CPU, beam, and SD state
are mutually consistent while single-stepping.

Break conditions can additionally target filesystem ownership. `FS_ROLE` can
match BOOT, FAT1/FAT2, ROOT, FILE, DIRECTORY, SPARSE or UNALLOCATED sectors;
`FS_ACCESS` restricts matching to reads, writes, or both; and `FS_PATH` matches a
case-insensitive substring of the resolved VFAT owner path. The UI provides
direct FAT/root/file/subdirectory controls plus an owner-substring field. VFAT
ownership is not resolved unless one of these filesystem criteria is active.

This trace is pay-for-play. History and all break criteria share one gate in
`cu_spisd_send()`, evaluated once per SD byte rather than once per AVR cycle or
instruction. A trigger requests a stop through the existing debugger event-stop
boolean, shared with watchpoints. Set `FLAG_SD_TRACE=0` to compile the history,
trigger state, and SD-byte gate out completely.

### Filesystem-operation history

`SD_FS_HISTORY [COUNT]` reconstructs a higher-level VFAT activity stream from the
already-captured SD protocol ring. It does not maintain a second trace buffer or
add any emulation hook. Successful CMD17/CMD18/CMD24/CMD25 sector boundaries are
resolved through the debugger VFAT map and coalesced into operations such as
`DIR_LOOKUP`, `FAT_LOOKUP`, `FILE_READ`, `FILE_WRITE`, and metadata updates.

Consecutive file sectors are merged by logical file offset, so a fragmented FAT
chain still appears as one logical read/write range while the result separately
reports cluster transitions and physical runs. Each operation retains the first
AVR PC/raster position, cycle range, trace-sequence/transaction range, sectors
started/completed, bytes completed, R1/CRC/fault/abort state, and current FAT
chain information. Directory and FAT reads are labelled as lookups because that
is the filesystem-level activity observable from the wire; the debugger does not
invent an `OPEN` event that the SD protocol cannot prove.

The main debugger makes operation rows navigable: selecting one jumps disassembly
to its first AVR PC, pins its first beam row/cycle, and opens the first sector in
the payload inspector. The `/sd` page uses the same reconstruction and opens the
selected sector. Ownership is resolved against the current VFAT map at query time,
which keeps reconstruction snapshot-only and avoids additional trace-time work.

### Deterministic SD fault injection

`SD_FAULT` is separate from `SD_TRACE` and has eight independently configurable
rule slots. The web debugger and `/sd` page can arm a rule as one-shot or
persistent, with optional CMD and sector filters. Supported rule kinds are R1
response delay, data-token delay, write-busy extension, injected R1 bits, forced
command CRC error, forced data CRC error, and command rejection. One-shot rules
disable themselves after the matched fault is actually consumed; persistent
rules retain a fire counter and last-cycle timestamp.

API forms are:

```text
SD_FAULT STATUS
SD_FAULT CLEAR <slot|ALL>
SD_FAULT SET <slot> <kind> <value> <ONCE|PERSIST> [CMD <0..63|ANY>] [SECTOR <n|ANY>]
SD_TRACE BREAK FS_ROLE <role> <ON|OFF>
SD_TRACE BREAK FS_ACCESS <R|W|RW>
SD_TRACE BREAK FS_PATH <substring|OFF>
```

When SD history is armed, injected faults also appear as `FAULT` history events.
History is not required for fault injection. `FLAG_SD_FAULT=0` removes the fault
rule table and all injection checks; with it built but unarmed, only the specific
SD protocol phases capable of being faulted test the shared rule-active flag.
There is no fault-specific AVR-cycle or AVR-instruction hook.

### Deterministic SD interaction record/replay

The debugger and `/sd` page can explicitly record the exact card-visible SPI
interaction and replay it later. The recorder is intentionally below the
filesystem-operation reconstruction layer: it records what the AVR and card see,
not an inferred CMD17/CMD24 script. Every transfer has a `BYTE_START` event for
the MISO sample and a `BYTE_END` event for MOSI delivery. CS transitions and SD
resets are independent events, so even a CS edge during an in-flight byte is
preserved. Event timing is stored relative to the capture origin.

Replay can therefore be rebased to a different absolute CPU cycle while still
requiring the same relative event timing. The timing tolerance field is zero by
default and may be raised deliberately for a known deterministic scheduling
offset. MOSI, CS state/order, reset order and timing are validated; the captured
MISO stream and recorded post-event SD states are restored without executing the
normal card/VFAT path. Any divergence latches the exact event index, expected and
actual value, switches replay to `ERROR`, and requests a debugger break.

Captures can be saved to and loaded from versioned `.sdr` files. Recording an
injected SD fault naturally records its externally visible result; replay then
reproduces that result without requiring the fault rule to remain armed. Storage
is dynamically allocated only for record/load, up to 131072 events (two events
per ordinary SPI byte). `FLAG_SD_REPLAY=0` removes the recorder/replayer and its
SD receive/send hooks completely.

## Audio oscilloscope page

The Audio page provides a live oscilloscope for the native Uzebox 15.734 kHz
unsigned-8-bit DAC stream and an optional post-processing/output waveform. The
native source view snapshots the existing audio ring, so it adds no capture
work to emulation or the SDL callback. Samples at 0x00/0xFF are highlighted as
DAC rail hits.

Host-output capture is explicitly opt-in. While disabled, SDL is configured
with the original `audio_callback` directly: there is no scope branch, ring
write, clip scan, or per-sample debug work in the normal callback. Arming the
scope reopens the audio device with a wrapper callback that calls the original
callback and then records the already-produced host samples. Disarming reopens
the device with the original callback again. This keeps the debugging cost
pay-for-play.

The page displays waveform, peak hold, RMS level, rail-hit counters, longest
consecutive rail run, DC/source statistics, hover sample values, freeze, and
auto-freeze-on-rail-hit behavior. API access is through
`AUDIO_SCOPE STATUS|ENABLE|DISABLE|CLEAR|SOURCE|OUTPUT`.

### Native DAC instruction tracing

`FLAG_AUDIO_TRACE` defaults to `FLAG_DEBUGGER` and may be disabled
independently. When present, `AUDIO_DEBUG` can explicitly arm a 4096-event
history of writes to the emulated AVR `OCR2A` PWM audio register. Each event
contains the absolute AVR cycle, PC, scanline, beam cycle, 8-bit sample and
rail/break flags. The main debugger and Audio page can click these events to
jump to the producing AVR instruction and pin its beam position.

Native-audio break triggers may be armed without history using either exact
DAC rails (`0`/`255`) or a programmable outside-range threshold. A hit reuses
the debugger's existing one-shot post-instruction event-stop flag, so the CPU
stops after the instruction that wrote the offending sample. History and
breakpoints share one `audio_debug_active` gate at the `OCR2A` write path.
With `FLAG_AUDIO_TRACE=0`, that gate, ring and event handler preprocess out
entirely.

This is final-DAC attribution, not logical mixer-channel attribution. CUzeBox
observes the kernel's final PWM sample generically; individual channel envelope,
HOLD/SLIDE or oscillator attribution requires kernel-aware symbol/state decoding
and is intentionally not guessed by the generic audio debugger.

Commands are:

```
AUDIO_DEBUG STATUS
AUDIO_DEBUG ENABLE|DISABLE|CLEAR
AUDIO_DEBUG READ [SEQ] [COUNT]
AUDIO_DEBUG BREAK OFF
AUDIO_DEBUG BREAK RAIL
AUDIO_DEBUG BREAK OUTSIDE <LOW> <HIGH>
AUDIO_DEBUG HIT [CLEAR]
```

## Serial page

The serial page replaces the crowded backend and serial-trace windows. It
exposes API-backed controls/status for:

- serial routing (ESP, host serial, host/virtual MIDI, TCP serial, loopback)
- ESP model and AT firmware profile
- UART timing/profile selection
- SoftAP policy
- host serial and MIDI endpoint names
- virtual MIDI mode/port
- TCP serial client/server settings and auto reconnect
- live UART/backend byte counters and UART status
- TCP queue/service/error diagnostics
- serial trace enable/disable/clear/read/export

String-valued serial API settings consume the complete command remainder so
host and MIDI endpoint names may contain spaces.

## Network page

The network page combines what was previously spread across ESP/network
monitoring surfaces:

- ESP emulator state and UART errors
- Wi-Fi mode, SSID, RSSI/channel and station addresses
- SoftAP and LAN state
- DNS/ping/UDP pending state
- server state and all ESP link/socket slots
- netplay transport state, peer/interface/address and latency/jitter
- packet duplicate/out-of-order/gap counters
- rollback/input-delay/prediction/stall/resimulation state

A section reports unavailable rather than failing the page when its associated
compile option is disabled.

## Multiple API clients

The API supports up to eight simultaneous localhost clients. Each client has
independent RX/TX and wait state, while emulator state and break/watchpoints are
shared. This permits the web tools, API regression scripts and other external
debug clients to operate concurrently.

## Design rule

Do not add web-only emulator inspection hooks. If a web panel needs information
or an action, expose it through `api_server` first. The web frontend should
remain replaceable and external automation should have access to the same
capability.

Diagnostic capture is also pay-for-play. `FLAG_DEBUGGER=0` leaves the AVR
SRAM/I/O access path free of debugger gates and event calls. In debugger builds,
watchpoints and memory history share one `cu_debug_mem_active` branch; the slot
scan/history writer is entered only while at least one consumer is armed.
`FLAG_MEMORY_TRACE` defaults to `FLAG_DEBUGGER`, and setting it to `0` removes
the historical memory ring while retaining ordinary debugger/watchpoint support.

The expensive beam, instruction-map, logic-analyzer, scanline-profiler, UART
waveform and host-output audio captures are all explicitly armed. The beam's
current position does not require history: debugger/API builds keep only a
line-start timestamp updated at sync-row boundaries and derive the live cycle
from the existing absolute AVR cycle. There is no continuously incremented
beam-debug counter. `FLAG_BEAM_CAPTURE` and `FLAG_BEAM_HISTORY` both default to
`FLAG_DEBUGGER` and can be disabled independently. With beam capture compiled
in, the disarmed per-AVR-cycle path is one normally-not-taken
`video_beam_capture_on` branch; with `FLAG_BEAM_CAPTURE=0`, that branch and the
PORTC history buffer compile out completely. `FLAG_BEAM_HISTORY=0` similarly
removes the instruction-map ring while keeping live beam position, raster
breakpoints, and scanline profiling.

Raster, instruction-map and scanline profiling share the instruction timing
gate, while actual SYNC-edge raster breakpoints add only a sync-edge gate at
PORTB transitions. Disarmed timing tools therefore do not enter an out-of-line
diagnostic handler. The native audio callback is replaced by the scope wrapper
only while host-output capture is armed, and UART waveform reconstruction
reuses the existing serial-trace fast gate.

### UART logic waveforms

The logic analyzer reconstructs asynchronous UART TXD/RXD at AVR-cycle resolution from one metadata record per byte. Captured metadata includes the modeled wire-frame start, bit duration, data width, parity, stop bits and RX error flags. With logic capture disabled, the UART continues through the pre-existing serial-trace fast gate; there is no per-bit instrumentation or waveform work.

### SD transaction timing and conformance view

`SD_TIMING_ANALYSIS [COUNT]` reconstructs recent command/block timing from the
existing SD protocol trace ring. It does not collect a second timing history.
Each block reports the command-packet span (for the first block), R1 wait,
data-token wait, 512-byte payload span, CRC span, and write-busy span, together
with the starting AVR PC and raster position. CMD18/CMD25 therefore appear as
one timing sample per sector under the same transaction ID.

Configured `cmd_wait_bytes` and `read_wait_bytes` are intentionally reported as
byte slots rather than converted to a claimed fixed cycle count: the AVR
controls the actual SPI cadence. Write busy has a true cycle-domain minimum
(`write_busy_ms * 28634`); an observed release before that floor is flagged.
Command CRC mismatch, traced CRC/fault/latency/abort conditions, and incomplete
samples are also surfaced as warnings. The web debugger and `/sd` page render
these phases as proportional bars and allow historical samples to be selected.

### Automated SD timing stress runner

`tools/sd_stress_runner.py` drives the localhost API and repeatedly resets the
ROM/SD card with different timing parameters. It restores the original SD
model on exit and refuses to start while deterministic replay is active; armed
fault injection is also rejected unless `--allow-faults` is explicit. Armed SD
protocol/filesystem break conditions are rejected unless `--allow-breaks` is
explicit, so an old breakpoint cannot masquerade as a timing failure.

Examples:

```
python3 tools/sd_stress_runner.py --frames 600 --conformance \
    --sweep cmd_wait_bytes=0:12

python3 tools/sd_stress_runner.py --frames 900 --trace \
    --threshold read_wait_bytes=0:128 \
    --expect-mem SRAM:0x10F:==:1

python3 tools/sd_stress_runner.py --frames 600 \
    --sweep cmd_wait_bytes=0,2,4,8 \
    --sweep write_busy_ms=10,50,100,250 \
    --csv sd-matrix.csv
```

`--threshold` performs a binary search for the largest passing value and assumes
that increasing the selected latency is monotonically harder for the ROM.
Without `--expect-mem`, a case passes only when CUzeBox completes the requested
frame budget without an early debugger stop. `--trace` additionally clears and
arms SD protocol history for each case and records response/token/busy maxima in
CSV/JSON results. `--conformance` implies `--trace` and additionally fails a
case if the reconstructed trace contains a protocol/conformance error even when
the frame budget completed. The runner restores the trace enable/disable state it found
before the sweep; trace history itself is deliberately cleared when `--trace`
is requested.
