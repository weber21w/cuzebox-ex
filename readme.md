# CUzeboxEx

CUzeboxEx extends CUzeBox, an Uzebox emulator written in C, with gameplay tools,
peripheral emulation, networking, and a development debugger. Native builds use
SDL2 and support Linux, Windows through MinGW, and macOS. An Emscripten target
is also available with a reduced feature set.

**Build it for the job.** The same source can produce a lean gameplay emulator
for lower-resource machines or a development build with debugging, tracing,
web tools, capture, and networking. Major optional features have compile-time
flags, allowing their code and diagnostic buffers to be omitted entirely.

Authors: Sandor Zsuga (Jubatian), Lee Weber (D3thAdd3r), and contributors.
Licensed under the [GNU GPLv3](LICENSE). See source headers for original
Uzem/CUzeBox credits.

## Documentation

Controller setup: **CONFIG → INPUT → CONTROLLER MAPPING...** supports eight
physical controllers, button learning, live input feedback, and a multitap on
either or both ports. See [Controller mapping](documentation/controller-mapping.md).

**Extensive documentation is available in [documentation/](documentation/).**
Start with the [index](documentation/index.md) or
[overview](documentation/overview.md). The topic guides cover capabilities,
configuration, workflows, and implementation details beyond this quick guide.

| Topic | Guides |
| --- | --- |
| Build options and development workflow | [Building](documentation/building.md) |
| Building cuzebox for handheld Linux devices using PortMaster  | [Handheld guide](documentation/handhelds-rgb30.md) |
| API capabilities, scripting and automated tests | [API overview](documentation/api/index.md), [command reference](documentation/api/commands.md), [testing](documentation/api/testing.md) |
| CPU/source debugging, symbols, watches and profiles | [Debugger](documentation/debugger.md), [web debugger and diagnostics](documentation/web-debugger.md) |
| Input capture playback and video export | [Input files](documentation/input-files.md) |
| Timer1 compare behavior and emulation limits | [Timer1](documentation/timer1.md) |
| Advanced API inspection | [Run control](documentation/api-run-control.md), [breakpoints](documentation/api-breakpoints.md), [watchpoints](documentation/api-watchpoints.md), [memory snapshots](documentation/api-memory-snapshots.md), [memory scans](documentation/api-memory-scans.md), [profiler](documentation/api-profiler.md), [stack](documentation/api-stack.md) |
| Controllers, UzeTap, UzeBus and virtual devices | [Input topology](documentation/input-topology.md) |
| Multiplayer gameplay | [Netplay](documentation/netplay.md) |
| UART, serial links and MIDI | [Serial and endpoints](documentation/serial.md), [ESP emulation](documentation/esp8266-emulation.md) |
| Display, sound and external memory | [Video filters](documentation/video-filters.md), [sound options](documentation/sound.md), [SPI RAM](documentation/spiram.md) |
| Cheats and saved settings | [Cheats](documentation/cheats.md), [preference persistence](documentation/preference-persistence.md) |

The included `mkdocs.yml` also supports serving the documentation locally with MkDocs.

## Features

Availability depends on the selected build flags and platform.

### Gameplay and hardware

- Cycle-based AVR execution, Uzebox video and audio, EEPROM persistence, and
  flash self-programming for bootloader software.
- Load `.uze` and Intel HEX `.hex` ROMs; browse local files and recent games.
  Optional Remote ROMs support browses and downloads games from a configured host.
- Save-state slots, screenshots, SRAM cheats with value search, and optional
  input recording/replay and video recording.
- Built-in Bootloader 5, an optional external boot-section override, and
  resident bootloader support.
- Virtual SD storage backed by host files, including nested directories and
  controlled file/folder creation, resizing, renaming, and deletion.
  SD timing presets and custom timing values support compatibility testing.
- SPI RAM selected by a ROM-header hint or manual override, up to 8 MiB.
- SNES pads, mouse/SuperMouse and lightgun-style devices, keyboard dongles,
  JAMMA display rotation, UzeTap slot selection, UzeBus traffic, and haptic routing.
- Virtual input devices with host bindings, button remapping, mouse tuning,
  presets, and per-ROM input profiles.

### Display, sound and interface

- Lightweight render paths and an optional staged pipeline with analog
  prefilters, Scale2x/HQ2x/xBR2x scaling, and CRT-style effects.
- Fullscreen, frame limiting, and frame merging.
- Configurable audio rate, latency, sample format, resampling, DC blocking,
  low-pass filtering, stereo simulation/width, reverb, and master volume.
- MicroUI menus for games, configuration, devices and tools, with themes,
  gamepad navigation, an optional virtual keyboard, and pause-on-menu behavior.
- Saved preferences in `config.cfg`, including audio, themes, paths, input,
  netplay and serial settings.

### Networking and peripherals

- Rollback netplay with relay room codes, public-room browsing, direct UDP,
  lobby readiness and seats, adjustable input delay/prediction, and ROM sync.
- ESP8266/ESP32-style module selection and ESP-AT emulation, legacy and ESP-AT
  2.3 firmware profiles, and optional SoftAP/LAN overlay helpers.
- UART profiles and routes for an emulated ESP module, host serial, raw TCP
  serial, host MIDI, virtual MIDI, and loopback.
- Serial links between instances through local TCP auto-pairing, LAN discovery,
  or internet relay rooms. Serial links exchange bytes independently of
  rollback netplay.
- Transport counters, serial/network traces, and a bad-connection simulator
  for testing latency, stalls, noise and dropped bytes.

### Debugging, web tools and automation

- CPU/register/memory inspection, annotated disassembly, source/symbol import,
  breakpoints, read/write watchpoints, stepping, step-over/out, and per-ROM
  debugger profiles.
- Optional memory history, instruction/beam correlation, raster timing tools,
  scanline profiling, SD protocol tracing/fault injection/record-replay, and
  cycle-correlated audio tracing.
- Embedded localhost web pages for controls, debugging, SD tools, audio
  analysis, serial tools, and network diagnostics.
- A TCP API for ROM loading, run control, state/memory inspection, debugging,
  peripheral configuration, and scripted regression tests. The
  [API command reference](documentation/api/commands.md) describes the full
  command surface.

## Building

Use a compiler supporting GNU C17, GNU Make, and the development libraries for
your target. Select features in [Make_config.mk](Make_config.mk);
[Make_defines.mk](Make_defines.mk) translates them into compiler/platform
options. Command-line assignments override the file settings.

The default is `TSYS=auto`. With the compiler and development libraries installed,
build the native executable with:

```sh
make
```

This selects Linux (`cuzebox`), macOS (`cuzebox`, using Clang), or Windows
MinGW/MSYS (`cuzebox.exe`). WSL builds a Linux executable. Use
`make show-build-config` to see the resolved target, compiler, output and object
directory. The default feature set includes the development tools; use the flags
below for a lean gameplay build.

Explicit `TSYS` assignments still select a target, including cross builds:

| Target | Example | Requirements |
| --- | --- | --- |
| Linux | `make -B TSYS=linux` | C compiler, Make, SDL2 and ALSA development libraries. SDL2 detection uses `sdl2-config` or `pkg-config`. |
| Windows | `make -B TSYS=windows_mingw` | MinGW toolchain, Make and matching SDL2 development libraries. Runtime DLLs must be beside the executable or on `PATH`. |
| macOS | `make -B TSYS=macos` | Xcode Command Line Tools, Make and SDL2. Detection supports `sdl2-config`, `pkg-config` and Homebrew. `TSYS=osx` is an alias. |
| Linux AArch64 | `make -B TARGET_LINUX_AARCH64=1` | AArch64 cross compiler and target SDL2/ALSA libraries; configure `CROSS_COMPILE` and target `pkg-config` paths as needed. |
| RGB30 / ROCKNIX | `sh tools/build_rgb30.sh` | Lean standalone AArch64 gameplay preset; Windows/WSL wrapper: `./tools/build_rgb30.ps1`. See the [handheld guide](documentation/handhelds-rgb30.md) for dependencies and the ROCKNIX integration template. |
| PortMaster | `make -B TARGET_PORTMASTER=1 package-portmaster` | AArch64 dependencies; produces a handheld package. `make TARGET_PORTMASTER=1 portmaster-doctor` checks the setup. |

Use `CCOMP`, `CCNAT`, `CC_INC`, `CC_LIB`, `SDL2_CONFIG`, or `PKG_CONFIG`
overrides when dependencies are outside standard locations.

RGB30/AArch64/PortMaster presets select Linux even on a Windows or macOS build
host; supply the appropriate cross toolchain and target libraries. A conflicting
explicit `TSYS` is rejected. Browser builds remain explicit with
`TSYS=emscripten` and require the Emscripten SDK. Auto-detection selects the
native target; it does not install compilers or dependency libraries.

Remote ROMs can use libcurl. Native builds fall back to an external `curl`
helper if libcurl development files are unavailable; set
`FLAG_REMOTE_ROMS_LIBCURL=0` to select that fallback explicitly. Cross-builds
requesting libcurl need the target library. Video capture needs external
`ffmpeg` with H.264/`libx264` and AAC support.

**Rebuild when changing feature flags or targets.** Command-line flag changes
are not tracked as file dependencies. The examples use `-B` to rebuild all
objects; alternatively, clean before rebuilding.

### Lean gameplay builds

For local gameplay, omit the debugger, API/web tools, filters, recording,
netplay, ESP/serial backends and Remote ROMs while keeping the native GUI,
sound, controllers, SD support and save states:

```sh
make -B TSYS=linux FLAG_DEBUGGER=0 FLAG_API_SERVER=0 FLAG_DISPLAY_FILTERS=0 FLAG_VCAP=0 FLAG_ICAP=0 FLAG_IREP=0 FLAG_NETPLAY=0 FLAG_ESP=0 FLAG_REMOTE_ROMS=0 FLAG_GUI_VKEYBOARD=0
```

Use `TSYS=windows_mingw` or `TSYS=macos` for the other native targets. Keep
features required by your games: ESP-dependent software needs `FLAG_ESP=1`;
online multiplayer needs `FLAG_NETPLAY=1`.

To trim the overlay too, add `FLAG_MICROUI=0` to the same command. This suits a
launch-and-play setup using a ROM argument, keyboard shortcuts and `config.cfg`.
Leave MicroUI enabled for its file browser, save-state controls and settings
windows.

| Flag set to `0` | What it omits |
| --- | --- |
| `FLAG_DEBUGGER` | Debugger support and compiled AVR debugging hooks; associated history/trace features are also excluded. |
| `FLAG_API_SERVER` | The automation API and embedded web tools. |
| `FLAG_DISPLAY_FILTERS` | Optional filter processing and filter controls. |
| `FLAG_VCAP` | Video recording. |
| `FLAG_ICAP`, `FLAG_IREP` | Input capture and replay; replay requires capture support. |
| `FLAG_NETPLAY` | Netplay and its UI. |
| `FLAG_ESP` | ESP emulation and associated serial/network endpoint backends; also disables SoftAP. |
| `FLAG_ESP_SOFTAP` | SoftAP/LAN overlay helpers while retaining the base ESP backend. |
| `FLAG_REMOTE_ROMS` | Remote game browsing/downloading. |
| `FLAG_MICROUI` | Native overlay and its windows. |
| `FLAG_GUI_VKEYBOARD` | On-screen keyboard; text entry uses a physical keyboard. |

Trimming features can reduce code size, diagnostic storage and optional
processing. Actual speed and memory savings depend on the game, host and
features removed; basic AVR execution and video/audio work remain.

### Keep debugging, trim diagnostics

To retain the debugger without its optional history/capture facilities, use:

```make
FLAG_DEBUGGER=1
FLAG_MEMORY_TRACE=0
FLAG_BEAM_CAPTURE=0
FLAG_BEAM_HISTORY=0
FLAG_SD_TRACE=0
FLAG_SD_FAULT=0
FLAG_SD_REPLAY=0
FLAG_AUDIO_TRACE=0
```

Set these in `Make_config.mk` or pass them to Make. They control memory history,
per-cycle beam capture, instruction-to-raster history, SD transactions, SD
faults/replay, and native DAC tracing. Their defaults follow `FLAG_DEBUGGER`.

`FLAG_HEADLESS=1` supports scripted runs without a window, host audio or input,
for automation/server/bot workflows. `FLAG_NOCONSOLE=1` removes ordinary
console output; `FLAG_PRINTF_WHISPER` controls guest whisper-port output
separately (headless builds always retain it). Guest writes to AVR data-space
address `0x39` print a two-digit hexadecimal byte; writes to `0x3A` print a
character. The corresponding `OUT` I/O operands are `0x19` and `0x1A`. Whisper output
goes to stdout independently of the on-screen message and log verbosity settings.

### Runtime choices for lower-resource machines

Build flags remove optional facilities; runtime choices also affect playback
cost. In `CONFIG > VIDEO`, try classic rendering or the potato path, disable
filters, and turn off frame merging for games that do not need it.
`RenderPath` values are `0` potato, `1` classic 1x, `2` classic 2x and `3`
staged; filters apply only to staged rendering.

For audio, use 44.1/48 kHz rather than 96 kHz, hold/linear rather than cubic
resampling, and mono with reverb off. Normal or safe latency can help avoid
underruns on a busy machine. See [sound options](documentation/sound.md) for
quality/latency tradeoffs. Saved runtime preferences take precedence over
applicable built-in defaults.

### Emscripten

The browser target uses SDL2 with MicroUI enabled by default. The native API
server, debugger/traces, networking, filters and native capture/replay are off;
save states, audio, controllers and the virtual SD filesystem remain available.

```sh
source /path/to/emsdk/emsdk_env.sh
make TSYS=emscripten -j2
python3 -m http.server 8000 --bind 127.0.0.1 --directory _obj_/emscripten
```

Open `http://localhost:8000/cuzebox.html`, choose **Load ROM**, then **Enable
audio**. No game files are required to build. Preferences and user files persist
in the browser's IndexedDB storage. WSL can build this target for a Windows
browser. See [the browser build guide](documentation/emscripten.md) for defaults,
optional bundled games, storage limitations and the browser regression test.

## Running and preferences

Launch with a ROM path:

```sh
./cuzebox roms/game.uze
```

On Windows:

```powershell
.\cuzebox.exe roms\game.uze
```

Without a ROM argument, the emulator starts its built-in Bootloader 5 image.
A configured external bootloader can override its boot section. The virtual
SD card is composed from the loaded game's directory.

For quiet AI/API testing, use the same full-featured executable in runtime
headless mode:

```powershell
.\cuzebox.exe --headless --paused --api-port 7432 roms\game.uze
```

`--headless` creates no window, GUI, controller devices, audio device, or
embedded HTTP server. It enables the TCP API even when the saved preference
is off. Guest video/audio, source stepping, breakpoints, traces, and memory
access remain available. Screenshots render on request; source audio
is retained in a bounded ring, and output audio processing runs only while
`AUDIO_SCOPE ENABLE` is armed. Headless screenshots omit the visual debug HUD.
Use `RUN_FRAMES`, `STEP_FRAME`, or debugger steps to inspect a paused session.

Without `--paused`, headless emulation runs at approximately 60 Hz regardless
of the saved limiter setting. `--unthrottled` explicitly removes this limit
and can use a full CPU core; paused sessions still sleep. FAST FLASH is not
used in headless mode. `--api-port` selects a port for this process only;
omitting it uses the configured API port. These switches do not alter saved
preferences. `--help` lists the options. Use a build with API/debugger support
enabled to retain the full debugging command set.

**FAST FLASH** in the Devices settings is on by default. On desktop it runs
emulated frames faster than 60 Hz during flash page erase/write activity,
then restores normal pacing after a frame without programming activity.
AVR instruction and flash timings are preserved. Audio is muted during the
speedup, and CPU use can increase. Netplay, video recording, and external
serial/MIDI routes keep normal pacing. The preference is saved as `FastFlash`
in `config.cfg`; it also works with external bootloader images.
Bootloaders that depend on live network response timing may need this off.

In MicroUI builds, use the top bar to load games, select recent ROMs,
configure devices, save/load states, and open tools. Settings live in
`config.cfg` in the working directory. **Save** writes immediately; edits
are also saved on normal exit. **Reload** restores saved values, and
**Defaults** changes current settings until saved. Input/debugger profiles
and cheat files retain their own per-ROM data.

For quiet gameplay, **CONFIG > DISPLAY > ON-SCREEN MESSAGES** hides the
bottom-left notices. **LOG VERBOSITY** selects Off, Errors, Info (default),
Debug or Trace. Info keeps file write/create summaries and useful notices;
routine diagnostics require Debug. Repeated messages and bursts are limited,
and file writes are summarized on close/flush rather than logged per sector.
See [Messages and debugging output](documentation/logging.md) for details.

**Tools > Screenshot > Include GUI** is a saved checkbox. Unchecked captures
the game area; checked captures the complete rendered GUI after drawing.
The tool shows the last saved filename. GUI-time notices do not queue an OSD
backlog, and keyboard presses belong to the GUI while its windows are open.

**Tools > Input Files** selects and plays `.cic` captures, with **Reset Before
Recording** enabled by default and an optional **Record Video** checkbox. Playback
pauses at the end and finalizes its video. The same tool can record new input
files. CZINPUT v1 movies include a starting snapshot, ROM checksum, eight
pad/Super Mouse slots, keyboard protocol responses, cycle-timed events and
private SD data. Legacy raw captures are rejected. See
[Input movies](documentation/input-files.md) for compatibility and storage details.

Extended SD file/folder changes require `SdAllowNewFiles=1`, exposed by the
GUI's SD access option. Enable it for guest software whose file changes you
intend to allow. See [preference persistence](documentation/preference-persistence.md)
for storage locations and save behavior.

## Controls

Use **TOOLS → SHORTCUTS** to remap keyboard combinations and controller
buttons/combinations for emulator commands. Bindings persist across restarts;
see [Shortcut configuration](documentation/shortcuts.md). The function keys below
describe the defaults.

The default SNES keyboard layout is:

| Key | Controller input |
| --- | --- |
| Arrow keys | D-pad |
| Q / W | Y / X |
| A / S | B / A |
| Enter / Space | Start / Select |
| Left Shift / Right Shift | L / R |

Hold either Alt key to direct keyboard pad input to Player 2. Input configuration
provides host bindings, presets and remaps; legacy SNES/UZEM mapping and
one-/two-player allocation remain available.

| Key | Emulator action |
| --- | --- |
| Esc | Exit |
| F1 | Allow keyboard-dongle detection again |
| F2 | Toggle legacy small-display mode |
| F3 | Toggle game-only layout |
| F4 | Toggle frame-rate limiter |
| F5 | Toggle video recording, when built in |
| F6 | Cycle legacy mouse sensitivity through enabled/disabled states |
| F7 | Toggle frame merging |
| F8 | Toggle SNES/UZEM keyboard mapping |
| F9 | Pause/resume |
| F10 | Advance one frame |
| F11 | Toggle fullscreen |
| F12 | Toggle legacy one-/two-player controller allocation |

Keyboard-dongle capture passes host keys to the guest. **Left Ctrl+F1** releases
capture when needed. See the [input guide](documentation/input-topology.md)
for mouse/lightgun tuning, UzeTap slots and virtual-device routing.

## Web tools and API

With `FLAG_API_SERVER=1` and runtime `ApiServer=1`, the emulator starts its
embedded localhost API and web server. No separate Python process is needed.

Default endpoints:

- API: `127.0.0.1:24680`
- Web tools: `http://localhost:24681/`
- Pages: `/controls`, `/debugger`, `/sd`, `/audio`, `/serial`, `/network`

Use the native Tools menu to open the browser tools. Occupied ports cause
later port pairs to be selected; startup messages show the actual ports.
Debugger-specific commands also require debugger support.

See [web tools](documentation/web-debugger.md) and the
[API documentation](documentation/api/index.md) for file transfer, event
streams, command capabilities and automation examples.

## Bootloader and recording notes

**RESIDENT BOOTLOADER** is enabled by default (`ResidentBootloader=1`). UZE
loads retain the built-in boot section for games using Bootlib or
hardware-style boot/reset behavior. HEX loads instead behave like ISP
chip erase/programming: flash starts erased, only the HEX contents are
installed, and games start at their application reset vector even when they
extend into the boot section. A bootloader-only HEX starts in the boot section.
Switching between UZE and HEX requires no preference changes.
`ResidentBootloaderFile`
selects an optional 4 KiB override; the built-in image remains available if
that file is absent.

Generated bootloader and web assets are checked in. Normal builds consume
them without requiring Python. Maintainers can explicitly refresh or verify
them with:

```sh
make regen-bootloader
make check-bootloader
make regen-web-assets
```

These maintenance commands need Python and the corresponding source assets.

With `FLAG_VCAP=1`, use F5 or **DUMP VIDEO** to record. Temporary lossless
output is encoded to a 720p H.264/AAC MP4 with `ffmpeg`. **RESET FIRST**
starts recording from the first frame after a ROM reset. Failed encoding
retains temporary output and error logs.

## Checks and development

Focused tests live under [tests/](tests/) and API/web-tool checks under
[tools/](tools/). Examples:

```sh
make -f tests/preferences/Makefile preferences-test
make -f tests/preferences/Makefile lean-preferences-test
make api-multiclient-check
make embedded-web-check
make debug-source-check
make debug-dwarf-check
make audio-scope-check
make sd-trace-check
```

The preference test uses the normal SDL2/MicroUI development configuration.
Individual checks may need Python, a compiler, an emulator instance or
specific build features. See the [API testing guide](documentation/api/testing.md)
and the relevant test scripts for setup.

Before publishing changes, run `python3 tools/source_audit.py` and follow
[the developer handoff](OPTIMIZATION_HANDOFF.md). Local preferences, game files, saves,
captures, build outputs and backups are ignored; generated bootloader/web
source headers remain included. GitHub Actions runs Linux full/lean builds
and focused regressions. PortMaster packaging uses clean distribution defaults.

## Developer handoff

For optimization or GitHub preparation, start with
[OPTIMIZATION_HANDOFF.md](OPTIMIZATION_HANDOFF.md). It records completed changes,
validation, open correctness issues and optimization candidates. The
[Compact / Full Debug capture proposal](documentation/input-capture-modes.md)
is a design for future work; current captures use the full uncompressed format.
