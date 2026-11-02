# Preference persistence regression checks

From the repository root, using the normal SDL2/MicroUI build configuration:

```
make -j4 -f tests/preferences/Makefile preferences-test
```

The test links the production application objects and includes `main.c` to
exercise the actual preference capture/apply functions and GUI setters. SDL
uses dummy audio/video and a software renderer. Test configs are written only
under `_obj_/preferences-run`; the repository's `config.cfg` is untouched.

Coverage:

- All 80 fields in `app_config_t` save and reload with nondefault values,
  including every GUI color, recent ROM, virtual device, binding, and multitap
  slot. String fixtures include spaces, equals signs and hash characters.
- All audio preferences survive GUI Save/Reload. Frequency scaling remains
  selected with the frame limiter disabled, while its effective runtime value
  is disabled. Monitor width and reverb are restored to the audio engine.
- Saving a deferred display edit retains the requested layout.
- Theme name, theme file, netplay player settings, transfer options, name and
  pad labels survive Save/Defaults/Reload. Timing edits mark the config dirty.
- ESP route/model/firmware, host serial, host MIDI, virtual MIDI and TCP serial
  preferences survive Save/Reload with the serial route disconnected.
- Old configs retain existing settings and use defaults for new fields.
- Logging verbosity and overlay visibility survive Save/Reload. INFO hides
  diagnostic output, Errors retains failures, Off silences messages, and Trace
  bypasses throttling. Repeated messages and bursts are suppressed at Info/Debug.
  A 128-sector write emits a completion summary, with no per-sector messages.
- FAST FLASH detects real SPM erase/write instructions, remembers operations
  completed between frames, stops on idle/reset/pause, respects external serial
  and video recording, and saves/reloads its checkbox preference.
- An invalid destination reports failure. On Windows, locking the destination
  forces replacement failure and verifies that the old file remains intact.

This verifies persistence, not sound quality or physical serial/MIDI hardware.

To check core preference saving with ESP and other optional features compiled
out, run `make -f tests/preferences/Makefile lean-preferences-test`. This small
test uses the real config reader/writer without the ESP serializer.

## Core, storage and cheat regressions

```sh
make -j2 -f tests/preferences/Makefile interrupt-test storage-test cheats-test
python3 tools/test_build_dependencies.py
python3 tools/test_debug_elf_bounds.py
```

`interrupt-test` runs both CPU decoders through pending/new interrupt timing,
SEI/RETI/CLI, multi-cycle calls, repeated SEI, SREG writes and return chaining.
It also checks vector priority, EEPROM modes/enable windows and partial updates
to two-word instructions. `storage-test` checks CRC equivalence, reusable movie
buffers, malformed chunks and snapshot bounds before CPU mutation. Pass
`--benchmark` to its executable for three 32 MiB CRC comparison trials.

`storage-test` also exercises save-state paths: one default directory level,
absolute custom roots with spaces and a trailing separator, read-only slot
queries, legacy nested-slot loading, corrected-path priority, and rejection
of a corrupt current save without falling back to stale data.

The interrupt regression also checks PINx toggle semantics and that `OUT PINC`
affects the pixel on its own cycle, matching `OUT PORTC`, in both decoders.

`video-messages-test` requires FFmpeg and records three-frame clips with and
without emulator notices, while checking that messages remain on screen.
`python3 tools/test_video_messages.py <OBD>` independently decodes both clips
and verifies recorded pixels, frame counts and the mono 48 kHz audio stream.
The screenshot tool regression also clicks the Dump Video OMIT MESSAGES box;
the preference regression checks its saved value. Existing input-playback
coverage checks replay-to-video frame count and finalization.

`cheats-test` clicks the real MicroUI Save button, reloads its edited value,
round-trips 16/32-bit values and comparisons, and checks legacy byte defaults.
On Linux it also verifies that a failed write leaves the dirty flag set.
Test files stay in the respective `$(OBD)/*-run` directories.

The dependency test uses a fake compiler to verify no-op builds, flag changes
and `.inc` edits. ELF bounds tests compile both real readers and accept optional
`EXTRA_CFLAGS`, for example `-fsanitize=address,undefined` on Linux.
