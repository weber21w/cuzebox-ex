# Cheats

This page documents the current cheat system in CUzeBox: how to use it, how the files are stored, and what the implementation currently does.

## What the cheat system is for

The cheat system is a simple SRAM patcher that can force one, two, or four byte values at specific addresses. It is useful for:

- quick gameplay testing
- skipping setup or title screens
- pinning counters, lives, or debug flags
- experimenting with RAM locations while reverse engineering a program

It is not a Game Genie code decoder. The current implementation works directly on SRAM addresses inside the emulated AVR.

## Basic usage

Open the Cheats window from the MicroUI overlay. From there you can:

- enable or disable cheats globally
- enable or disable auto load/save
- add, edit, or remove cheat entries
- reload the current cheat file
- save the current cheat file

Each entry currently has:

- enabled flag
- SRAM address
- value
- optional compare value
- width in bytes
- description

## When cheats are applied

Cheats are applied against emulated SRAM during normal frame execution.

Important behavior:

- cheats are applied once per live frame
- when rollback re-simulation happens, they are also applied during the re-simulated frames so the corrected state stays consistent
- cheats are blocked while Netplay is enabled, to avoid desynchronizing multiplayer state

That last point is important: a cheat that silently changes RAM on only one machine would break deterministic synchronization very quickly.

## Per-game cheat files

Cheat files are stored under the `cheats/` directory.

The default path is derived from the loaded ROM name:

```text
cheats/cheats_<sanitized-rom-name>.cfg
```

The current code also tries a legacy leaf-name fallback when loading older files.

## File format

Cheat files are plain text and easy to edit manually. The current saved format looks like this:

```ini
# CUzeBox cheats
Version=2
Enabled=1
AutoLoadSave=1
Cheat0Enabled=1
Cheat0Addr=0x152
Cheat0Value=0x20
Cheat0CompareUsed=0
Cheat0Compare=0x00
Cheat0Desc=Start in gameplay
```

Common fields:

- `Enabled` — master enable for the whole cheat system
- `AutoLoadSave` — whether the current per-ROM cheat file is loaded and saved automatically
- `CheatNEnabled` — whether entry `N` is active
- `CheatNAddr` — SRAM address
- `CheatNValue` — value written when the cheat is active
- `CheatNCompareUsed` — if nonzero, only patch when memory matches the compare value
- `CheatNCompare` — compare value
- `CheatNDesc` — UI description string

## Current implementation details

The cheat core lives in:

- `cheats.c`
- `cheats.h`

The current implementation characteristics are:

- maximum of 16 cheat entries
- targets SRAM only
- values can be 1, 2, or 4 bytes wide internally
- compare checks happen before writing
- writes are little-endian for multi-byte values

The system reads and writes directly through the AVR SRAM image, so it is cheap and predictable.

## Practical workflow

A good workflow for creating cheats is:

1. use the debugger to find the SRAM location you care about
2. add a cheat entry for that address
3. start with a one-byte value if possible
4. add a description so the file stays readable later
5. save the cheat file for that ROM

This works especially well together with:

- debugger symbols
- watches
- watchpoints
- the automation API

## Current limits and caveats

A few things to keep in mind:

- the current system patches SRAM, not flash or EEPROM
- cheats are intentionally blocked when Netplay is enabled
- this is direct memory patching, not a code-translation cheat system
- manual editing is easy, but bad addresses can still produce confusing game behavior

## Future ideas

Reasonable expansions later could include:

- symbol-aware cheat editing
- API commands for cheat enable/disable and file reload
- importing cheat sets from external formats
- richer notes or categories in the cheat file
