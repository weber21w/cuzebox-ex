#!/usr/bin/env python3
"""Host-side regression for the lightweight ELF/DWARF line-table reader."""

from __future__ import annotations

import os
import pathlib
import shutil
import struct
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent


def uleb(v: int) -> bytes:
    out = bytearray()
    while True:
        b = v & 0x7F
        v >>= 7
        if v:
            b |= 0x80
        out.append(b)
        if not v:
            return bytes(out)


def make_debug_line() -> bytes:
    std_lengths = bytes([0, 1, 1, 1, 1, 0, 0, 0, 1])
    dirs = b"\0"
    files = b"fixture.c\0" + uleb(0) + uleb(0) + uleb(0) + b"\0"
    header = bytes([1, 1, 0xFB, 14, 10]) + std_lengths + dirs + files
    program = bytearray()
    program += b"\0" + uleb(5) + b"\x02" + struct.pack("<I", 0x10)  # set_address
    program += b"\x01"  # copy -> line 1, byte 0x10 / word 8
    program += b"\x03" + b"\x04"  # advance_line +4 -> line 5
    program += b"\x02" + uleb(6)  # advance_pc -> byte 0x16 / word 11
    program += b"\x01"  # copy
    program += b"\x02" + uleb(4)  # end address byte 0x1a / word 13
    program += b"\0" + uleb(1) + b"\x01"  # end_sequence
    body = struct.pack("<H", 2) + struct.pack("<I", len(header)) + header + program
    return struct.pack("<I", len(body)) + body


def make_elf(path: pathlib.Path) -> None:
    shstr = b"\0.shstrtab\0.debug_line\0"
    debug_line = make_debug_line()
    ehsize = 52
    shentsize = 40
    shnum = 3
    shstr_off = ehsize
    line_off = shstr_off + len(shstr)
    shoff = (line_off + len(debug_line) + 3) & ~3
    data = bytearray(shoff + shentsize * shnum)
    ident = b"\x7fELF" + bytes([1, 1, 1]) + bytes(9)
    hdr = struct.pack(
        "<16sHHIIIIIHHHHHH",
        ident,
        1,
        83,  # EM_AVR
        1,
        0,
        0,
        shoff,
        0,
        ehsize,
        0,
        0,
        shentsize,
        shnum,
        1,
    )
    data[: len(hdr)] = hdr
    data[shstr_off : shstr_off + len(shstr)] = shstr
    data[line_off : line_off + len(debug_line)] = debug_line

    def sh(index: int, name: int, typ: int, off: int, size: int, align: int = 1) -> None:
        base = shoff + index * shentsize
        data[base : base + shentsize] = struct.pack("<IIIIIIIIII", name, typ, 0, 0, off, size, 0, 0, align, 0)

    sh(1, 1, 3, shstr_off, len(shstr))
    sh(2, 11, 1, line_off, len(debug_line))
    path.write_bytes(data)


def main() -> int:
    cc = os.environ.get("CCNAT") or os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        raise SystemExit("No host C compiler found")
    with tempfile.TemporaryDirectory(prefix="cuzebox-dwarf-") as td:
        d = pathlib.Path(td)
        elf = d / "fixture.elf"
        (d / "fixture.c").write_text("line1\nline2\nline3\nline4\nline5\n", encoding="utf-8")
        make_elf(elf)
        harness = d / "harness.c"
        harness.write_text(
            r'''#include <stdio.h>
#include "debug_source.h"
int main(int argc, char** argv){
    cu_debug_source_location_t a,b;
    if(argc != 2 || !cu_debug_source_load_elf(argv[1])) return 2;
    if(cu_debug_source_file_count()!=1 || cu_debug_source_row_count()!=2) return 3;
    if(!cu_debug_source_lookup(8,&a) || a.line!=1 || a.word_addr!=8 || a.end_word_addr!=11) return 4;
    if(!cu_debug_source_lookup(11,&b) || b.line!=5 || b.word_addr!=11 || b.end_word_addr!=13) return 5;
    if(!cu_debug_source_resolve_line(0,5,&b) || b.word_addr!=11) return 6;
    printf("%s\n", cu_debug_source_status());
    return 0;
}
''',
            encoding="utf-8",
        )
        exe = d / ("test_debug_source.exe" if os.name == "nt" else "test_debug_source")
        subprocess.run(
            [str(cc), "-std=gnu99", "-Wall", "-Wextra", "-I", str(ROOT), str(ROOT / "debug_source.c"), str(ROOT / "debug_dwarf.c"), str(harness), "-o", str(exe)],
            check=True,
        )
        subprocess.run([str(exe), str(elf)], check=True)
    print("debug source/DWARF regression: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
