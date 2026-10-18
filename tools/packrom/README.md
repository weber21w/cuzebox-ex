# packrom with SD timing hints

This source is based on [Uzebox packrom v1.7](https://github.com/Uzebox/uzebox/blob/724d9b59c894003f378592215ce595d24154343b/tools/packrom/packrom.cpp),
retaining its GPL license and upstream ESP8266 AP support, keyboard metadata
corrections, and vertical-flip fix.
It fixes the packed header to exactly 512 bytes and reserves the whole byte at
`0x19A` for an SD timing value (no bit masks):

| Value | Meaning |
|---|---|
| 0 | Unspecified |
| 1 | SLOW |
| 2 | NORMAL |
| 3 | FAST |
| 4–255 | Reserved for future expansion |

The remaining reserved region is `0x19B–0x1FF` (101 bytes). Compile-time checks
enforce both the header length and the timing field's offset. Existing header
fields, program offset (512), and appended-data offset (61952) do not move.

Build and test from the emulator repository root:

```sh
g++ -std=c++11 -O2 tools/packrom/packrom.cpp -o _obj_/packrom
python3 tools/test_packrom_sd_hint.py _obj_/packrom
```

Add `sd=fast`, `sd=normal`, or `sd=slow` to a properties file when packing
a game. Omit the property to leave a new ROM without a hint. Extraction emits
only these three named values; unset and reserved values are omitted.

To add a hint to an existing game, use a different output filename:

```sh
printf 'sd=fast\n' > sd-fast.properties
_obj_/packrom megadoom.uze megadoom-hinted.uze sd-fast.properties
```

Patch mode preserves unspecified header fields and the existing appended
dependency unless a replacement dependency is supplied. Use lowercase `.uze`
and `.hex` extensions, as expected by upstream's filename checks.
Extraction writes `sd=` to its properties output.
Keep the game's expected filename (`MEGADOOM.UZE`) when deploying the patched
file into a separate game directory; games may open their own UZE by name.

CUzebox defaults new preferences to `.UZE HINT`. Old/unknown hints and HEX files
resolve to NORMAL. A manual SD timing choice takes precedence, including saved
preferences from older builds. Older emulators ignore the new hint byte.
