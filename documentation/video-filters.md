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
