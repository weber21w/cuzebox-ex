# Screenshot checkpoint, November 1, 2026

Local changes only. Active emulator source is
`C:/cuzebox-dev-green-led/cuzebox-ex-main/`. The Git root is its parent,
with origin `https://github.com/weber21w/cuzebox-ex.git`; the nested active
source is untracked there. Do not stage/reset the parent tree wholesale.

The PNG work includes independent X/Y dropdowns, presets, three horizontal
sampling methods, saved preferences, API settings and geometry, web controls,
and PNG metadata for AI/image consumers. Defaults: CYCLE=1440/5, NEAREST,
BYPASS FILTERS on, Include GUI off. See [the screenshot contract](screenshots.md)
and the source tree's `documentation/api/commands.md` for usage and mapping.

`screenshot-cycles.patch` is against this machine's local source immediately
before the screenshot change, **already containing the UART TXC fix and other
local work**. It is not a patch against a verified fresh GitHub checkout.
It excludes generated `web_assets.h`; run `python3 tools/web_assets_to_c.py`
after applying. The unchanged legacy screenshot test is supplied by the base.
The preference regression's stale fast-flash helper names and API fake stubs
were also updated to match this local source before tests could run.

Inspect the patch and run `git apply --check` against a suitable source tree
before applying. Preserve existing changes. The manifest records SHA256 of
normalized UTF-8 source text before/after each changed file, not filesystem
line-ending bytes. Copying the complete active source is the easiest migration.
Windows objects/executables cannot serve as Linux build inputs.

Validation completed on Windows MinGW and Ubuntu 22.04/WSL:

- Full emulator builds; cycle screenshots, actual native dropdown interactions,
  config round trips, full-GUI opt-in, and 64 legacy BMP orientation/layout cases.
- Independent Python zlib/CRC and optional Pillow decode; complete RGB fixture
  equality, legitimate black guest rows, interlace, partial-field isolation.
- Owned paused UNOS sessions: file/API bytes and embedded metadata agree;
  256/240/192/160 widths and three sampling choices; no frame advancement.
- API multi-client regression; embedded web settings forwarding and PNG/BMP
  downloads, including metadata headers; controls-page JavaScript interaction test.
- Earlier storage and interrupt regressions also passed with the screenshot
  cache; the AVR core is unchanged by this screenshot change. Original UART
  performance figures do not include the new per-scanline screenshot cache.

`native-api-result.json`, `linux-api-result.json`, test summaries and artifact
hashes provide the results. The included PNGs are example UNOS desktop exports.
Matching Windows/Linux default PNG hashes demonstrate identical output for
the paused fixture. Build and test commands are in `screenshots.md`.

No browser/WASM emulator binary was rebuilt or deployed. The native emulator's
embedded web tools and the Python web bridge have been updated. No GitHub push
or uzenet.us deployment was performed. UNOS client source was not changed.

This folder is also copied into the active UNOS directory at
`reference/cuzebox-screenshot-cycles/` for the Linux handoff.
