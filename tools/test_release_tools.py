"""Release tooling checks that can run without macOS or publishing credentials."""
from pathlib import Path
import importlib.util,subprocess,tempfile,unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('release_package',Path(__file__).with_name('release_package.py'))
release=importlib.util.module_from_spec(spec);spec.loader.exec_module(release)

class ReleaseTools(unittest.TestCase):
    def test_source_whitelist(self):
        names={p.relative_to(release.ROOT).as_posix() for p in release.source_files()}
        self.assertIn('main.c',names);self.assertIn('runtime_paths.h',names)
        self.assertIn('icon.rc',names);self.assertIn('icon.ico',names)
        self.assertIn('extras/cuzebox_hex_hints.h',names)
        self.assertIn('packaging/config.default.cfg',names)
        self.assertIn('packaging/cuzebox.desktop',names)
        self.assertIn('packaging/rocknix/package.mk.in',names)
        for name in ('config.cfg','themes/custom.cfg','eeprom.bin','gamefile.uze'):
            self.assertNotIn(name,names)
        self.assertFalse(any(n.startswith(('_obj_/','work/','roms/','dist/')) for n in names))

    def test_macos_dependency_closure(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=Path(tmp).resolve();homebrew=base/'brew';homebrew.mkdir()
            a=homebrew/'libA.dylib';b=homebrew/'libB.dylib';a.write_bytes(b'A');b.write_bytes(b'B')
            exe=base/'MacOS/cuzebox';exe.parent.mkdir();exe.write_bytes(b'EXEC')
            libs=base/'Frameworks';calls=[]
            def deps(path):
                if path==exe:return [str(a),'/usr/lib/libSystem.B.dylib']
                if path==a:return ['@loader_path/libB.dylib']
                return []
            with patch.object(release,'macho_deps',side_effect=deps),patch.object(release,'run',side_effect=lambda cmd,**kw:calls.append([str(x) for x in cmd])):
                result=release.macos_libraries([exe],libs)
            self.assertEqual({p.name for p in result},{'libA.dylib','libB.dylib'})
            self.assertEqual((libs/'libB.dylib').read_bytes(),b'B')
            self.assertTrue(any('@executable_path/../Frameworks/libA.dylib' in c for c in calls))
            self.assertTrue(any('@loader_path/libB.dylib' in c for c in calls))
            self.assertFalse((libs/'libSystem.B.dylib').exists())

    def test_asset_collision_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);src=root/'downloads'
            for job in ('one','two'):(src/job).mkdir(parents=True)
            (src/'one/same.tar.gz').write_bytes(b'one');(src/'two/same.tar.gz').write_bytes(b'two')
            r=subprocess.run([__import__('sys').executable,str(Path(__file__).with_name('collect_release_artifacts.py')),str(src),str(root/'release')],capture_output=True)
            self.assertNotEqual(r.returncode,0)
            self.assertIn(b'Conflicting release asset',r.stderr)

if __name__=='__main__':unittest.main()
