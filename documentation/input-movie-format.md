# CZINPUT v1 binary format

All movie-container integers are unsigned little endian, independent of C struct
packing. New files use `.cic` (CUzeboxex Input Capture). Existing CZINPUT files
named `.cap` are also accepted; renaming them does not change the format. Raw legacy captures
have no CZINPUT signature and are rejected.

The 16-byte file header is eight bytes `CZINPUT\0`, a u32 version (`1`), and a
u32 header size (`16`). Each chunk has a four-byte ASCII tag, u32 payload length,
u32 IEEE CRC32 of its payload, then the payload. CRC32 uses polynomial
`0xEDB88320`, initial value `0xFFFFFFFF`, and final inversion. Payload lengths
are bounded at 32 MiB. Unknown required tags are rejected.

The first chunks must be META and STAT, followed by timed chunks and one END!.
EOF without END!, any trailing data, CRC failure or malformed length is invalid.
A reader scans/validates the complete file before modifying the live machine.

## META (104 bytes)

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | Savestate ABI signature |
| 4 | u32 | CRC32 of the complete loaded ROM file |
| 8 | u64 | ROM file byte length |
| 16 | u32 | CRC32 of initial 64 KiB emulated flash, including bootloader |
| 20 | u32 | Start mode: 0=current state, 1=reset then snapshot |
| 24 | 8 × u32 | SD model: preset, init ms, command wait bytes, read wait bytes, write busy ms, CS-high ms, min/max initialization byte cycles |
| 56 | u32 | Reserved, zero |
| 60 | 2 × u8 | Multitap presence for P1 and P2 |
| 62 | 2 bytes | Reserved, zero |
| 64 | 8 × u8 | Slot types, P1 slots 0–3 then P2 slots 0–3: 0=none, 1=pad, 2=Super Mouse, 3=keyboard |
| 72 | 32 bytes | Reserved, zero |

When no ROM path exists, the ROM identity is the CRC32 of current 64 KiB flash,
with length zero. Slot topology and starting EEPROM are also stored in STAT.
The ABI signature covers the relevant runtime struct sizes, integer and pointer
sizes, and native byte order. It is a compatibility check, not a core-behavior
version guarantee; changes to core semantics may require a future format version.

The 2026-10-15 interrupt/EEPROM corrections leave this v1 container and native
state layout unchanged, but can desynchronize older recordings. The ABI signature
does not identify those semantic changes; affected recordings need to be remade.
The CRC implementation now uses a 16-entry nibble table with identical results.

## STAT

An uncompressed snapshot produced by `savestate_capture_to_mem`. Its internal
layout is native to the savestate ABI, unlike the portable container. The reader
requires compatible sizes and validates nested block lengths before restoring.
This captures CPU/flash/EEPROM, SPI RAM, SD/VFAT, ESP and input state. Live host
handles are sanitized on restore. External networking/serial activity is not a
supported movie source.

## Timed records

Every timed payload begins with a u64 elapsed CPU-cycle count. It starts at zero
at the snapshot and extends the core's wrapping 32-bit cycle counter to 64 bits.
Events at the same cycle retain file order. Frame numbers are relative to the
movie, independent of the GUI's frame counter.

| Tag | Payload bytes | Fields after timestamp |
|---|---|---|
| LACH | 28 | u8 port (0–1), u8 slot (0–3), u8 bit count (1–64), u8 probe flag, u64 low-active serial shift packet, u64 current frame index |
| TOPO | 196 | u64 current frame index, 180-byte topology/protocol description |
| KEYB | 16 | u8 port, u8 slot, u8 keyboard command, u8 response, u32 current frame index |
| SECT | 524 | u32 SD sector number, 512 initial data bytes |
| FRAM | 24 | u64 completed-frame count, u32 CPU-state digest, u32 running SD-write digest |
| END! | 16 | u64 completed-frame count |

LACH captures the packet at the actual latch, including a pending multitap probe
prefix. It bypasses host packet construction during replay. KEYB captures each
keyboard protocol response, including zero/empty responses and device queries;
legacy dongles and tap-routed keyboards share this boundary.

TOPO is emitted when the emulated device configuration changes, before the
next affected packet, keyboard response, SD operation or frame boundary. Its
180-byte description contains two u8 tap-presence flags, u8 legacy-keyboard
enable, u8 legacy-mouse enable (must be zero), eight u8 virtual-device IDs
(255=unassigned), and sixteen 8-byte device entries. Each entry contains u8 used,
u8 type, u32 options, u8 haptic enable and u8 haptic bus ID. IDs are internal;
input addresses remain the fixed physical port/tap slot. The last 40 bytes
contain two sets of three u32 values (selected slot, probe pending, last activity
cycle), followed by four u32 tap-controller values (state, arm pulses, command
pulses, target mask). These also preserve GUI preset resets which leave the same
device types connected but reset protocol progress. Host bindings are not
needed to replay replacement devices. Guest-controlled slot selection and probe
state continue to run through the normal emulated bus protocol.

SECT appears on first read or write of a sector. Subsequent reads/writes use the
private cache. It embeds only sectors actually used and has no dependency on
host file ordering at playback. The current implementation limits the cache to
16384 sectors. Sector writes never call the backing VFAT mutation path.

FRAM is emitted after each `frame_run`, before audio/video submission. The CPU
digest starts with CRC32(SRAM), then runs the same CRC update (without another
final inversion) over I/O registers, EEPROM and the little-endian u32 PC. The SD
digest starts at `0xFFFFFFFF` and updates over each write's little-endian u32 byte
address and u8 value, without final inversion. It detects different writes even
if the final sector contents happen to agree.

END! is emitted when recording is stopped cleanly. Playback checks it before
executing another frame, so input replay can produce video with an exact ending.
