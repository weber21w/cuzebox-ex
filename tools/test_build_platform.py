"""Exercise GNU Make's platform selection without compiling or requiring SDKs."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MAKE = os.environ.get("MAKE", "make")


class PlatformTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="cuzebox-platform-")
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        fake_uname = self.directory / "uname.py"
        fake_uname.write_text("import os\nprint(os.environ.get('TEST_UNAME', ''))\n", encoding="utf-8")
        platform = (ROOT / "Make_platform.mk").as_posix().replace(" ", "\\ ")
        self.fixture = self.directory / "platform.mk"
        self.fixture.write_text(
            f"include {platform}\nall:\n\t@echo RESULT=$(TSYS)\n", encoding="utf-8"
        )
        self.uname = f'UNAME="{Path(sys.executable).as_posix()}" "{fake_uname.as_posix()}"'

    def run_make(self, system="Linux", windows=False, arguments=(), ok=True):
        env = dict(os.environ)
        for variable in ("MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES", "TSYS", "TARGET_RGB30", "TARGET_PORTMASTER", "TARGET_LINUX_AARCH64"):
            env.pop(variable, None)
        env["OS"] = "Windows_NT" if windows else ""
        env["TEST_UNAME"] = system
        result = subprocess.run(
            [MAKE, "--no-print-directory", "-s", "-f", str(self.fixture), self.uname, *arguments],
            env=env, cwd=self.directory, text=True, capture_output=True, timeout=20,
        )
        if ok:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout + result.stderr

    def test_native_auto(self):
        for system, expected in (("Linux", "linux"), ("Darwin", "macos"),
                                 ("MINGW64_NT-10.0", "windows_mingw"),
                                 ("MINGW32_NT-6.1", "windows_mingw"),
                                 ("MSYS_NT-10.0", "windows_mingw"),
                                 ("CYGWIN_NT-10.0", "windows_mingw")):
            for arguments in ((), ("TSYS=auto",)):
                with self.subTest(system=system, arguments=arguments):
                    self.assertIn("RESULT=" + expected, self.run_make(system, arguments=arguments))

    def test_windows_does_not_require_uname(self):
        self.assertIn("RESULT=windows_mingw", self.run_make(windows=True, arguments=("UNAME=nonexistent-uname-command",)))

    def test_explicit_targets_and_alias(self):
        for target, expected in (("linux", "linux"), ("windows_mingw", "windows_mingw"),
                                 ("macos", "macos"), ("osx", "macos"), ("emscripten", "emscripten")):
            with self.subTest(target=target):
                self.assertIn("RESULT=" + expected, self.run_make("Unknown", arguments=(f"TSYS={target}",)))

    def test_handheld_presets_select_linux_on_other_hosts(self):
        for preset in ("TARGET_RGB30=1", "TARGET_LINUX_AARCH64=1", "TARGET_PORTMASTER=1"):
            for arguments in ((preset,), (preset, "TSYS=auto"), (preset, "TSYS=linux")):
                with self.subTest(arguments=arguments):
                    self.assertIn("RESULT=linux", self.run_make("Darwin", windows=True, arguments=arguments))
            self.assertIn("require TSYS=auto or TSYS=linux", self.run_make(arguments=(preset, "TSYS=windows_mingw"), ok=False))

    def test_invalid_or_unknown_host_has_actionable_error(self):
        self.assertIn("Cannot detect native platform", self.run_make("FreeBSD", ok=False))
        self.assertIn("Cannot detect native platform", self.run_make("", ok=False))
        self.assertIn("Unsupported TSYS", self.run_make(arguments=("TSYS=typo",), ok=False))
        self.assertIn("exactly one TSYS", self.run_make(arguments=("TSYS=linux macos",), ok=False))

    def test_real_makefile_selects_tools_and_output(self):
        for system, target, compiler, output in (("Linux", "linux", "gcc", "cuzebox"),
                                                 ("Darwin", "macos", "clang", "cuzebox"),
                                                 ("MINGW64_NT-10.0", "windows_mingw", "gcc", "cuzebox.exe")):
            with self.subTest(system=system):
                env = dict(os.environ, OS="", TEST_UNAME=system)
                for name in ("MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES", "TSYS", "CCOMP", "CCNAT", "OUT",
                             "TARGET_RGB30", "TARGET_PORTMASTER", "TARGET_LINUX_AARCH64"):
                    env.pop(name, None)
                result = subprocess.run(
                    [MAKE, "--no-print-directory", "-s", "show-build-config", self.uname, "TSYS=auto"],
                    env=env, cwd=ROOT, text=True, capture_output=True, timeout=30,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                for expected in (f"TSYS={target}", f"CC={compiler}", f"OUT={output}"):
                    self.assertIn(expected, result.stdout)

    def test_browser_defaults(self):
        fixture = self.directory / "browser.mk"
        fixture.write_text(
            "include Make_defines.mk\nall:\n"
            "\t@echo FEATURES=$(ENABLE_MICROUI),$(ENABLE_DEBUGGER),$(ENABLE_API_SERVER),$(ENABLE_NETPLAY),$(ENABLE_ESP)\n"
            "\t@echo GAME=$(FLAG_NOGAMEFILE)\n\t@echo CFLAGS=$(CFLAGS)\n\t@echo LINK=$(LINKB)\n",
            encoding="utf-8",
        )
        for options in ([], ["FLAG_MICROUI=0"], ["FLAG_USE_SDL1=1"]):
            result = subprocess.run(
                [MAKE, "--no-print-directory", "-s", "-f", str(fixture), "TSYS=emscripten", *options],
                cwd=ROOT, text=True, capture_output=True, timeout=30,
            )
            output = result.stdout + result.stderr
            if options == ["FLAG_USE_SDL1=1"]:
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("requires SDL2", output)
                continue
            self.assertEqual(result.returncode, 0, output)
            self.assertIn("FEATURES=" + ("0" if options else "1") + ",0,0,0,0", output)
            self.assertIn("GAME=1", output)
            self.assertIn("--use-port=sdl2", output)
            self.assertNotIn("USE_PTHREADS", output)
            self.assertNotIn("PROXY_POSIX_SOCKETS", output)
            self.assertNotIn("--preload-file", output)


    def test_esp_hardware_defaults_and_explicit_legacy_builds(self):
        fixture = self.directory / "esp-timing.mk"
        fixture.write_text(
            "include Make_defines.mk\nall:\n"
            "\t@echo HARDWARE=$(FLAG_AVR_TIMER1_CLOCK),$(FLAG_AVR_GPIO),$(FLAG_ESP_AT_HARDWARE_TIMING)\n"
            "\t@echo AT_DEFINE=$(findstring -DCU_ESP_AT_CAPTURE_PROFILE=1,$(CFLAGS))\n",
            encoding="utf-8",
        )
        cases = [
            (["TSYS=linux"], "1,1,1", True),
            (["TSYS=windows_mingw"], "1,1,1", True),
            (["TSYS=macos"], "1,1,1", True),
            (["TSYS=emscripten"], "0,0,1", True),
            (["TSYS=linux", "FLAG_AVR_TIMER1_CLOCK=0", "FLAG_AVR_GPIO=0", "FLAG_ESP_AT_HARDWARE_TIMING=0"], "0,0,0", False),
            (["TSYS=emscripten", "FLAG_AVR_TIMER1_CLOCK=1", "FLAG_AVR_GPIO=1"], "1,1,1", True),
        ]
        for options, flags, enabled in cases:
            with self.subTest(options=options):
                result = subprocess.run([MAKE, "--no-print-directory", "-s", "-f", str(fixture), *options],
                                        cwd=ROOT, text=True, capture_output=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("HARDWARE=" + flags, result.stdout)
                self.assertIn("AT_DEFINE=" + ("-DCU_ESP_AT_CAPTURE_PROFILE=1" if enabled else "") + "\n", result.stdout)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--make", default=MAKE, help="GNU Make executable")
    args, rest = parser.parse_known_args()
    MAKE = shutil.which(args.make) or args.make
    unittest.main(argv=[sys.argv[0], *rest])
