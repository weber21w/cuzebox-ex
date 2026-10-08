#!/usr/bin/env python3
"""Generate CUzeBox's built-in resident bootloader C header from Intel HEX.

By default the script searches the project tree for a file named
``bootloader.hex`` case-insensitively, extracts the ATmega644 4 KiB boot
section (0xF000..0xFFFF), fills holes with 0xFF, and emits
``bootloader_builtin.h``.

The output file is only replaced when its contents actually change, so it is
safe to run from a normal incremental build.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import sys
import tempfile
import zlib

DEFAULT_ORIGIN = 0xF000
DEFAULT_SIZE = 4096
DEFAULT_OUTPUT = "bootloader_builtin.h"
IGNORED_SEARCH_DIRS = {
    ".git", ".hg", ".svn", "__pycache__", "build", "dist", "_build",
    "_portmaster", "node_modules",
}


class HexError(ValueError):
    pass


def parse_int(text: str) -> int:
    try:
        return int(text, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid integer: {text}") from exc


def find_bootloader_hex(root: Path) -> Path | None:
    """Find bootloader.hex deterministically, preferring the shallowest path."""
    root = root.resolve()
    matches: list[Path] = []

    if root.is_file():
        return root if root.name.lower() == "bootloader.hex" else None

    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(
            d for d in dirnames if d not in IGNORED_SEARCH_DIRS and not d.startswith(".")
        )
        for filename in filenames:
            if filename.lower() == "bootloader.hex":
                matches.append(Path(dirpath) / filename)

    if not matches:
        return None

    def rank(path: Path) -> tuple[int, str]:
        try:
            rel = path.resolve().relative_to(root)
            return (len(rel.parts), rel.as_posix().lower())
        except ValueError:
            return (1 << 30, path.as_posix().lower())

    matches.sort(key=rank)
    return matches[0]


def _decode_record(line: str, line_no: int) -> tuple[int, int, int, bytes]:
    if not line.startswith(":"):
        raise HexError(f"line {line_no}: Intel HEX record must start with ':'")
    text = line[1:].strip()
    if len(text) < 10 or (len(text) & 1):
        raise HexError(f"line {line_no}: malformed Intel HEX record length")
    try:
        record = bytes.fromhex(text)
    except ValueError as exc:
        raise HexError(f"line {line_no}: non-hexadecimal data") from exc

    count = record[0]
    if len(record) != count + 5:
        raise HexError(
            f"line {line_no}: byte count says {count}, record contains {len(record) - 5} data bytes"
        )
    if (sum(record) & 0xFF) != 0:
        raise HexError(f"line {line_no}: checksum mismatch")

    address = (record[1] << 8) | record[2]
    record_type = record[3]
    return count, address, record_type, record[4 : 4 + count]


def load_boot_section(path: Path, origin: int, size: int) -> tuple[bytes, int, int]:
    """Return (boot image, bytes_from_hex_in_section, bytes_outside_section)."""
    if origin < 0 or size <= 0 or origin + size > 0x100000000:
        raise HexError("invalid output address range")

    image = bytearray([0xFF]) * size
    written = bytearray(size)
    base = 0
    in_section = 0
    outside = 0
    saw_eof = False

    try:
        lines = path.read_text(encoding="ascii").splitlines()
    except (OSError, UnicodeError) as exc:
        raise HexError(f"unable to read {path}: {exc}") from exc

    for line_no, raw in enumerate(lines, 1):
        line = raw.strip()
        if not line:
            continue
        if saw_eof:
            raise HexError(f"line {line_no}: data appears after EOF record")

        count, address, record_type, data = _decode_record(line, line_no)

        if record_type == 0x00:  # Data
            start = base + address
            for index, value in enumerate(data):
                absolute = start + index
                if origin <= absolute < origin + size:
                    offset = absolute - origin
                    if written[offset] and image[offset] != value:
                        raise HexError(
                            f"line {line_no}: conflicting data at address 0x{absolute:X}"
                        )
                    if not written[offset]:
                        in_section += 1
                        written[offset] = 1
                    image[offset] = value
                else:
                    outside += 1
        elif record_type == 0x01:  # EOF
            if count != 0:
                raise HexError(f"line {line_no}: EOF record has data")
            saw_eof = True
        elif record_type == 0x02:  # Extended segment address
            if count != 2:
                raise HexError(f"line {line_no}: bad extended-segment-address record")
            base = int.from_bytes(data, "big") << 4
        elif record_type == 0x04:  # Extended linear address
            if count != 2:
                raise HexError(f"line {line_no}: bad extended-linear-address record")
            base = int.from_bytes(data, "big") << 16
        elif record_type in (0x03, 0x05):
            # Start segment / linear address records describe execution entry
            # points and do not contribute bytes to the flash image.
            continue
        else:
            raise HexError(f"line {line_no}: unsupported Intel HEX record type 0x{record_type:02X}")

    if not saw_eof:
        raise HexError("missing Intel HEX EOF record")
    if in_section == 0:
        raise HexError(
            f"no data falls inside requested boot section 0x{origin:X}..0x{origin + size - 1:X}"
        )

    return bytes(image), in_section, outside


def render_header(image: bytes, source_name: str, origin: int) -> str:
    crc = zlib.crc32(image) & 0xFFFFFFFF
    size = len(image)
    lines = [
        "/*",
        " * Built-in Uzebox resident bootloader image.",
        " *",
        f" * Generated by tools/bootloader_hex_to_c.py from {source_name}.",
        " * Do not edit this byte array by hand; regenerate it from the HEX file.",
        " * External ResidentBootloaderFile content, when present and valid,",
        " * overrides this boot section at runtime.",
        " */",
        "",
        "#ifndef BOOTLOADER_BUILTIN_H",
        "#define BOOTLOADER_BUILTIN_H",
        "",
        '#include "types.h"',
        "",
        f"#define CU_BUILTIN_BOOTLOADER_ORIGIN 0x{origin:04X}U",
        f"#define CU_BUILTIN_BOOTLOADER_SIZE   {size}U",
        f"#define CU_BUILTIN_BOOTLOADER_CRC32  0x{crc:08X}UL",
        "",
        "static uint8 const cu_builtin_bootloader[CU_BUILTIN_BOOTLOADER_SIZE] = {",
    ]

    width = 16
    for offset in range(0, size, width):
        chunk = image[offset : offset + width]
        suffix = "," if offset + width < size else ""
        lines.append("    " + ", ".join(f"0x{value:02X}U" for value in chunk) + suffix)

    lines.extend(["};", "", "#endif", ""])
    return "\n".join(lines)


def write_if_changed(path: Path, content: str) -> bool:
    """Atomically write path if needed. Return True when file changed."""
    encoded = content.encode("utf-8")
    try:
        if path.read_bytes() == encoded:
            return False
    except FileNotFoundError:
        pass

    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=str(path.parent))
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(encoded)
        os.replace(tmp_name, path)
    except Exception:
        try:
            os.unlink(tmp_name)
        except OSError:
            pass
        raise
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, help="Intel HEX input; otherwise search for bootloader.hex")
    parser.add_argument("--search-root", type=Path, default=Path("."), help="tree to search (default: current directory)")
    parser.add_argument("--output", type=Path, default=Path(DEFAULT_OUTPUT), help=f"generated header (default: {DEFAULT_OUTPUT})")
    parser.add_argument("--origin", type=parse_int, default=DEFAULT_ORIGIN, help="boot-section byte origin (default: 0xF000)")
    parser.add_argument("--size", type=parse_int, default=DEFAULT_SIZE, help="boot-section byte size (default: 4096)")
    parser.add_argument("--check", action="store_true", help="verify output is current without modifying it")
    parser.add_argument(
        "--keep-existing-if-missing",
        action="store_true",
        help="if no HEX is found, keep an existing output header and succeed",
    )
    parser.add_argument("--quiet", action="store_true", help="suppress normal status output")
    args = parser.parse_args()

    source = args.input
    if source is None:
        source = find_bootloader_hex(args.search_root)
    elif not source.is_file():
        print(f"bootloader generator: input not found: {source}", file=sys.stderr)
        return 2

    if source is None:
        if args.keep_existing_if_missing and args.output.is_file():
            if not args.quiet:
                print(f"bootloader generator: no bootloader.hex found; keeping {args.output}")
            return 0
        print(
            f"bootloader generator: no bootloader.hex found under {args.search_root}",
            file=sys.stderr,
        )
        return 2

    try:
        image, in_section, outside = load_boot_section(source, args.origin, args.size)
    except HexError as exc:
        print(f"bootloader generator: {exc}", file=sys.stderr)
        return 2

    content = render_header(image, source.name, args.origin)
    crc = zlib.crc32(image) & 0xFFFFFFFF

    if args.check:
        try:
            current = args.output.read_text(encoding="utf-8")
        except OSError:
            current = None
        if current != content:
            print(
                f"bootloader generator: {args.output} is stale; regenerate it from {source}",
                file=sys.stderr,
            )
            return 1
        if not args.quiet:
            print(
                f"bootloader generator: {args.output} is current "
                f"({len(image)} bytes, CRC32 0x{crc:08X})"
            )
        return 0

    try:
        changed = write_if_changed(args.output, content)
    except OSError as exc:
        print(f"bootloader generator: unable to write {args.output}: {exc}", file=sys.stderr)
        return 2

    if not args.quiet:
        action = "generated" if changed else "unchanged"
        detail = f", ignored {outside} byte(s) outside boot section" if outside else ""
        print(
            f"bootloader generator: {action} {args.output} from {source} "
            f"({in_section}/{len(image)} bytes supplied, CRC32 0x{crc:08X}{detail})"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
