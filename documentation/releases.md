# Native downloads and writable data

The native release workflow is `.github/workflows/release.yml`. A manual run
builds downloadable Actions artifacts. Pushing a `v*` tag builds the same matrix
and creates a **draft** GitHub release only after every package job succeeds.
Draft publication is a separate review step. This workflow does not merge PRs.

The reusable `.github/workflows/macos.yml` also runs Mac builds/tests on pushes
to `main` and `codex/**`, pull requests, or manual dispatch. The release workflow
calls the same jobs, so test artifacts and release artifacts use the same recipe.
Both Apple Silicon and Intel jobs run the native core/GUI regressions, independent
PNG/video decoding, a packaged headless session, and an actual Cocoa-window
launch of the extracted app. The bundle signature and its bundled-library paths
are checked again after launch. Runner audio uses SDL's dummy driver; actual
audio devices/controllers/serial hardware still require desktop testing.
Bundle creation, packaged headless recording, and Cocoa launch are separately
timed stages. FFmpeg is cached by architecture, SDK/x264 version and the pinned
recipe; the cache is saved after a successful helper build even if a later test
fails. Build/FFmpeg/package diagnostics are retained as separate `debug-macos-*`
artifacts, including on failure. Release collection includes only `cuex-*`
package artifacts, so diagnostic logs do not become release downloads. The collector
accepts differing gzip streams only when their uncompressed CUEX source TAR
bytes match exactly. It verifies each original manifest hash and rewrites the
final SHA256SUMS to the selected shared archive. Different source payloads or
binary assets still stop release assembly.

| Target | Artifact | Initial support |
| --- | --- | --- |
| Windows x86-64 | ZIP, required DLLs and FFmpeg | Windows 10+, portable by default |
| Linux x86-64 | AppImage and relocatable tar.gz | Ubuntu 22.04/glibc 2.35 baseline and newer compatible systems |
| macOS arm64 | CUEX.app in an unsigned-test ZIP | macOS 15+, requires a successful Mac runner build/test |
| macOS x86-64 | Separate Intel app ZIP | macOS 15+, requires a successful Mac runner build/test |

Flatpak is not part of this first release pipeline. PortMaster packaging remains
separate and keeps its existing launch behavior. No personal ROMs, live configs,
credentials, saves, EEPROM, SD directories or recordings are packaged.

## Storage modes

Unmarked developer/handheld executables retain the original working-directory
behavior. Existing commands, `config.cfg` files and relative paths keep their
meaning. Packaged executables identify their defaults with small marker files:

- `cuex-user-data` in the resource directory selects installed mode.
- `cuex-portable` beside the executable selects portable mode.
- Explicit command-line mode choices override these package markers.

Installed mode uses SDL2's `SDL_GetPrefPath("uzenet", "CUEX")` directory:
typically `~/.local/share/uzenet/CUEX/` on Linux,
`~/Library/Application Support/uzenet/CUEX/` on macOS, and
`%APPDATA%\uzenet\CUEX\` on Windows. This becomes the process working directory,
so relative settings, captures and FFmpeg temporary files are writable without
modifying an AppImage mount, signed app bundle or installation directory.

`config.cfg`, `roms/`, `savestates/`, `screenshots/`, `video/`, profiles and other
relative output paths live here. The package supplies clean defaults and theme
templates. They are copied with exclusive creation: existing config files and
user-edited themes are never replaced. Distribution builds default their
localhost API/web server off; the GUI setting or explicit headless/API flags
can enable it.

Portable Windows ZIPs keep data beside the executable. AppImage `--portable`
uses the original AppImage's directory, not its temporary mount. For macOS or
any read-only installation, `--data-dir` is the preferred way to choose a
portable writable directory outside the app bundle.

```text
cuzebox --user-data
cuzebox --data-dir /path/to/writable/CUEX
cuzebox --portable
cuzebox --data-dir /path/to/writable/CUEX --print-paths
cuzebox --data-dir /path/to/writable/CUEX --config /path/to/config.cfg game.uze
```

`--print-paths` initializes the selected storage mode and reports JSON with
`mode`, `config`, `data` and `resources`, then exits. `--resource-dir` overrides
the read-only template/resource source. These are native launch choices, not
saved preferences. `--help` lists them.

Command-line ROM and explicit config paths are resolved **before** changing
working directory. An explicitly loaded ROM's containing directory remains the
virtual SD root, including sibling files and subdirectories. Package/user-data
directories are not substituted for that root. With no ROM, installed/portable
packages expose their writable `roms/` directory to the built-in Bootloader 5.
EEPROM and game-side SD writes retain their existing ROM-adjacent semantics.

Old developer configs are not silently imported into an installed profile.
Use an absolute `--config` path or copy the desired config deliberately. Relative
paths inside that config are relative to the selected working directory; use
absolute SavePath/ROM/screenshot paths when retaining existing external data.
No old save directories are moved or deleted. The nested-state compatibility
fallback described in `preference-persistence.md` still applies.

## Building packages locally

Use separate object directories for platforms/profiles. Set `VER_DATE` to the
source/release date; CI derives it from the tagged commit. Distribution builds
use `EXTRA_CFLAGS=-DCUEX_DISTRIBUTION=1`.

```sh
make -j4 TSYS=linux OBD=_obj_/release OUT=_obj_/release/cuzebox \
  EXTRA_CFLAGS=-DCUEX_DISTRIBUTION=1 VER_DATE=0x20261101
bash packaging/release/build-ffmpeg.sh _obj_/ffmpeg
python3 tools/release_package.py build --platform linux --version preview \
  --binary _obj_/release/cuzebox \
  --ffmpeg _obj_/ffmpeg/prefix/bin/ffmpeg \
  --ffprobe _obj_/ffmpeg/prefix/bin/ffprobe --out dist/linux
```

The helper build needs a native C toolchain, make, pkg-config, nasm and x264
development files. It builds the pinned FFmpeg source with only CUEX recording,
decoding and verification components. AppImage tools/runtime and FFmpeg input
are SHA256-verified against `packaging/release/tools.lock.json`. If an upstream
continuous tool asset changes, verification fails; review the new official
asset and update the lock explicitly rather than disabling verification.

Linux uses linuxdeploy for shared-library closure and appimagetool with a pinned
runtime. Bundling does not remove the glibc/kernel/graphics-driver compatibility
baseline. The tar.gz fallback contains `CUEX/AppRun`; it requires no FUSE.
The AppImage can also use `APPIMAGE_EXTRACT_AND_RUN=1` on hosts without FUSE.

On Windows, build with MinGW64 and pass `--platform windows`, the `.exe` paths,
and `--dependency-dir C:/path/to/mingw64/bin`. The packager follows PE imports,
copies non-system DLLs, and fails if a required dependency cannot be resolved.
Recording requires an FFmpeg build with libx264 and AAC; the CI helper recipe
provides this. ffprobe is optional in packages, useful for diagnostics/tests.
Linux hosts with MinGW-w64 can also build the Windows helper with
`bash packaging/release/build-windows-ffmpeg.sh /tmp/cuex-win-helper`.
This cross-build pins and statically links x264, keeps its original license,
and adds the matching x264 source archive to the package output. Native CI
uses the platform's x264 development package instead.
The Windows and macOS CI profiles set `FLAG_REMOTE_ROMS_LIBCURL=0` and use the
OS-provided curl helper for remote ROM downloads, retaining the OS certificate
store instead of depending on a builder's OpenSSL/CA installation. Linux uses
bundled libcurl with the target distribution's standard CA certificate files.

On macOS, use native Apple Silicon/Intel runners and GNU make (Apple's bundled
make is too old for these build rules):

```sh
brew install sdl2 x264 pkg-config nasm make
export PATH="$(brew --prefix make)/libexec/gnubin:$PATH"
export MACOSX_DEPLOYMENT_TARGET=15.0
make -j4 TSYS=macos OBD=_obj_/release OUT=_obj_/release/cuzebox \
  FLAG_REMOTE_ROMS_LIBCURL=0 EXTRA_CFLAGS=-DCUEX_DISTRIBUTION=1 VER_DATE=0x20261102
bash packaging/release/build-ffmpeg.sh _obj_/ffmpeg
python3 tools/release_package.py build --platform macos --arch "$(uname -m)" \
  --version preview --binary _obj_/release/cuzebox \
  --ffmpeg _obj_/ffmpeg/prefix/bin/ffmpeg --ffprobe _obj_/ffmpeg/prefix/bin/ffprobe
```

The packager builds CUEX.app, follows Mach-O
dependencies, rewrites library references into Contents/Frameworks, includes
helpers/resources, creates an icon and Info.plist, and ad-hoc signs the result.
When the SDK supplies sdl2-compat, its dynamically loaded SDL3 runtime and its
dependencies are also bundled; installed Homebrew libraries are not required.
It does **not** use Developer ID or notarize. Public supported Mac releases
should add Developer ID signing, hardened runtime and notarization, and be
tested on actual Mac desktops/controllers/serial devices. The first pipeline
produces explicitly labeled unsigned test packages, not a Universal 2 build.

## Provenance and verification

Every package includes `BUILDINFO.json`: version, architecture, source-file
hashes/tree hash, input executable/helper hashes and helper version. Input
hashes precede dependency relocation and signing; SHA256SUMS verifies the final
downloads. A Git revision
is recorded only when the source directory itself is the Git root. An unrelated
parent checkout is never presented as the revision of the active nested source.
The CUEX source archive is deterministic across the build matrix; conflicting
same-name artifacts cause collection to fail instead of overwriting sources.
SHA256SUMS files cover download artifacts. The pinned FFmpeg source archive is
also included when the helper was built by the provided recipe. Dependency
notices accompany the app; SDK/runtime libraries retain their own licenses.

```sh
python3 tools/test_runtime_paths.py _obj_/release/cuzebox
python3 tools/test_headless_api.py _obj_/release/cuzebox
python3 tools/test_release_tools.py
python3 tools/test_release_package.py --linux dist/linux/*.AppImage
python3 tools/test_release_package.py --windows dist/windows/*windows*.zip
python3 tools/test_release_package.py --macos dist/*macos*.zip
python3 tools/test_macos_app.py dist/*macos*.zip
```

Package smoke tests start extracted payloads with a clean PATH, create a PNG,
record through the bundled FFmpeg, and confirm writable files stay in user data.
Runtime-path tests cover old launches, markers, preserved files, explicit config,
relative ROM arguments, save-state writes and the actual virtual SD sibling file.
Mac dependency traversal also has host-independent tests; these do not replace
running the macOS job. No macOS package should be advertised as tested until
the corresponding runner and subsequent desktop testing have succeeded.

## Local checkpoint: November 1, 2026

The Windows and Linux preview payloads are under `dist/windows/` and
`dist/linux/`, version `2026.11.01-preview`. Both were tested on this Windows
host and WSL Ubuntu 22.04, including runtime paths, API startup, PNG capture
and a real MP4 recording with a clean PATH. This does not establish desktop,
controller or serial-device compatibility across every distribution.
The macOS jobs have not run yet. No release or workflow was pushed or
published as part of this local checkpoint.
