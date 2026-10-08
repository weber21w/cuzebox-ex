# SPI RAM

This page describes how CUzeBox models external SPI RAM, how the size policy is selected, and what to check when a program expects more external RAM than the emulator currently exposes.

## What SPI RAM means in CUzeBox

CUzeBox includes a model of the external SPI RAM device used by several Uzebox projects for:

- frame buffers and tile caches
- streamed audio or video data
- large game state blocks
- scratch space for utilities and editors
- custom peripherals or experimental video modes

The emulator models SPI RAM on the SPI bus through `cu_spir.c` and `cu_spir.h`.

## How size is selected

The exposed SPI RAM size is controlled by `SpiRamPagesMode` in `config.cfg`.

The setting uses 64 KiB pages.

- `0` = use the `.uze` header hint when available
- `1` = disable SPI RAM
- `2` = 1 page  = 64 KiB
- `3` = 2 pages = 128 KiB
- `4` = 4 pages = 256 KiB
- `5` = 8 pages = 512 KiB
- `6` = 16 pages = 1 MiB
- `7` = 32 pages = 2 MiB
- `8` = 64 pages = 4 MiB
- `9` = 128 pages = 8 MiB

When `SpiRamPagesMode=0`, the emulator resolves the size like this:

- for `.uze` ROMs, use the header hint if present
- for `.uze` ROMs with no hint, default to 2 pages
- for other ROM formats, default to 2 pages

That policy is applied in `main.c` through `main_resolve_spiram_banks()` and `main_apply_spiram_policy()`.

## Practical guidance

Start with:

- `SpiRamPagesMode=0` when you trust the `.uze` metadata
- `SpiRamPagesMode=1` to deliberately test behavior without SPI RAM
- a fixed page count when you want deterministic development across ROM revisions

Common uses:

- reproducing a hardware target exactly
- testing what happens when a tool assumes more RAM than the image declares
- forcing a large RAM target for development builds that are not packaged as final `.uze` files yet

## Performance notes

SPI RAM accesses are emulated as a peripheral on the SPI bus, not as a magical direct memory map.

That matters because:

- software still needs to speak the expected SPI command/address/data protocol
- timing-sensitive code can behave differently than a simple host-side byte array shortcut
- the emulator includes a fast path for reads because some projects use SPI RAM in very hot loops

The intent is to stay useful for development without making every SPI RAM transaction disproportionately expensive on the host.

## Troubleshooting

### Program behaves as if there is no external RAM

Check:

- `SpiRamPagesMode` is not `1`
- the ROM's `.uze` header actually advertises SPI RAM if you are using mode `0`
- the program is speaking the expected SPI RAM command sequence

### Program works only with a forced page count

That usually means one of these:

- the `.uze` header hint is missing or wrong
- the program changed RAM assumptions during development
- the ROM is not being loaded through a path that provides the intended metadata

### Netplay or automated tests differ after changing SPI RAM size

Treat SPI RAM size as part of the effective runtime configuration.

If a program stores game logic or large buffers in SPI RAM, changing the size policy can change behavior enough to invalidate a comparison with earlier runs.

## Developer notes

Relevant files:

- `cu_spir.c`
- `cu_spir.h`
- `main.c`
- `configcfg.c`

Useful debugger/API angles:

- inspect whether the ROM header reports a SPI RAM hint
- record the active `SpiRamPagesMode` in test logs
- keep per-ROM debugger notes describing the expected external RAM size
