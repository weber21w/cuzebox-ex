# GUI preference persistence

Preferences use the commented `key=value` file `config.cfg` in the application's
working directory. **Save** writes immediately; ordinary preference edits are
also saved on normal exit. **Reload** reads the saved values; **Defaults** changes
the live preferences and marks them modified until saved. Recent ROM changes
save immediately. Crashes or forced termination can lose edits not yet saved.

| GUI settings | Storage |
| --- | --- |
| Video layout, fullscreen, limiter, frame merge, rendering and filters | `config.cfg` |
| All audio controls, including volume, output rate, latency, resampling, filters, stereo width and reverb | `config.cfg` |
| GUI gamepad controls, pause behavior, virtual keyboard, theme identity and colors | `config.cfg` |
| On-screen message visibility and log verbosity | `config.cfg` (`SystemMessages`, `LogVerbosity`) |
| Input routing, mappings, virtual devices, multitaps, mouse and haptics | `config.cfg`; input profiles also store game-specific input settings |
| SD timing/access, SPI RAM, ESP policy and bootloader preferences | `config.cfg` |
| ROM/state/screenshot/controller database paths and recent ROM list/freeze | `config.cfg` |
| Video capture filename, automatic numbering and reset-first option | `config.cfg` |
| Netplay timing, relay/interface, player mask/count, ROM transfer options, name and pad labels | `config.cfg` |
| ESP/serial/TCP/MIDI preferences | ESP section of `config.cfg` |
| Cheat master enable and automatic load/save | `config.cfg`; cheat entries use cheat files |
| Debugger breakpoints, watchpoints and inspection settings | Existing debugger profiles |

Running capture, connection/lobby state, emulator pause state, trace activity,
open GUI pages and pending file-dialog text are session state. Apply/submit a
text field before saving where the GUI provides an Apply action.

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
