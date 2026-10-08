# API command reference

This page is a starter command reference for the current automation API.

## General notes

- commands are intended for localhost use
- commands are request/response oriented
- up to eight localhost API clients may be connected simultaneously
- each client has independent transport queues and pending wait state
- emulator/run-control/debugger state is intentionally shared between clients
- waits are checked at frame boundaries rather than inside the CPU hot path

The settings API exposes `system_messages` (overlay visibility),
`log_verbosity` (0=Off, 1=Errors, 2=Info, 3=Debug, 4=Trace), and `fast_flash`.
These use the same persisted preferences as the GUI. See the
[logging guide](../logging.md) for message filtering and rate limits.

## Core utility commands

### `PING`

Basic liveness check.

### `HELP`

Returns a list or summary of commands.

## ROM and emulator state

### `GET_ROM_INFO`

Returns information about the currently loaded ROM.

Expected use:

- verify the right ROM is loaded before starting a test
- report title or CRC information

### `GET_STATE`

Returns high-level emulator state.

Typical fields include:

- running or paused state
- frame count
- bounded-run state
- API port
- current and maximum API client counts

### `GET_FRAME`

Returns the current frame number.

## ROM control

### `LOAD_ROM WAIT <path>`

Loads a ROM and leaves it waiting so timing can begin from an explicit later command.

### `LOAD_ROM RUN <path>`

Loads a ROM and starts running immediately.

### `RESET`

Resets the emulator.

### `PAUSE`

Pauses execution.

### `RESUME`

Resumes execution.

### `STEP_FRAME`

Advances exactly one frame.

### `RUN_FRAMES <count>`

Runs a fixed number of frames.

## SD card debugger

### `SD_STATUS`

Returns an on-demand snapshot of the current SD-card state machine and AVR SPI
transfer state. It does not arm or write a trace buffer. The `protocol` object
also derives a debugger-friendly live view of the six-byte command packet,
R1/extra-response phase, data-token wait, 512-byte payload progress, CRC16
state, write response token, and busy interval. These fields are reconstructed
from state that the emulator already maintains; querying them does not enable
SD tracing.

### `SD_TRACE STATUS`

Returns whether SD protocol tracing was compiled, whether history is armed, the
history range, current break criteria, and the most recent SD-trigger hit.

### `SD_TRACE ENABLE|DISABLE|CLEAR`

Arms/disarms the 256-entry protocol history or clears it. History is independent
of SD break criteria.

### `SD_TRACE READ [seq] [count]`

Reads protocol events. Events include command/R1 transactions, multiblock sector
boundaries, initialization failures, data CRC rejects, response-latency events,
and command aborts.

### `SD_TRACE BREAK CMD <0..63> <ON|OFF>`

Arms or disarms a command breakpoint. The stop occurs after the R1 response so
the command argument, response, resulting card state, and sector are available.
`SD_TRACE BREAK CMD_CLEAR` clears all command breakpoints.

### `SD_TRACE BREAK SECTOR <sector|OFF>`

Breaks when a data command targets the selected sector or when a CMD18
multiblock read advances into it.

### `SD_TRACE BREAK INIT <ON|OFF>`

Breaks on native-mode initialization pulse/cadence failure or an initialization
command that returns an error.

### `SD_TRACE BREAK CRC <ON|OFF>`

Breaks on command R1 CRC errors and rejected write-data CRC responses.

### `SD_TRACE BREAK LATENCY <cycles|OFF>`

Breaks when the command-to-R1 response wait exceeds the selected CPU-cycle
threshold.

### `SD_TRACE HIT [CLEAR]`

Returns the most recent SD-trigger event and optionally clears the latched hit.

### `SD_REPLAY STATUS`

Returns compile state, current record/replay mode, event count/cursor, timing
tolerance, completion-break setting, and any deterministic mismatch information.

### `SD_REPLAY RECORD|STOP|CLEAR`

Starts an explicitly armed card-visible capture, stops record/replay without
discarding the captured stream, or clears the stream. Capture records separate
`BYTE_START` and `BYTE_END` events plus CS transitions and SD resets.

### `SD_REPLAY REPLAY [tolerance_cycles]`

Restores the captured initial card state at the current CPU cycle and replays the
recording. The AVR's MOSI bytes, CS/reset ordering and relative timing must match.
`tolerance_cycles` defaults to zero; a nonzero value allows that many CPU cycles
of deterministic scheduling difference per event. During replay the captured
MISO bytes are substituted directly and VFAT is not accessed.

### `SD_REPLAY READ [start] [count]`

Returns recorded events. `BYTE_START` reports the MISO byte sampled by the AVR
and its relative start cycle; `BYTE_END` reports the MOSI byte delivered to the
card and its relative end cycle. CS/reset events carry their own relative cycle.

### `SD_REPLAY SAVE <path>` / `SD_REPLAY LOAD <path>`

Saves or loads the versioned little-endian `.sdr` capture format. Loading does
not start replay; use `SD_REPLAY REPLAY` afterward.

### `SD_REPLAY BREAK_END <ON|OFF>`

Controls whether completing the final replay event requests a debugger stop.
Mismatch errors always request a debugger stop.

## Waiting commands

### `WAIT_FRAME <target> [timeout_frames]`

Waits for an absolute frame target.

### `WAIT_MEM <region> <addr> <op> <value> [timeout_frames]`

Waits until a memory condition becomes true.

Supported operators currently include:

- `==`
- `!=`
- `<`
- `<=`
- `>`
- `>=`
- `&`

## Memory and register reads

### `READ_REGS`

Returns CPU register state.

### `GET_DEBUG_INFO`

Returns debugger-oriented state.

### `DISASM <ADDR|PC> [COUNT]`

Returns decoded AVR instructions starting at a word address, `PC`, or `.`.
`COUNT` defaults to 16 and is clamped to 64 entries. Entries include the word
address, instruction length, raw program words, and formatted debugger text.

### `STACK [COUNT]`

Returns raw bytes beginning at `SP + 1`. The default is 32 bytes and the
maximum is 128.

### `CALLSTACK [COUNT]`

Returns a best-effort call chain by scanning stack byte pairs and validating
return addresses against `CALL`, `RCALL`, and `ICALL` opcodes. The result is
heuristic and intentionally excludes unvalidated stack data.

### `RUN_STATUS`

Returns debugger run-control state, including paused/running status, frame
number, active temporary run target, last breakpoint hit, and the UI debugger
status string.

### `RUN_TO <ADDR|PC>`

Arms the debugger's temporary run-to target and resumes execution.

### `STEP_INTO` / `STEP_OVER` / `STEP_OUT`

Uses the same debugger navigation helpers as the UI. `STEP_OVER` falls back to a
single-instruction step when the current instruction is not call-like.

### `MEM_SIZE <REGION>`

Returns the current size in bytes of one memory region. This is useful for tools
that need to export the complete active SPI-RAM allocation rather than assuming
a fixed size.

### `MEM_REGIONS`

Returns the current byte sizes of SRAM, I/O, Flash, EEPROM, and SPI RAM in one
response.

### `READ_MEM <REGION> <ADDR> [LEN]`

Reads bytes from a memory region.

Known regions currently include:

- `SRAM`
- `IO`
- `EEPROM`
- `FLASH`
- `SPIRAM`

## Video timing and logic-analyzer commands

### `VIDEO_BEAM [STATUS|ENABLE|DISABLE|READ] [START] [COUNT]`

Returns the current cycle-domain beam position and line geometry. The live row
and cycle are available even when PORTC capture is disarmed. `STATUS` reports
`built` so clients can distinguish a disarmed capture from a build made with
`FLAG_BEAM_CAPTURE=0`. `ENABLE` arms the per-cycle BBGGGRRR PORTC buffer;
`DISABLE` removes that capture work while the live position remains available.

### `BEAM_HISTORY STATUS|ENABLE|DISABLE|CLEAR|READ [SEQ] [COUNT]`

Controls the opt-in instruction-to-video map. `STATUS` reports `built`; builds
with `FLAG_BEAM_HISTORY=0` retain the rest of the timing debugger but cannot arm
this history. Each retained instruction reports its sequence number, PC,
absolute start/end AVR cycle, cycle count, and start/end raster row/cycle. The
ring retains the most recent 4096 instructions. When disabled it adds no
instruction handler call beyond the shared timing gate.

### `RASTER_BREAK STATUS|SET <ROW|ANY> <CYCLE>|EVENT <RISE|FALL> [ROW|ANY]|CLEAR|HIT_CLEAR`

`SET` arms a cycle-domain breakpoint. A hit occurs at the first AVR instruction
boundary that reaches or crosses the requested beam cycle, so a multi-cycle
instruction cannot make a raster position unbreakable. `EVENT` arms a real
emulated SYNC edge breakpoint. `RISE` and `FALL` are driven by PORTB transitions
instead of assuming nominal HSync timing. Hit status reports the actual row,
cycle, mode, and PC where execution stopped.

### `LOGIC_TRACE STATUS|ENABLE|DISABLE|CLEAR`

Controls the opt-in cycle logic analyzer. `STATUS` reports the active event
count, first sequence number, capture mask, default mask, and AVR clock rate.
The default mask deliberately excludes high-volume `PORTC` writes because the
beam capture already has one `PORTC` sample per AVR cycle.

### `LOGIC_TRACE MASK [DEFAULT|ALL|NONE|VALUE]`

Selects event classes to retain in the analyzer ring. Event capture is bounded
to the most recent 8192 events.

### `LOGIC_TRACE READ [SEQ] [COUNT]`

Returns captured events with absolute cycle, scanline/cycle, type and payload.
SPI records include TX/RX bytes, exact emulated transfer duration and
CPOL/CPHA/DORD mode; IRQ records include vector/depth; Timer1 records include
the timer value. The web debugger reconstructs SPI SCK/MOSI/MISO transitions
from this metadata and can export the capture as VCD without a temporary file.

UART TX/RX events include `wire_start_cycle`, `bit_cycles`, data bits, parity, stop bits, and error flags. The web debugger reconstructs asynchronous TXD/RXD start/data/parity/stop-bit waveforms from this metadata; no per-bit trace events are generated by the emulator.

### `SCANLINE_PROFILE STATUS|ENABLE|DISABLE|CLEAR`

Controls the opt-in scanline CPU-budget profiler.

### `SCANLINE_PROFILE ROWS [START] [COUNT]` / `ROW <ROW>`

Returns per-row line length, 1820-cycle slack/overrun, instruction counts and,
for a selected row, bounded dominant-PC cycle attribution.

## Source-level debugging

Source commands use AVR **word addresses**, matching `DISASM`, breakpoints and
the PC reported by the debugger. Source information is loaded from the ELF's
DWARF line table inside CUzeBox and is therefore available equally to the web
debugger and other API clients.

### `LOAD_SYMBOLS <AUTO|PATH>`

Loads debugger symbols. When the selected file is an ELF, CUzeBox also parses
its DWARF 2-4 `.debug_line` data. `AUTO` replaces the current ROM extension
with `.elf` and tries that sibling file. Explicit `.map` and text symbol files
continue to work but do not provide source-line mappings.

### `CLEAR_SYMBOLS`

Clears imported symbols and source-line mappings.

### `SOURCE_STATUS`

Returns the active symbol file, symbol count/status, DWARF source-file count,
source-row count and source parser status.

### `SOURCE_LIST [START] [COUNT]`

Returns source files referenced by the loaded DWARF line table, including
whether each resolved source path is currently readable.

### `SOURCE_AT <ADDR|PC>`

Maps an AVR word address to source file index, path, line, column and the mapped
address interval. Returns `mapped:0` when the address has no source row.

### `SOURCE_READ <FILE_INDEX> <START_LINE> [COUNT]`

Reads source text through the CUzeBox API. The web server does not bypass the
API to read source files. `COUNT` defaults to 40 and is clamped to 128 lines.

### `SOURCE_BREAK <FILE_INDEX> <LINE> [SET|CLEAR]`

Resolves a source line to an executable AVR word address and sets or clears a
breakpoint. The response includes the actual resolved line and address.

## DWARF variables and types

When an ELF contains `.debug_info`/`.debug_abbrev`, CUzeBox also imports the
DWARF variable/type graph. The evaluator is AVR-aware and supports the normal
`-gdwarf-2` locations emitted by AVR-GCC: absolute data addresses, register
locations, frame-base-relative locals, register-relative addresses, location
lists, pieces, and common constant/arithmetic expressions. Unsupported
expressions are reported explicitly instead of being guessed.

The imported type graph includes base types, typedef/const/volatile wrappers,
pointers, arrays, structures, unions and enumerations. Structures/unions expose
member offsets; arrays expose element counts; enumerations retain enumerator
names and values.

### `DWARF_STATUS`

Returns type/variable/global counts, the number of locals currently in scope at
the PC, and the DWARF parser status.

### `DWARF_LOCALS [PC|ADDR] [OFFSET] [COUNT]`

Returns parameters and locals in scope at the selected AVR word address, with
resolved type names, current values, and register/SRAM location information.
`PC` is the default.

### `DWARF_GLOBALS [OFFSET] [COUNT]`

Returns global variables with the same value/location information.

### `DWARF_VALUE <ID> [PC|ADDR]`

Returns one variable in detail. Structures/unions include first-level members;
arrays include first-level elements (currently capped at 64 in one response).
Optimized-out or unsupported locations remain visible with a diagnostic.

### `DWARF_WRITE <ID> <VALUE> [PC|ADDR]`

While paused, writes a scalar variable up to eight bytes when its current DWARF
location is an AVR register sequence or addressable data memory. Aggregate,
optimized-out, computed-value and unsupported locations are rejected.

### `DWARF_TYPE <TYPE_ID>`

Returns one imported type and its target type, size, array count, encoding and
structure/union/enum members. Enum entries include their constant values.

## Memory scans

### `MEMSCAN START <SLOT> <REGION> <ADDR> <LEN> [ANY|VALUE|OP VALUE]`

Starts a persistent 8-bit memory search. With `ANY`, every byte begins as a
candidate. A numeric value performs an exact initial search. `EQ`, `NE`, `LT`,
`LE`, `GT`, and `GE` may also be used with a value. Four slots are available,
and scan length is clamped to 4096 bytes.

### `MEMSCAN REFINE <SLOT> <OP> [VALUE]`

Narrows the candidate set. Relational operations compare against a supplied
byte value. `CHANGED`, `UNCHANGED`, `INCREASED`, and `DECREASED` compare against
the values recorded by the previous scan operation.

### `MEMSCAN RESULTS <SLOT> [OFFSET] [COUNT]`

Returns paged candidate addresses, their last recorded values, and their current
values. `COUNT` defaults to 32 and is clamped to 128.

### `MEMSCAN STATUS` / `MEMSCAN CLEAR <SLOT|ALL>`

Lists scan slots or clears one or all persistent searches. Memory is read only
when a scan command is issued, so inactive scans add no execution-path cost.


## Profiler

Profiler commands are available when CUzeBox is built with debugger support.
They use AVR word addresses, matching the debugger PC display.

### `PROFILE ON`

Enables per-PC execution and cycle counting.

### `PROFILE OFF`

Disables profiling without clearing accumulated samples.

### `PROFILE CLEAR`

Clears accumulated profile samples. `PROFILE RESET` is accepted as an alias.

### `READ_PROFILE <ADDR> [LEN]`

Reads raw profiler samples starting at AVR word address `<ADDR>`. `LEN` is
clamped to 128 entries.

### `PROFILE_TOP [COUNT] [CYCLES|HITS]`

Returns the hottest profiler entries. `COUNT` defaults to 16 and is clamped to
32. Sorting defaults to total cycles; use `HITS` to sort by execution count.

## Breakpoints

Breakpoint commands are available when CUzeBox is built with debugger support.
They use AVR word addresses, matching the debugger PC display and profiler API.

### `BREAK SET <ADDR>`

Sets a persistent execute breakpoint.

### `BREAK CLEAR <ADDR|ALL>`

Clears one persistent breakpoint, or all persistent breakpoints.

### `BREAK LIST [START] [COUNT]`

Returns persistent breakpoints at or after `START`. `COUNT` defaults to 64 and
is clamped to 128.

### `BREAK NEXT [START]`

Returns the next persistent breakpoint at or after `START`.

### `BREAK TEMP SET <ADDR>` / `BREAK TEMP CLEAR` / `BREAK TEMP LIST`

Controls the existing temporary execute breakpoint used for run-to-cursor,
step-over, and step-out style tools.

### `BREAK STEP [COUNT]`

Runs for a bounded number of AVR instructions and then stops with a debug break.
`COUNT` defaults to 1.

### `BREAK STEP_CLEAR`

Cancels a pending instruction-step countdown.

## Watchpoints

Watchpoint commands are available when CUzeBox is built with debugger support.
They expose the existing debugger watchpoint engine through the automation API,
without changing the efficient AVR instruction path when no watchpoints are
armed. Watchpoint regions are intentionally limited to `SRAM` and `IO`, which
are the regions the core can observe directly at access time.

### `WATCH SET <SLOT> <SRAM|IO> <R|W|RW> <START> [END]`

Arms watchpoint slot `<SLOT>` for reads, writes, or both. Slots are zero-based
and currently range from 0 to 7. `END` is optional; when omitted, the
watchpoint covers one byte.

### `WATCH CLEAR <SLOT|ALL>`

Clears one watchpoint slot, or all watchpoints.

### `WATCH LIST`

Returns all currently armed watchpoints.

### `WATCH HIT [CLEAR]`

Returns the last watchpoint hit, including slot, region, access mode, address,
value, and AVR PC word address. Add `CLEAR` to consume the hit after reading it.

## Symbol access

### `READ_SYMBOL <name>`

Reads a symbol-backed value or address.

### `WRITE_SYMBOL <name> <value>`

Writes a symbol-backed value when the symbol resolves to writable data or I/O.

## Input control

### `SET_INPUT <PLAYER> <MASK> <FRAMES>`

Sets a controller override for a fixed number of frames.

### `CLEAR_INPUT <PLAYER>`

Clears a player override.

### `QUEUE_INPUT <player> <mask> <frames> [mask frames]...`

Queues a short scripted input sequence.

Useful for menus, deterministic title-screen navigation, and simple gameplay checks.

## Output and capture

### `SCREENSHOT [path]`

Captures the current screen image.

## Process control

### `QUIT`

Requests emulator shutdown.

## TODO

This page should later grow to include:

- exact request and response examples
- error responses
- symbol resolution details
- screenshot path behavior
- richer response examples

## In-memory screenshot and cycle-domain video inspection

### `SCREENSHOT_CAPTURE`

Captures the current logical CUzeBox GUI pixel buffer as a 24-bit BMP entirely
in memory and associates it with the requesting API client. The response
includes byte size, width, height, and format. No host screenshot file is
created.

### `SCREENSHOT_READ <OFFSET> [LEN]`

Reads bytes from that client's in-memory screenshot. `LEN` is bounded to 4096
bytes per request. This is intended for streaming a browser download without a
transient file.

### `SCREENSHOT_CLEAR`

Releases the requesting client's in-memory screenshot buffer. Disconnecting the
client also releases it automatically.

The older `SCREENSHOT [PATH]` command remains the explicit host-file screenshot
operation.

### `VIDEO_BEAM [START] [COUNT]`

The default/read form returns the live cycle-domain beam state. Debugger/API
builds derive the current cycle from the absolute AVR cycle and a line-start
timestamp updated at row-producing sync falls. This avoids an always-running
per-cycle debugger counter.

When PORTC capture is armed, the response additionally contains the executed
portion of the current line as BBGGGRRR bytes. With capture off, `pixels` is
empty and `valid_cycles` is zero, but the live `cycle`, `row_pulse`,
`line_start_abs`, sync pulse counter, 1820-cycle geometry and 299-1738 active
window remain valid. A raw cycle greater than 1820 is reported rather than
clamped so late scanlines remain visible.
