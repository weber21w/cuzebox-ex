# GUI preference persistence

Preferences use the commented `key=value` file `config.cfg` in the application's
working directory. **Save** writes immediately; ordinary preference edits are
also saved on normal exit. **Reload** reads the saved values; **Defaults** changes
the live preferences and marks them modified until saved. Recent ROM changes
save immediately. Crashes or forced termination can lose edits not yet saved.

| GUI settings | Storage |
| --- | --- |
| Video layout, fullscreen, limiter, frame merge, TV sync PLL, rendering and filters | `config.cfg` |
| All audio controls, including volume, output rate, latency, resampling, filters, stereo width and reverb | `config.cfg` |
| GUI gamepad controls, pause behavior, virtual keyboard, theme identity and colors | `config.cfg` |
| On-screen message visibility and log verbosity | `config.cfg` (`SystemMessages`, `LogVerbosity`) |
| Screenshot INCLUDE GUI checkbox | `config.cfg` (`ScreenshotIncludeGui`) |
| Input routing, mappings, virtual devices, multitaps, mouse and haptics | `config.cfg`; input profiles also store game-specific input settings |
| Physical controller identities, left-stick enable and dead zone | `config.cfg` (`ControllerIdentity0..7`, `ControllerLeftStick0..7`, `ControllerDeadzone0..7`) |
| Emulator keyboard and controller shortcuts | `config.cfg` (`Shortcut0..22`); edited through TOOLS → SHORTCUTS |
| SD timing/access, SPI RAM, ESP policy and bootloader preferences | `config.cfg` |
| ROM/state/screenshot/controller database paths and recent ROM list/freeze | `config.cfg` |
| Video capture filename, automatic numbering, reset-first and OMIT MESSAGES | `config.cfg` (`VideoDumpOmitMessages`, default 1) |
| Input recording Compact / Full Debug mode | `config.cfg` (`InputMovieCompact`, default 1) |
| Netplay timing, relay/interface, player mask/count, ROM transfer options, name and pad labels | `config.cfg` |
| ESP/serial/TCP/MIDI preferences | ESP section of `config.cfg` |
| Cheat master enable and automatic load/save | `config.cfg`; cheat entries use cheat files |
| Debugger breakpoints, watchpoints and inspection settings | Existing debugger profiles |

Running capture, connection/lobby state, emulator pause state, trace activity,
open GUI pages and pending file-dialog text are session state. Apply/submit a
text field before saving where the GUI provides an Apply action.

Tools > Dump Video **OMIT MESSAGES** defaults on, including for old configs
without `VideoDumpOmitMessages`. It records the game buffer before emulator
text and notices are drawn. Screen messages keep their normal visibility and
lifetime; the global System Messages setting is independent. Unchecking this
option records the existing on-screen notices. It can be changed while
recording and takes effect on the next rendered recording frame. Replay-to-video
uses the same preference. Video dimensions, sampling, frame rate and audio
remain unchanged; screenshot scaling and bypass preferences do not apply.
The web Controls page exposes the same setting. API clients use
`EMU_SET video_dump_omit_messages 0|1` and read it through `EMU_STATUS`.

The Devices **FAST FLASH** checkbox defaults on and persists as `FastFlash`.
Its temporary acceleration state is not saved and does not change the saved
frame limiter or audio preferences. See the README for timing and audio effects.

The persistence fixes preserve the selected audio scaling preference even when
the frame limiter temporarily disables it, protect deferred display edits from
runtime capture, and add missing theme/netplay serialization and dirty tracking.
Both config writers check stream errors and replace the old file only after the
temporary file closes successfully. Failed saves retain the previous config.

JSON would provide standard tooling and nested objects, but would require a
coordinated migration of application, ESP and profile readers, plus a legacy
import path. The existing commented format remains appropriate for this change;
changing syntax alone would not fix missing fields or incorrect capture logic.

See `tests/preferences/README.md` for automated coverage and limitations.

`SavePath` is the save-state directory itself (default `savestates`). Slots are
written to `SavePath/<sanitized ROM name>/slot_N.czs`; the configured path is
used directly for relative and absolute paths. For example, the default is
`savestates/Game/slot_0.czs`, without a second `savestates` component.
Slot checks and failed loads create no directories; directories are created
only when saving. Loading and slot indicators also check the old
`SavePath/savestates/<ROM>/slot_N.czs` location when the corrected file is
missing. Existing old saves are preserved without moving or rewriting them.
A corrected-path save takes priority; unreadable or invalid corrected-path
files report failure rather than silently restoring an older nested save.

The resident bootloader's GAME/MENU choice belongs to the emulated EEPROM,
not `config.cfg`: byte `0x16` is `1` for GAME and `0` for MENU. Fresh EEPROM
starts with a standard Uzebox header and GAME selected; complete existing
`eeprom.bin` images, including their boot choice and game saves, are preserved.
The bootloader toggles this choice with B and saves it when programming a game.
EEPROM is stored beside the loaded ROM (shared by games in that directory);
browser builds persist that file in IndexedDB with the other user files.

SD timing defaults new preferences to `.UZE HINT` (`SdTimingPreset=4`). The
selection remains HINT when a ROM resolves it to SLOW/NORMAL/FAST; saving does
not replace that policy with the effective preset. Existing manual settings
are preserved. UZE byte `0x19A` values 1/2/3 select SLOW/NORMAL/FAST; absent or
unknown hints fall back to NORMAL. HEX programs can request the same presets
with `EmulatorHint(EMU_HINT_SD_TIMING, value)`; they start at NORMAL until a
valid hint executes. Reset clears the runtime hint without changing preferences.

Interlace defaults to ROM hints for both enablement and presentation:
`InterlaceMode=0`, `InterlacePresentation=0`. Unhinted ROMs remain off;
forcing interlace on without a presentation hint selects FIELD. Explicit
OFF/ON and FIELD/WEAVE overrides survive ROM changes and preference reloads.
See [video settings](video-filters.md#interlaced-video) for byte `0x19C` encoding.
