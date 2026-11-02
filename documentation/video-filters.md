# Video Filters

This page covers CUzeBox's host-side display filter pipeline: what each stage does, when it is active, and how the modes relate to the render path.

## Quick start

If you want crisp output with minimal extra processing:

- use `RenderPath=1` or `RenderPath=2`
- leave the filter modes at `none`

If you want the newer filter pipeline:

- use `RenderPath=3` (`staged`)
- then choose pre-filter, scale filter, and CRT filter modes as needed

This detail is important: **filters only apply in `RenderPath=3`**.

## Pipeline overview

The staged pipeline is conceptually:

1. render the emulator image to an internal 32-bit stage buffer
2. optionally apply a pre-filter
3. optionally apply a 2x scale filter
4. optionally apply a CRT-style post effect
5. present the final image

The implementation lives mainly in:

- `guicore.c`
- `filters.c`
- `filters.h`

## TV sync PLL (optional)

The Display **TV SYNC PLL** checkbox approximates a television's horizontal
sync recovery. It defaults **off**, including when an older config has no entry.
The setting is saved as `TvSyncPll=0` or `TvSyncPll=1` in `config.cfg`.
**PLL RECOVERY %** sets the recovery speed from **25% to 800%**. The default
is **100%**, the original response. Higher values settle faster and reduce the
initial skew; try **400%** when the original effect is too strong. At **800%**
the phase follows each edge immediately. This is a relative tuning control,
not a calibrated time constant in milliseconds. Changes apply during playback
without resetting the cartridge and discard the previous frame's blend history.
The value is saved as `TvSyncRecovery` and clamped to the supported range.

**PLL SKEW %** independently scales the visible horizontal displacement from
**0%** (no visible skew) to **100%** (full effect, the default). It does not
change the loop's phase, frequency, or recovery speed. Try **35%** strength
and **400%** recovery as a starting point, then tune each against your TV.
It persists as `TvSyncStrength`; API clients use `tv_sync_strength`.
The Video window fits the available screen height and scrolls when needed.

The web Controls page has the same controls. API clients can use:

```text
EMU_SET tv_sync_pll 1
EMU_SET tv_sync_recovery 400
EMU_SET tv_sync_strength 35
EMU_STATUS
CONFIG SAVE
```

`EMU_STATUS` returns `tv_sync_pll`, `tv_sync_recovery`, and `tv_sync_strength`; set it to `0` to return to the normal display.
Save, Reload, Defaults, and normal-exit persistence follow the other preferences.

Normally CUzeBox aligns each row directly to the cartridge's sync edge. With
this option enabled, a small fixed-point phase/frequency loop follows actual
PB0 falling edges. A sudden timing change can skew the picture horizontally
until the loop settles. Normal 1,820-clock lines and 910-clock vertical-sync
half-lines are accepted. This affects every render path, including Potato and
Classic, before frame merging and the staged filters. It also affects game
screenshots and rendered video; raw beam captures still show the cartridge's
unaltered output.

This is a horizontal timing approximation, not a calibrated TV or full analog
composite decoder. Different televisions have different loop responses. It
does not simulate vertical rolling, interlaced phosphor persistence, sync
amplitude, noise, or color-burst lock, and cannot prove how a particular TV will
respond. There is no artificial top-of-screen distortion: a stable sync stream
produces no displacement.

The loop runs once per sync falling edge, using integer arithmetic, and shifts
the existing row sampling window without an extra image pass or allocation.
When disabled, the loop is bypassed and the sampling offset is zero. The offset
is limited to 240 AVR clocks in either direction to keep every sampling window
inside the source row. Long sync gaps cause reacquisition. Toggle, power reset,
and savestate restore reset the presentation history; PLL state is deliberately
excluded from machine savestates and replay fingerprints. A restored screenshot
may therefore differ briefly while the presentation reacquires.

Validation: `python tools/test_tv_sync.py` exercises stable sync, half-lines,
phase recovery, frequency tracking, clock wrap, lost sync, and sampling bounds.
Preference round-trip tests cover the off default and saved setting.

## Pre-filter modes

Configured by `FilterPreMode`.

Available modes in the code:

- `0` = none
- `1` = soft RGB
- `2` = S-Video
- `3` = composite
- `4` = RF modulator

These modes soften or blend the base image before scaling and CRT effects. They are useful when you want the image to look less like raw square pixels and more like an analog video feed.

## Scale filter modes

Configured by `FilterScaleMode`.

Available modes:

- `0` = nearest
- `1` = Scale2x
- `2` = HQ2x
- `3` = xBR2x

These filters are intended for 2x enlargement. They affect edge smoothing and perceived pixel shape.

General guidance:

- `nearest` keeps the sharpest pixel look
- `Scale2x` is simple and often a good first try
- `HQ2x` is smoother and can look softer
- `xBR2x` tries harder to round and blend diagonal edges

## CRT modes

Configured by `FilterCrtMode`.

Available modes:

- `0` = none
- `14` = scanlines 12.5% (listed first in the menu, after NONE)
- `1` = scanlines 25%
- `2` = scanlines 37%
- `3` = scanlines 50%
- `4` = aperture grille
- `5` = shadow mask
- `6` = curvature
- `7` = composite
- `8` = composite + scanlines
- `9` = grille + scanlines
- `10` = mask + scanlines
- `11` = bump map
- `12` = lucid
- `13` = heatwave

The config comments intentionally expose the most useful presets directly. Internally, `guicore.c` also keeps CRT parameter fields such as scanline darkness, mask strength, curvature strength, composite strength, and vignette strength.

## Recommended combinations

### Sharpest

- `RenderPath=1` or `2`
- no filters

### Clean but slightly softened

- `RenderPath=3`
- pre-filter: soft RGB or S-Video
- scale: nearest or Scale2x
- CRT: none

### CRT-like

- `RenderPath=3`
- pre-filter: composite
- scale: nearest or Scale2x
- CRT: scanlines, grille, or shadow mask

### Stylized

- `RenderPath=3`
- scale: HQ2x or xBR2x
- CRT: lucid or heatwave

## Performance notes

The staged pipeline is more flexible, but it does more host-side work than the simpler classic paths.

If you are debugging CPU, netplay, or serial behavior and want to minimize host overhead:

- prefer `RenderPath=1` or `2`
- leave filters disabled

If you are capturing screenshots or comparing visual styles, `RenderPath=3` is the right place to experiment.

## Troubleshooting

### Filter settings do nothing

Check `RenderPath`. If it is not `3`, the filter settings are intentionally ignored.

### Image is softer than expected

Check both:

- pre-filter mode
- scale filter mode

A soft analog pre-filter plus HQ/xBR scaling can add up quickly.

### Performance drops on weaker machines

Try this order:

1. disable CRT mode
2. use `nearest`
3. disable pre-filter
4. drop back to a classic render path

## Developer notes

Relevant files:

- `configcfg.c`
- `main.c`
- `guicore.c`
- `filters.c`
- `filters.h`

For documentation and testing, it is useful to record:

- render path
- pre-filter mode
- scale mode
- CRT mode

Those settings materially change how screenshots and visual comparisons should be interpreted.

## Interlaced video

Display settings offer INTERLACE (`.UZE HINT`, `OFF`, `ON`, `DETECT`) and PRESENTATION
(`.UZE HINT`, `FIELD (60 HZ)`, `WEAVE`). Both default to `.UZE HINT`.
Existing ROMs without a video hint remain progressive. Forcing ON without a
presentation hint uses FIELD. A stable alternating vertical-sync phase must
be detected before either interlace presentation is used; progressive signals
continue through the normal renderer.

- FIELD draws each incoming field at its own scanline positions, leaving the
  opposite lines black. It updates at the incoming field rate (about 60 Hz),
  without retaining the previous field. This models field placement, not a full
  phosphor/beam simulation. Fine details can flicker and average brightness is
  lower. A 1x output cannot represent the alternating positions fully.
- WEAVE updates the arriving field's lines and retains the other field. This
  presents all spatial detail, but moving edges can show combing because the
  two fields were generated at different times. Presentation still updates on
  each field; a pair spans about 1/30 second.

These controls are independent of TV SYNC PLL. Changing them takes effect
without a reset. Frame merge and automatic field dropping are bypassed while
interlace is enabled to preserve field order. Reset, loss of detection, ROM
changes, and presentation changes discard field history. Screenshots use the
selected presentation. No additional field copying or filtering is performed
when interlace is disabled; the normal 2x pixel loop is unchanged.

Packrom properties:

```ini
interlace=field
# Alternatively: interlace=weave or interlace=off
```

The new video flags byte at header offset `0x19C` uses bit 0 to request
interlace and bit 1 to select weave (only meaningful with bit 0 set).
Thus field=1, weave=3, off=0. Other bits are reserved and preserved on patching.
`interlace=on` and `interlace=default` are aliases for field. The header remains
512 bytes; SD and UART bytes and appended dependency offsets are unchanged.
Local presentation overrides take precedence over the ROM's request.

Config keys: `InterlaceMode` (0=hint, 1=off, 2=on, 3=detect) and
`InterlacePresentation` (0=hint, 1=field, 2=weave).
The API exposes these as `interlace_mode` and `interlace_presentation` via
`EMU_SET` / `EMU_STATUS`; status also reports `interlace_active` for current
signal detection. Web Controls includes both settings. Use matching interlace
settings for deterministic movie comparisons; field detection/history is
reacquired after a machine-state restore, not embedded in the savestate ABI.

DETECT examines sync timing regardless of ROM hints and uses the selected
presentation only after a stable alternating field signal is found. Existing
ON settings retain the same signal-validation requirement; DETECT is an explicit
name for that automatic behavior, not a forced odd/even guess. OFF and unhinted
HINT retain the normal decoder without interlace monitoring.

## Area-weighted sampling

CONFIG > VIDEO has a **SAMPLING** dropdown with **.UZE HINT**, **NEAREST**,
and **AREA-WEIGHTED** choices for the staged render path. It blends source cycles by their coverage of each output pixel,
reducing uneven strokes in narrow-pixel text modes. It costs additional CPU
work only when enabled; the default nearest-cycle pixel loop is unchanged.
Classic paths retain their existing filters; Potato always uses nearest sampling.
The video window is enlarged and scrolls on smaller displays.

By default, sampling is off unless requested by a ROM. Choosing NEAREST or AREA-WEIGHTED
sets a persistent override. **.UZE HINT** restores ROM selection.
`AreaWeightedMode` in config.cfg is 0 for ROM hint (otherwise off), 1 for forced
off, or 2 for forced on. Changing the setting takes effect immediately and
invalidates previous-frame blending. Loading HEX does not inherit a UZE hint.

The `.uze` video byte at offset `0x19C` reserves bit 2 (`0x04`) for area sampling:
clear means off, set means on. Bits 0 and 1 retain their interlace meanings;
other bits must be preserved. This defines the future packrom property
`areaweight=0` or `areaweight=1`; packrom support is not implemented here.
Older zero-filled headers remain off. Program hints never replace saved user
preferences.

### Full width in the filters

The same selection marks a hi-res game for the display filters. The filters
normally take every other pixel of each 640-pixel line, 320 across, and the 2x
scalers double them back: right for pixel art, but a mode 80 text dot is about
1.3 pixels wide, so whole dots are dropped and letters lose strokes. With area
sampling on, a pre-filter or CRT effect takes all 640 pixels, each line is
drawn twice to the height the CRT effects expect, and the 2x scalers (Scale2x,
HQ2x, xBR2x) are skipped. The CRT effects run on the same 620x456 image as
before; the capture and any pre-filter cover twice the pixels. The small (1x)
window and the unfiltered display are unchanged.

The API exposes `area_weighted` and `area_weighted_mode` in `EMU_STATUS`.
`EMU_SET area_weighted 0|1` forces off/on; `EMU_SET area_weighted_mode 0`
restores ROM hints. The sampling algorithm is adapted from uze6666's PR #11.

HEX programs can also request these settings using [.hex hints](hex-hints.md).
