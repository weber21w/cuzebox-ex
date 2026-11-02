"""Smoke-test extracted release payloads without writing into installed assets."""
from pathlib import Path
import argparse,json,os,subprocess,tempfile,zipfile
from test_screenshot_png import decode
from cuzebox_api import CUzeBoxApi
import socket

p=argparse.ArgumentParser(description=__doc__)
g=p.add_mutually_exclusive_group(required=True)
g.add_argument('--windows',type=Path);g.add_argument('--linux',type=Path);g.add_argument('--macos',type=Path)
args=p.parse_args()

with tempfile.TemporaryDirectory(prefix='cuex-package-test-') as temporary:
    root=Path(temporary);launch=root/'caller';launch.mkdir();data=root/'user data'
    artifact=(args.windows or args.linux or args.macos).resolve()
    env=dict(os.environ)
    if args.windows or args.macos:
        with zipfile.ZipFile(artifact) as z:
            for item in z.infolist():
                target=(root/item.filename).resolve()
                if not target.is_relative_to(root):raise AssertionError('Unsafe archive member')
            z.extractall(root)
            if args.macos:
                for item in z.infolist():
                    target=root/item.filename
                    if target.is_file():target.chmod((item.external_attr>>16)&0o777 or 0o644)
        exe=root/'CUEX/cuzebox.exe' if args.windows else root/'CUEX.app/Contents/MacOS/cuzebox'
        command=[str(exe)]
        installed=root/'CUEX' if args.windows else root/'CUEX.app'
        # Remove builder SDK/helper directories from PATH. The package must
        # resolve its own DLLs/helpers and must not need Python/Homebrew there.
        env['PATH']=str(Path(os.environ.get('SystemRoot','C:/Windows'))/'System32') if args.windows else '/usr/bin:/bin'
    else:
        # Extract-and-run supports CI/WSL hosts without FUSE. It launches the
        # exact AppRun packaged in the AppImage rather than the source binary.
        command=[str(artifact)];env['APPIMAGE_EXTRACT_AND_RUN']='1'
        env['XDG_DATA_HOME']=str(root/'xdg');env['PATH']='/usr/bin:/bin'
        installed=None
    before={str(f.relative_to(installed)):f.stat().st_size for f in installed.rglob('*') if f.is_file()} if installed else None
    result=subprocess.check_output([*command,'--data-dir',str(data),'--print-paths'],cwd=launch,env=env,text=True)
    info=json.loads(result);assert info['mode']=='user-data' and Path(info['data']).resolve()==data
    assert (data/'config.cfg').is_file() and 'ApiServer=0' in (data/'config.cfg').read_text()
    with socket.socket() as probe:probe.bind(('127.0.0.1',0));port=probe.getsockname()[1]
    with (root/'run.log').open('w') as log:
        proc=subprocess.Popen([*command,'--data-dir',str(data),'--headless','--paused','--api-port',str(port)],cwd=launch,env=env,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        api=CUzeBoxApi(port=port)
        try:
            api.wait_ready(20)
            def request(cmd):
                r=api.command(cmd);assert r.get('ok'),(cmd,r);return r
            request('RUN_FRAMES 6');api.wait_run_complete(timeout=20)
            shot=data/'capture.png';request('SCREENSHOT '+shot.as_posix());decode(shot)
            request('EMU_SET video_dump_autoinc 0')
            request('EMU_SET video_dump_file video/package-test.MP4')
            request('EMU_SET video_dump_active 1');request('RUN_FRAMES 12');api.wait_run_complete(timeout=30)
            request('EMU_SET video_dump_active 0')
            status=request('EMU_STATUS')['video_dump_status']
            errors={p.name:p.read_text(errors='replace')[-2500:] for p in data.glob('~*.err')}
            assert status.startswith('WROTE '),(status,errors)
            assert (data/'video/package-test.MP4').stat().st_size>0
            request('CONFIG SAVE')
        finally:
            try:api.command('QUIT')
            except Exception:pass
            api.close()
            try:proc.wait(timeout=10)
            except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=5)
    assert not (launch/'config.cfg').exists()
    if installed:
        after={str(f.relative_to(installed)):f.stat().st_size for f in installed.rglob('*') if f.is_file()}
        assert before==after,'installed package resources were modified'
print('Release package: PASS (isolated startup, clean PATH, PNG, bundled FFmpeg recording, writable user data, immutable package)')
