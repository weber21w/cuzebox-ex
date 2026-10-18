# Input movies

CUzebox records versioned **CZINPUT v1** movies in `.cic` files
(**CUzeboxex Input Capture**). Existing CZINPUT movies named `.cap` still open;
renaming those to `.cic` is sufficient. Raw legacy Uzem `.cap` files are rejected:
they contain only per-frame P1 buttons, with no starting state or ROM/settings
identity. A lossless automatic conversion cannot reconstruct that missing data.
The file signature identifies the format, regardless of its extension.

## Recording and playback

Open **Tools -> Input Files**. Select an output file, choose **RESET BEFORE
RECORDING** to begin from ROM reset, or clear it to begin at the current frame
boundary. Click **RECORD INPUT**, then **STOP** to finish and flush the movie.

Select a movie with the playback file picker and click **PLAY FROM BEGINNING**.
Playback always restores the embedded starting snapshot. The recording checkbox
only chooses how a new recording starts. A changed resident-bootloader setting,
EEPROM file or manual SD speed does not change the recorded starting state.
The loaded ROM must match the recorded file checksum and size.

**RECORD VIDEO** also writes video using Tools -> Dump Video settings. FFmpeg
must be available. Replay completion pauses emulation and finalizes its video
without adding an extra frame. Movie sessions temporarily bypass frame merging
to avoid carrying visual history from an earlier session. An unrelated video session must be stopped first.

Supported inputs:

- Pads and Super Mouse devices at either direct controller port.
- Independent P1/P2 multitaps, with four slots per port (eight total).
- Keyboard dongle emulation and keyboard virtual devices, including tap slots.
  Extended make/break sequences, empty polls and command responses are recorded.
- Hot swapping: attach/remove either tap, replace slot device types, or enable
  the keyboard while recording. Timed topology events reproduce those changes.
  Slots keep stable P1/0–3 and P2/0–3 addresses; they are never renumbered.
  The game decides which logical player number belongs to each port/slot.

Packets are captured when the emulated game latches them, after host mapping and
mouse tuning. Multiple latches in one frame retain their individual CPU-cycle
timestamps. Playback supplies the recorded packets and keyboard responses, so
live gameplay input cannot change them. Keyboard host events are ignored during
replay; GUI controls remain available. The legacy direct mouse path is not
supported: configure a virtual Super Mouse instead.

## Starting state and storage

Every movie embeds the initial CPU/flash/EEPROM, SPI RAM, SD controller, virtual
FAT and controller/keyboard state. Reset recordings store the resulting reset
snapshot too, including the actual resident bootloader. The active SD timing
model is stored separately and applied during playback; the user's previous
model and legacy input enables are restored afterward.

SD data is recorded lazily: the first access to each 512-byte sector embeds its
initial contents. Recording and playback use a private block overlay; SD writes,
file creation, deletion and renaming never reach the host game directory.
Playback therefore does not need the original external SD files. The ROM-file
checksum includes any data appended to the UZE.

Touched sectors are cached in memory, up to **8 MiB of sector data** per session.
Exceeding that limit stops the session with an error. The file also contains an
uncompressed snapshot (whose size depends mainly on SPI RAM) and timed events.
It does not buffer video frames.

Generated ten-minute/60 Hz runs with one keyboard poll per frame and
occasional chat measured:

| Controller traffic | File size, before additional SD data |
|---|---:|
| Four payload packets/frame, no tap-control pulses | 8.40 MiB |
| Eight payload packets/frame, no tap-control pulses | 13.89 MiB |
| Full UzeTap slot-selection protocol, two slots per port | 38.61 MiB |
| Full UzeTap slot-selection protocol, four slots per port | 85.30 MiB |

The full wire measurements are the relevant estimates for games that select tap
slots every frame. Control pulses also latch controller packets, so they are
recorded too. Actual sizes depend on the game's polling strategy. These samples
include a roughly 0.71 MiB starting snapshot and no SD sectors; add the sectors
a game uses (up to the 8 MiB cache limit) and any larger SPI RAM snapshot. Busy
keyboard polling loops add more data than occasional chat text. Each topology
change adds 208 bytes. Version 1 currently writes uncompressed events.

Movie sessions also suppress automatic EEPROM/code-ROM dump writes. This stays
in effect after Stop until a ROM is successfully reloaded, preventing replayed
save data from later leaking into the user's normal saves. Reload the ROM to
resume normal persistent storage. Private SD changes are discarded on Stop.

For the proposed smaller recording mode and its distinction from Full Debug, see
[Compact / Full Debug design notes](input-capture-modes.md). Neither mode selection
nor compression is implemented yet.

## Performance

A Windows native/headless measurement on this development machine ran 600
MEGADOOM frames after 2,000 warm-up frames, restoring the same initial state for
each case. Three runs measured 1.402–1.414 seconds without capture,
1.383–1.398 seconds recording and 1.381–1.410 seconds replaying. The differences
are within benchmark noise; this sample does not establish a measurable
slowdown. Starting a recording or preflighting/restoring playback took about
0.10 seconds. This is one game and machine, without video encoding, and does
not establish performance on handhelds or under heavy multitap/keyboard/SD use.

Ongoing work includes packet/chunk CRCs, frame checksums over CPU memory and
registers, topology checks, and buffered file I/O. Playback preflights the whole
file before starting, so startup cost grows with recording size. Recording ROM
checksums and the initial snapshot are also startup costs. The SD overlay grows
only with unique sectors touched, up to the documented limit. No movie session
means no movie file I/O or per-frame movie checksums.

The measured 600-frame file was 4,855,174 bytes. An offline fast zlib compression
experiment reduced it to 141,490 bytes, including its snapshot. This illustrates
repetition in this particular sample; it is not an implemented compressed mode
or a general compression-ratio guarantee. Current recordings remain the full,
uncompressed format. A future compact option can preserve exact input timing
while compressing repeated events and reducing optional frame-state diagnostics.

## Validation and limitations

Playback validates the complete file, CRCs, clean end marker, ROM identity and
snapshot layout before restoring the machine. It checks event order, port/slot,
cycle timing, frame counts, CPU memory/register checksums and the SD write stream
while running. A mismatch stops and pauses playback with a desynchronization
message rather than silently continuing.

Snapshots currently use CUzebox's existing savestate ABI. Movies require a
compatible emulator build/architecture; v1 is not a promise of portable states
across future core changes. Networked games, external serial/MIDI input and
other external nondeterministic devices are not supported movie sources. Stop
netplay and external sessions first. Manual ROM resets/reloads and savestate
loads end the active movie. Playback controls the recorded topology and restores the user's pre-playback
virtual-device/tap configuration afterward.

The 2026-10-15 code review corrected SEI/RETI interrupt delays, interrupt priority
and EEPROM behavior. Movies recorded before those corrections can desynchronize
despite passing the existing ABI check. Re-record affected movies with the
corrected core. The CRC and buffer optimizations do not change CZINPUT v1 bytes.

## Headless/API use

These commands work in the regular and headless executables when input capture
and replay are compiled in:

```text
INPUT_MOVIE RECORD RESET C:/captures/game.cic
INPUT_MOVIE RECORD HERE C:/captures/from-here.cic
INPUT_MOVIE STOP
INPUT_MOVIE PLAY C:/captures/game.cic
INPUT_MOVIE STATUS
```

The remainder of a command is the path, so spaces are supported without quoting.
Record and Play resume emulation. Pause before Stop when inspecting the exact
final machine state. Use `RUN_FRAMES` or frame advance to control recording.

The binary layout is described in [input-movie-format.md](input-movie-format.md).
