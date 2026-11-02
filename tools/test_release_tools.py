"""Release tooling checks that can run without macOS or publishing credentials."""
from pathlib import Path
import importlib.util,subprocess,tempfile,unittest,gzip,hashlib
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

    def test_macos_sdl_compat_dynamic_runtime(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=Path(tmp).resolve();sdk=base/'sdk';sdk.mkdir()
            sdl2=sdk/'libSDL2-2.0.0.dylib';sdl3=sdk/'libSDL3.dylib';extra=sdk/'libExtra.dylib'
            sdl2.write_bytes(b'sdl2-compat\0libSDL3.dylib\0');sdl3.write_bytes(b'SDL3');extra.write_bytes(b'extra')
            exe=base/'MacOS/cuzebox';exe.parent.mkdir();exe.write_bytes(b'EXEC')
            def deps(path):
                if path==exe:return [str(sdl2)]
                if path==sdl3:return [str(sdl3),'@loader_path/libExtra.dylib']
                return []
            with patch.object(release,'macho_deps',side_effect=deps),patch.object(release,'run') as commands:
                copied=release.macos_libraries([exe],base/'Frameworks')
            self.assertEqual({p.name for p in copied},{sdl2.name,'libSDL3.dylib',extra.name})
            self.assertEqual((base/'Frameworks/libSDL3.dylib').read_bytes(),b'SDL3')
            self.assertFalse(any(cmd.args[0][1]=='-change' and cmd.args[0][2]=='@loader_path/libSDL3.dylib' for cmd in commands.call_args_list), 'dlopen dependency must not be treated as a load command')

    def test_equivalent_gzip_sources_and_final_checksums(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);source=root/'downloads';name='CUEX-preview-source.tar.gz'
            for platform,mtime in [('linux',0),('windows',123)]:
                folder=source/platform;folder.mkdir(parents=True)
                archive=gzip.compress(b'identical TAR bytes',mtime=mtime)
                (folder/name).write_bytes(archive)
                binary=platform+'.zip';(folder/binary).write_bytes(platform.encode())
                (folder/(platform+'-SHA256SUMS.txt')).write_text(
                    hashlib.sha256(archive).hexdigest()+'  '+name+'\n'+
                    hashlib.sha256(platform.encode()).hexdigest()+'  '+binary+'\n')
            subprocess.run([__import__('sys').executable,str(Path(__file__).with_name('collect_release_artifacts.py')),str(source),str(root/'release')],check=True,capture_output=True)
            self.assertEqual(gzip.decompress((root/'release'/name).read_bytes()),b'identical TAR bytes')
            for manifest in (root/'release').glob('*SHA256SUMS.txt'):
                for line in manifest.read_text().splitlines():
                    expected,filename=line.split(None,1)
                    self.assertEqual(expected,hashlib.sha256((root/'release'/filename).read_bytes()).hexdigest())

    def test_different_gzip_source_payloads_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);source=root/'downloads'
            for platform in ('linux','windows'):
                folder=source/platform;folder.mkdir(parents=True)
                (folder/'CUEX-preview-source.tar.gz').write_bytes(gzip.compress(platform.encode()))
            r=subprocess.run([__import__('sys').executable,str(Path(__file__).with_name('collect_release_artifacts.py')),str(source),str(root/'release')],capture_output=True)
            self.assertNotEqual(r.returncode,0);self.assertIn(b'Conflicting release asset',r.stderr)

    def test_invalid_manifest_hash_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);source=root/'downloads';source.mkdir()
            (source/'app.zip').write_bytes(b'binary')
            (source/'platform-SHA256SUMS.txt').write_text('0'*64+'  app.zip\n')
            r=subprocess.run([__import__('sys').executable,str(Path(__file__).with_name('collect_release_artifacts.py')),str(source),str(root/'release')],capture_output=True)
            self.assertNotEqual(r.returncode,0);self.assertIn(b'Checksum mismatch',r.stderr)

    def test_asset_collision_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);src=root/'downloads'
            for job in ('one','two'):(src/job).mkdir(parents=True)
            (src/'one/same.tar.gz').write_bytes(b'one');(src/'two/same.tar.gz').write_bytes(b'two')
            r=subprocess.run([__import__('sys').executable,str(Path(__file__).with_name('collect_release_artifacts.py')),str(src),str(root/'release')],capture_output=True)
            self.assertNotEqual(r.returncode,0)
            self.assertIn(b'Conflicting release asset',r.stderr)

if __name__=='__main__':unittest.main()
