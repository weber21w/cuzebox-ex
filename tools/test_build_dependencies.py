"""Check incremental builds without compiling the emulator or requiring SDL."""
import os
from pathlib import Path
import shutil
import shlex
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
MAKE = os.environ.get("MAKE", "make")


class DependenciesTest(unittest.TestCase):
    def test_macos_sdl_header_parent(self):
        # Homebrew's SDL flags point inside SDL2; CUEX uses <SDL2/SDL.h>.
        with tempfile.TemporaryDirectory(prefix="cuex-macos-sdl-") as directory:
            root = Path(directory)
            prefix = root / "sdl"
            (prefix / "include/SDL2").mkdir(parents=True)
            (prefix / "include/SDL2/SDL.h").write_text("/* fixture */\n")
            metadata = root / "metadata.py"
            metadata.write_text(
                "import sys\n"
                f"prefix = {prefix.as_posix()!r}\n"
                "arg = sys.argv[1]\n"
                "print({'--prefix': prefix, '--cflags': '-I' + prefix + '/include/SDL2', "
                "'--variable=includedir': prefix + '/include', '--libs': '-lSDL2'}.get(arg, ''))\n")
            fixture = root / "flags.mk"
            fixture.write_text(
                "include Make_defines.mk\n$(info CUEX_FLAGS=$(CFLAGS))\nall:;\n")
            helper = f'"{Path(sys.executable).as_posix()}" "{metadata.as_posix()}"'
            for route, flags in (
                ("sdl2-config", [f"SDL2_CONFIG_BIN={helper}", "SDL2_PKG_OK="]),
                ("pkg-config", ["SDL2_CONFIG_BIN=", "SDL2_PKG_OK=yes", f"PKG_CONFIG={helper}"]),
                ("brew", ["SDL2_CONFIG_BIN=", "SDL2_PKG_OK=", f"SDL2_BREW_PREFIX={prefix.as_posix()}"]),
            ):
                with self.subTest(route=route):
                    result = subprocess.run(
                        [MAKE, "--no-print-directory", "-f", str(fixture), "TSYS=macos",
                         "FLAG_REMOTE_ROMS_LIBCURL=0"] + flags,
                        cwd=ROOT, text=True, capture_output=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    line = next(line for line in result.stdout.splitlines() if line.startswith("CUEX_FLAGS="))
                    includes = [Path(arg[2:]) for arg in shlex.split(line) if arg.startswith("-I")]
                    self.assertTrue(any((base / "SDL2/SDL.h").is_file() for base in includes), line)

    def test_flags_includes_and_noop(self):
        with tempfile.TemporaryDirectory(prefix="cuzebox-dependencies-") as directory:
            root = Path(directory)
            for name in ("Makefile", "Make_config.mk", "Make_defines.mk", "Make_platform.mk"):
                shutil.copyfile(ROOT / name, root / name)
            for name in ("cu_esp_at.c", "cu_esp_at23.inc", "cu_esp_mqtt.inc",
                         "bootloader_builtin.h", "web_assets.h"):
                (root / name).write_text("/* fixture */\n", encoding="utf-8")
            compiler = root / "compiler.py"
            compiler.write_text(
                "from pathlib import Path\nimport sys\n"
                "target = Path(sys.argv[sys.argv.index('-o') + 1])\n"
                "target.write_text(' '.join(sys.argv))\n", encoding="utf-8")
            target = root / "objects/cu_esp_at.o"
            command = [MAKE, "--no-print-directory", "-s", "TSYS=linux", "OBD=objects",
                       f'CCOMP="{Path(sys.executable).as_posix()}" "{compiler.as_posix()}"',
                       "objects/cu_esp_at.o"]
            env = dict(os.environ)
            for key in ("MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES"):
                env.pop(key, None)

            def build(*flags):
                result = subprocess.run(command + list(flags), cwd=root, env=env,
                                        text=True, capture_output=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return target.stat().st_mtime_ns

            first = build()
            time.sleep(1.1)  # Also covers filesystems/older make with one-second stamps.
            self.assertEqual(build(), first, "unchanged flags must not rebuild")
            second = build("FLAG_HEADLESS=1")
            self.assertGreater(second, first, "new command-line flags must rebuild")
            self.assertIn("-DHEADLESS=1", target.read_text())
            time.sleep(1.1)
            self.assertEqual(build("FLAG_HEADLESS=1"), second)
            (root / "cu_esp_mqtt.inc").write_text("/* changed */\n", encoding="utf-8")
            third = build("FLAG_HEADLESS=1")
            self.assertGreater(third, second, "included protocol source must rebuild")
            time.sleep(1.1)
            self.assertGreater(build(), third, "returning to default flags must rebuild")
            self.assertNotIn("-DHEADLESS=1", target.read_text())


if __name__ == "__main__":
    unittest.main()
