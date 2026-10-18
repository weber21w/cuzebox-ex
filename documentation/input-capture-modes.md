# Input capture modes: Compact and Full Debug

**Status: proposal, 2026-10-15. Neither a mode selector nor a compressed stream is
implemented.** Current `.cic` files use the full, uncompressed CZINPUT v1 format
in [the format reference](input-movie-format.md). See [usage](input-files.md).

## Intended user experience

Tools -> Input Files would offer **Recording mode: Compact / Full Debug**.
Compact would be the normal gameplay choice; Full Debug would retain detailed
checks for emulator development and desync investigation. The selected mode
should persist like other GUI preferences. The file declares its mode so playback
automatically chooses the correct decoder/checks; users should not have to select
playback mode. Existing v1 files remain playable.

This is a proposal for the next optimization pass, not a promise of an existing
GUI control or final wire format. The current writer is equivalent to the planned
Full Debug baseline, though that label is not present in the GUI yet.

## Fidelity and safety shared by both modes

Both must preserve actual CPU-cycle input timing, multiple latches within a frame,
eight stable port/tap slots, Super Mouse packets, keyboard commands/responses,
hot swapping, topology reset events, ROM identity and a reproducible initial
state. Both retain private SD data/writes and a clean completion marker. Replay
must ignore live gameplay input, restore temporary user settings, stop exactly
at EOF and finalize optional video without an extra frame.

Compact must not mean sampling each controller once per video frame. That loses
information if a game polls twice, selects tap slots within a frame, or consumes
mouse movement/keyboard replies between polls. Repeated equal packets can be
encoded compactly only if their event order and timestamps are reconstructible.

The initial snapshot cannot simply disappear: reset mode can still depend on
EEPROM, flash/bootloader contents, SPI RAM and SD state. Compacting the existing
snapshot is separate from designing a portable snapshot schema.

## Proposed differences

| Concern | Compact | Full Debug |
|---|---|---|
| Input fidelity and topology events | Exact | Exact |
| ROM/state validation and private SD | Required | Required |
| Event encoding | Batched, delta/repeat encoded, optionally compressed | Detailed events; compression may still be offered |
| CPU memory digest every frame | Optional or periodic checkpoints | Every frame, as current v1 |
| Event timing/address validation | Retained | Retained with maximum diagnostic context |
| Diagnostic reporting | Clear stop reason and last checkpoint | Precise event/frame mismatch information |
| File size goal | Small gameplay recordings | Prioritize inspection/desync investigation |

File-integrity CRCs and bounded parsing remain in both modes. Reducing optional
CPU-state checks changes how quickly internal emulator divergence is detected;
it does not justify accepting malformed files or skipping ROM verification.
Removing frame CPU hashes alone will not account for the large savings: repeated
latches, tap control traffic, empty keyboard polls and snapshots are major sources
of repetition. SD-write integrity/isolation should remain in both profiles.

## Candidate implementation

1. Benchmark current writer/reader on representative ROMs, full tap traffic,
   busy keyboard polling and SD workloads before selecting an encoding.
2. Start with bounded block compression of the existing logical stream. It can
   reduce file size while preserving existing events and checks, and is easier
   to compare against v1 than a new input sampling model. Evaluate a small portable
   codec and build/dependency/code-size costs; no codec has been selected.
3. Consider delta timestamps and repeated-packet encoding only if block
   compression leaves a meaningful size/CPU opportunity. Preserve exact cycles,
   including 32-bit CPU-cycle wrap and long runs.
4. Add mode/version declarations and a backwards-compatible reader. Do not
   silently reinterpret old v1 chunks. Decide whether a new major version or
   explicitly versioned blocks/flags are appropriate before writing files.
5. Separate the parser/codec from emulation hooks. Reuse bounded read buffers,
   keep blocks small enough for handhelds, and avoid buffering the whole movie.
6. Only then add the GUI/API recording selector, persisted preference, docs and
   format specification. Playback remains automatic from the file header.

Preflight must bound compressed and expanded lengths, expansion ratio/memory,
block counts and arithmetic. Reject truncated/corrupt data before applying its
state where possible; preserve chunk CRC and nested snapshot validation. Include
write/flush failures and partial recordings in tests. A codec is not a reason to
weaken corruption detection or the 8 MiB touched-sector cache limit.

## Measurements already available

On this Windows development machine, three headless MEGADOOM trials measured
600 frames after 2,000 warm-up frames:

- Capture off: 1.402–1.414 seconds.
- Recording: 1.383–1.398 seconds.
- Playback: 1.381–1.410 seconds.
- Recording startup / playback preflight and restore: about 0.10 seconds each.

These show no measurable slowdown above noise for this one workload, not zero
cost on every machine. No real-time video encoding was running. A 4,855,174-byte
sample compressed offline with zlib level 1 to 141,490 bytes. This 2.9% result
illustrates potential; current playback cannot read that compressed data.

The full uncompressed protocol can produce tens of MiB over ten minutes; see the
[usage page's traffic-specific measurements](input-files.md#starting-state-and-storage).
Test gameplay, tap-heavy, keyboard-heavy and SD-heavy recordings separately.

## Acceptance criteria

- Compact and Full Debug reproduce identical final CPU/SD/input states for the
  same logical session, with exact frame/video counts.
- Round trips cover eight pads/mice, two taps, keyboard slots, empty polls,
  multiple latches per frame, hot swaps and resets with unchanged topology.
- Embedded state/ROM checks, SD isolation and post-stop save protection survive.
- Old CZINPUT v1 `.cap` and `.cic` files play, raw legacy captures remain clearly
  distinguished, and unknown modes/versions fail with a useful message.
- Both normal and compile-time headless APIs and GUI/video playback pass.
- Corrupt lengths/CRCs, oversized expansion, truncation, missing END, cycle/frame
  mismatch and recording I/O failure have meaningful coverage.
- Measure file size, peak RAM, startup time and emulation throughput on a desktop
  and ideally RGB30/ROCKNIX. Document test conditions, not just percentage claims.

## Legacy conversion is separate

Raw legacy captures store only two bytes of P1 buttons per frame. They cannot
provide a known starting state, ROM checksum or the other input channels. A
best-effort import would need a user-selected ROM and explicit reset/bootloader/
EEPROM/SD assumptions, run the old stream and record a new `.cic`. It should write
a new file, preserve the original, and clearly label provenance/uncertainty.
This was considered too much complexity for the user's likely usage and has not
been implemented. Already-versioned CZINPUT files named `.cap` need only renaming.
