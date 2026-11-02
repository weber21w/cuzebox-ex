# Screenshot output

Native CUEX defaults to **PNG, game only, BYPASS FILTERS enabled**. This applies
to the screenshot shortcut, automatic `SCREENSHOT` files, default
`SCREENSHOT_CAPTURE` API output, and Tools > Screenshot with Include GUI off.
The web tools download PNG with `image/png` and a `.png` filename.

## Cycle capture

`frame.c` retains palette indices from the latest completed field, before
horizontal sampling, display filters, CRT effects and frame merging. A bounded
frame from a program with imperfect sync can also be retained. A debugger stop
mid-field keeps the preceding complete image; capture never advances emulation.

- The active interval is 1,440 CPU cycles starting at cycle 299, matching the
  renderer's cycle-domain extraction window. At default X=1440 every cycle
  becomes one column. Smaller widths resample this complete interval.
- By default progressive source rows repeat five times, without interpolation. Thus a
  224-row capture is 1440x1120. The current renderer accepts up to 225 visible
  rows, normally producing 1440x1125. Dimensions follow captured row bounds;
  there is no mode-name lookup or fixed assumption of 224 rows.
- Legitimate black rows and columns are preserved. No content-based black
  trimming is used: blank game output is different from the emulator HUD.
- Interlaced fields weave before nearest-neighbor 5/2 vertical scaling.
  Alternating three/two rows retain both fields and the progressive aspect
  instead of doubling image height. A missing opposite field duplicates the
  available field, without blending.
- JAMMA rotation/mirrors apply to the image canvas, including portrait output;
  screenshots do not acquire landscape letterboxing.
- Reset and state restore invalidate cached fields. Until a new frame completes,
  cycle capture returns a black image rather than another game's stale pixels.

Three bounded palette-index banks use about 950 KiB. Collection is one memcpy
per visible scanline; CPU instruction/pixel macros are unchanged. RGB expansion
and PNG compression run only when requested. This is additional frame-renderer
work; the earlier UART-only performance comparison does not measure it.

## Scale, presets and sampling

Tools > Screenshot and the web Controls page provide independent **X WIDTH**,
**Y SCALE**, **SAMPLE X** and **PRESET** dropdowns. Defaults are CYCLE, 1440
(1X), 5X and NEAREST. A preset changes only X and Y; either axis and sampling
remain editable. The preset is derived from the current pair, becoming CUSTOM
when no preset matches. These preferences persist with the normal config.

| Preset | X width | Y scale |
| --- | ---: | ---: |
| CYCLE (default) | 1440 | 5 |
| RAW | 1440 | 1 |
| WIDTH320 | 320 | 1 |
| WIDTH256 | 256 | 1 |
| WIDTH240 | 240 | 1 |
| WIDTH192 | 192 | 1 |
| WIDTH160 | 160 | 1 |

X choices include 1440, 2880 (2X), 720, 480, 384, 376, 360, 352, 320, 288,
256, 240, 224, 192 and 160. The API additionally accepts any integer width
1–2880. Y choices are integer 1X–10X. Width is the exported full-window width,
**not a detected native game resolution**. A mode's actual active drawing may
occupy only part of the window. No mode detection or black-edge trimming occurs.

- NEAREST uses source column `floor(x * 1440 / output_width)`. It preserves
  palette values but downsampling can lose thin details and alias dithering.
- AREA averages RGB over `[x*1440/W, (x+1)*1440/W)`, weighting partial cycles
  by overlap. This is useful for shrinking unusual cycle timings or dithering.
- LINEAR samples at pixel centers, `(x+0.5)*1440/W-0.5`, blending adjacent
  RGB values and clamping edges. It can soften edges; unlike AREA it does not
  integrate all cycles in a wide downsampling interval.

RGB averaging is in encoded channel values, rounded to nearest integer; it
does not apply gamma correction. At width 1440 all three are exact identity.
Vertical scaling is always nearest-neighbor. Progressive height is source
rows times Y. Interlaced height uses the same formula, with Y/2 per woven row;
Y=1 retains only one parity under nearest sampling, so use Y>=2 to retain both.
Scale is applied before JAMMA rotation, followed by mirrors in rotated space.

Config keys are `ScreenshotXWidth=1440`, `ScreenshotYScale=5`,
`ScreenshotSampling=0` (0=NEAREST, 1=AREA, 2=LINEAR), alongside
`ScreenshotBypassFilters=1` and `ScreenshotIncludeGui=0`. Invalid loaded scale
values fall back to defaults. Older configs inherit these defaults.

Scale/sampling controls apply to cycle captures with BYPASS FILTERS enabled.
Filtered display export, explicit BMP and full-GUI saves use their actual
rendered dimensions; their metadata says `scale_applied:0`. Include GUI only
affects Tools > Screenshot saves; API, web download and shortcuts stay game only.

## Interpreting screenshots in AI agents

`SCREENSHOT_CAPTURE` returns a `capture` JSON object frozen with the image.
The identical JSON is embedded in PNG's `tEXt` chunk, keyword **CUEX.Capture**.
With Pillow, read `json.loads(image.info['CUEX.Capture'])`. Both web servers
also send this object in the `X-CUEX-Capture-Metadata` download header. PNG
pixel dimensions alone cannot establish game resolution or cycle accuracy.

The schema `cuex-shot-1` specifies:

| Field | Meaning |
| --- | --- |
| path | cycle, display, gui, or legacy_bmp |
| filters_bypassed / scale_applied | Whether raw cycle capture and export scaling were used |
| source_valid | Cycle cache has a completed field; zero means black fallback, not observed game output |
| source_cycle_begin / source_width_cycles | 299 / 1440 for cycle captures; zero otherwise |
| source_row_first / source_rows | First pulse-counter row (normally 19) and field-row span, including black rows |
| source_fields | 1 progressive or 2 woven; unavailable opposite field duplicates the available field |
| requested_x_width / requested_y_scale / sampling | Saved settings, even when scale_applied is zero |
| x_scale_num / x_scale_den | Cycle-to-export X ratio, W/1440 |
| y_scale_num / y_scale_den | Woven-row-to-export Y ratio, Y/fields |
| pre_rotation_width / pre_rotation_height | Cycle canvas before JAMMA rotation, W by source_rows*Y; other paths report actual dimensions |
| width / height | Actual PNG/BMP dimensions after orientation |
| jamma_flags | Existing JAMMA bits: rotate90=1, rotate180=2, rotate270=4, flip H=8, flip V=16 |
| cycle_exact_x | One output column per cycle, only true for a cycle capture at width 1440 |

Only use the cycle-coordinate fields when `path == 'cycle'` and
`source_valid == 1`. Other paths return zero cycle geometry and scale ratios.
Undo mirrors first, then rotation, to map an output point to pre-rotation
coordinates `(x,y)`. For NEAREST, its cycle is
`299 + floor(x*1440/pre_rotation_width)`; its woven source line is
`floor(y*source_fields/requested_y_scale)`. The pulse row is
`source_row_first + floor(line/source_fields)` and interlace parity is `line%2`.
AREA/LINEAR output represents multiple source cycles; use the sampling formulas
above rather than treating it as one DAC sample. Capture returns the latest
completed field(s), so a breakpoint in the next partial field can precede the
current debugger CPU position. Repeated screenshots do not run the CPU.

Settings are emulator-wide and affect future captures; already captured bytes
and the returned capture metadata remain unchanged. Agents should keep the
capture response alongside downloaded bytes instead of reinterpreting an old
image using the latest settings.

## Options and compatibility

`ScreenshotBypassFilters=1` is the saved default. Disable BYPASS FILTERS to use
the filtered game image at display sampling/scaling. This path also removes
the emulator surround and portrait letterboxing. Existing config files that
lack the new key inherit the enabled default.

INCLUDE GUI remains off by default. In the GUI tool, enabling it explicitly
captures the completed GUI surface using display settings. The tool explains
that bypass does not apply to full-GUI output. API and shortcut defaults remain
game-only independently of this tool-specific override.

Explicit non-PNG file paths (including `.bmp` and extensionless names) and
`SCREENSHOT_CAPTURE BMP` preserve historical
rendered-layout BMP export, including the old HUD/fitting behavior. The default
API format is `png`; explicit BMP returns `bmp24`. Clients must use returned
dimensions/format. Existing screenshot read/clear commands are unchanged.

The dependency-free RGB PNG encoder uses PNG Up filtering and fixed-Huffman
DEFLATE with short-distance runs. It adds no libpng/zlib build dependency and
performs no color quantization. PNG CRCs use the existing CRC32 implementation.

File-writing screenshots remain disabled in the gameplay-only Emscripten build;
no replacement browser emulator binary was built for this change.

## Verification

From the source root, use a fresh object directory for each platform/flag set:

```sh
make -j2 -f tests/preferences/Makefile TSYS=linux \
  OBD=_obj_/shots OUT=_obj_/shots/cuzebox \
  all screenshot-test cycle-screenshot-test screenshot-tool-test preferences-test
python3 tools/test_screenshot_png.py _obj_/shots
python3 tools/test_screenshot_api.py _obj_/shots/cuzebox --rom /path/to/game.uze
python3 tools/test_api_multiclient.py
python3 tools/test_embedded_web_server.py
node tools/test_screenshot_web_controls.js
```

The cycle test checks every sample in a 1440x1120 fixture, fivefold repetition,
black game rows, partial-field isolation, filter independence, multiple heights,
rotation/mirror mappings, interlaced aspect and reset. Encoder cases include
one-pixel rows, high-entropy RGB, solid colors and long runs. The Python decoder
independently verifies PNG chunk CRCs, DEFLATE via standard zlib, and complete
RGB byte equality; it also uses Pillow as a second decoder when available.

The GUI test exercises actual controls, saved preferences and full-GUI opt-in.
Legacy BMP tests still cover 64 layout/rotation/mirror combinations. The API
test compares file and in-memory PNG bytes and verifies capture leaves a paused
game on the same frame, while retaining explicit BMP support.
It additionally checks 256/240/192/160 output, nearest/area/linear sampling,
independent axis overrides, invalid commands and matching PNG/API metadata.
The web tests cover settings forwarding, download metadata and controls-page
JavaScript preset/axis/sampling interactions.
