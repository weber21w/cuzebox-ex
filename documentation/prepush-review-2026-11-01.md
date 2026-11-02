# CUEX pre-push review: November 1, 2026

Compared with freshly fetched [GitHub main bd553618efb1401972a7d99e282a72d1e8b8921d](https://github.com/weber21w/cuzebox-ex/commit/bd553618efb1401972a7d99e282a72d1e8b8921d).
The remote HEAD was checked again during review and had not changed.

## Scope and preservation

The [complete file inventory](prepush-review-20261101-inventory.json) records every
candidate input file, its baseline/candidate hashes, disposition and review note.
All 441 upstream files are present. Before these two review artifacts, the
candidate contained 495 files: 369 byte-identical, 72 modified and 54 added.
Unchanged files were verified by identity; every changed/new runtime, library,
build and tool file was reviewed. Generated embedded web assets were verified
against their HTML source. Validation evidence and documentation were classified
separately from executable code.

The interface comparison found no removed API command names (96 retained),
native UI interface functions (290 retained), named configuration keys (82),
GUI windows (38), or existing build flags (34). This checks interface presence;
behavioral regressions are additionally exercised by the tests below.

## Findings corrected

1. **Generated files would enter a source push.** The unignored `build/` tree
   contained 118 generated files, including runtime configs and ROM fixtures.
   It is now ignored; the source audit also rejects tracked build/dist data.
   All 118 originals remain unaltered on disk.
2. **Portable firmware pins failed after an LF checkout.** Seven manifest
   hashes depended on CRLF. `snapshot.json` now declares `sha256-lf`, the test
   normalizes line endings and verifies complete source/header coverage.
   All 27 vendored portable files match the matching firmware workspace after
   normalization. This corrects provenance without changing the portable code.
3. **Extensionless screenshot filenames stopped working.** Explicit non-PNG
   filenames retain their original BMP export; automatic/default PNG behavior
   is unchanged. A real API regression failed before the fix and now passes on
   Linux and Windows.
4. **A legacy ESP save-state fixture no longer linked.** It now supplies the
   capture-reset hook and asserts that restoration clears stale pixels.
5. **Pre-existing Linux test defects hid validation.** The whisper fixture now
   supplies its diagnostic clock; replay-to-video tests and CI use the actual
   `.MP4` spelling. The whisper link failure was reproduced on GitHub's baseline.
   Normal CI now includes portable adapter, PNG decode/settings, clean-video
   decode and the native hardware regression checks.

## Intentional behavior changes

The user explicitly selected **Timer1/GPIO enabled by default as the new native
standard**. The distinct strict replay behavior identifier is accepted; no
compatibility distribution or runtime exception was added. Hardware-disabled
builds were exercised only as audit comparisons. Emscripten retains its separate
browser-overlay defaults.

Other requested defaults remain: game-only PNG, cycle width 1440 and Y scale 5,
BYPASS FILTERS on, and recording OMIT MESSAGES on. Explicit BMP export, filtered
captures and opt-in messages/GUI remain available. Distribution API defaults off;
existing explicit API/headless controls remain available. SavePath writes use the
corrected directory while legacy nested saves remain readable.

## Validation completed

- Linux full application, lean gameplay and compile-time headless builds.
- Full headless/diagnostics/logical/movie/game-runner validation and runtime paths.
- Native interrupt/UART/PINx, Timer1/GPIO, preferences, video settings, interlace,
  file picker, eight controllers, shortcuts, cheats, input playback/movie/storage,
  screenshots, headless devices, ROM loading, whisper and source stepping.
- Independent PNG CRC/DEFLATE/RGB checks and actual FFmpeg video/audio decode.
- ESP AT, IPv4/IPv6 TCP, MQTT/WebSocket/reconnect, legacy saved ESP state, SoftAP,
  UART polling/pacing, writable SD, bootloader, API concurrency and debugger tools.
- Twenty portable protocol tests against the actual vendored CUEX sources using
  the matching firmware's host fixtures: wire/session, relay/direct/replacement,
  redundancy, game/pacing/OOB, snapshot/stream extensions, endpoint/drop-in/live,
  burst/services/client-pad policy/wide inputs, round transitions and scores.
- Chromium smoke: SDL2 startup/audio, frames, pause/step, HEX upload, MicroUI audio
  setting and IndexedDB reload. Screenshot web controls/source stepping also pass.
- Windows distribution build, runtime paths, headless API and PNG settings/capture.
- Three ROM startup comparisons (180 frames): MegaBomber, SFA2 and SpiRamTiles.
  Rendered BMP hashes matched GitHub in both compared configurations. With matching
  legacy flags, CPU register/cycle observations also matched. These are startup
  smoke checks, not full gameplay runs or a statistically controlled benchmark.
- Source audit and release-tool checks. Full local logs/scripts are under
  `_obj_/prepush-review/`; they are excluded from publication.

## Remaining limits and publication state

No remaining failure was found in the completed local checks. This does not
establish that every possible game, controller, network or saved input works.
Mac runner compilation/packaging and real Mac desktop/physical serial/controller
checks have not run; Mac output remains an experimental test target. The hosted
GitHub workflows have not run for this candidate yet.

The isolated review checkout is `_obj_/prepush-review/candidate/`, branch
`codex/prepush-review-20261101`, based on the commit above. The unrelated dirty
parent checkout was not reset or staged. No push, tag, PR, release publication or
production-server change occurred. Earlier pre-review download checksums do not
identify this corrected source; use regenerated packages and their manifests.
