#!/usr/bin/env python3
"""Check 512-byte packing, whole-byte SD hints, and dependency-safe patching."""
from pathlib import Path
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]


def main():
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='packrom-', dir=ROOT / '_obj_') as temp:
        directory = Path(temp)

        def run(*args, good=True):
            result = subprocess.run([str(exe), *map(str, args)], cwd=directory,
                                    capture_output=True, timeout=15)
            assert (result.returncode == 0) == good, result.stderr.decode(errors='replace')

        program = bytes([0xFF, 0xCF]) * 32
        records = []
        for addr in range(0, len(program), 16):
            record = bytes([16, addr >> 8, addr & 255, 0]) + program[addr:addr + 16]
            records.append(':' + (record + bytes([-sum(record) & 255])).hex().upper())
        (directory / 'game.hex').write_text('\n'.join(records) + '\n:00000001FF\n')
        dependency = bytes(range(256)) * 1000
        (directory / 'data.bin').write_bytes(dependency)
        (directory / 'game.properties').write_text(
            'name=Fixture\nspiram=4\nsd=fast\ndependency=data.bin\n'
            'keyboard=supported\nesp8266_ap=default\nJAMMA=flipV\n')
        run('game.hex', 'game.uze', 'game.properties')
        original = (directory / 'game.uze').read_bytes()
        assert original[:6] == b'UZEBOX' and original[0x19A] == 3
        assert original[0x152] & 0x62 == 0x62  # Keyboard, ESP8266 and AP support.
        assert original[0x193] & 0x40 == 0x40  # AP enabled by default.
        assert original[0x194] & 0x18 == 0x10  # Vertical flip, without horizontal flip.
        assert int.from_bytes(original[0x195:0x199], 'little') == len(dependency)
        assert original[512:512 + len(program)] == program
        assert original[61952:] == dependency
        assert int.from_bytes(original[334:338], 'little') == zlib.crc32(program)

        # Unknown reserved bytes and the whole appended image survive a hint-only patch.
        altered = bytearray(original)
        altered[0x19B:512] = bytes(range(101))
        (directory / 'game.uze').write_bytes(altered)
        (directory / 'patch.properties').write_text('sd=fast\n')
        run('game.uze', 'unchanged.uze', 'patch.properties')
        assert (directory / 'unchanged.uze').read_bytes() == altered
        for value, expected in [('slow', 1), ('normal', 2), ('fast', 3)]:
            (directory / 'patch.properties').write_text('sd=' + value + '\n')
            run('game.uze', 'patched.uze', 'patch.properties')
            patched = (directory / 'patched.uze').read_bytes()
            wanted = bytearray(altered); wanted[0x19A] = expected
            assert patched == wanted, 'patch changed data outside the SD hint byte'

        run('patched.uze', 'extracted.hex', 'extracted.properties', 'extracted.bin', 'icon.raw')
        extracted = (directory / 'extracted.properties').read_text()
        assert 'sd=fast' in extracted
        assert 'keyboard=supported' in extracted and 'keybord=' not in extracted
        assert 'esp8266_ap=default' in extracted
        assert (directory / 'extracted.bin').read_bytes() == dependency
        (directory / 'patch.properties').write_text('keybord=default\n')
        run('game.uze', 'legacy.uze', 'patch.properties')
        legacy = bytearray(altered); legacy[0x193] |= 2
        assert (directory / 'legacy.uze').read_bytes() == legacy
        (directory / 'support.properties').write_text('esp8266_ap=supported\n')
        run('game.hex', 'support.uze', 'support.properties')
        support = (directory / 'support.uze').read_bytes()
        assert support[0x152] & 0x60 == 0x60 and support[0x193] == 0
        assert support[0x19A] == 0  # An omitted property leaves new ROMs unset.
        for value in [0, 255]:
            unknown = bytearray(altered); unknown[0x19A] = value
            (directory / 'unknown.uze').write_bytes(unknown)
            run('unknown.uze', 'unknown.hex', 'unknown.properties', 'unknown.bin', 'unknown.raw')
            assert not any(line.startswith('sd=') for line in
                           (directory / 'unknown.properties').read_text().splitlines())
        for value in ['unspecified', 'none', '0', '3', '255', '256']:
            (directory / 'patch.properties').write_text('sd=' + value + '\n')
            run('game.uze', 'ignored.uze', 'patch.properties')
            assert (directory / 'ignored.uze').read_bytes() == altered

        (directory / 'patch.properties').write_text('sd=fast\n')
        run('game.uze', 'game.uze', 'patch.properties', good=False)
        assert (directory / 'game.uze').read_bytes() == altered
    print('packrom SD hints, header layout, lossless patching and upstream metadata: PASS')


if __name__ == '__main__':
    main()
