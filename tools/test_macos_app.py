"""Check the extracted signed bundle, then launch its native Cocoa window.
Run on a logged-in macOS runner: python3 tools/test_macos_app.py PACKAGE.zip
The existing test_release_package.py separately tests headless recording.
"""
from pathlib import Path
import hashlib,os,socket,subprocess,sys,tempfile,zipfile
from cuzebox_api import CUzeBoxApi
from test_screenshot_png import decode

if sys.platform!='darwin':raise SystemExit('This test requires macOS and its WindowServer.')
artifact=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='cuex-macos-gui-') as tmp:
    root=Path(tmp).resolve()
    with zipfile.ZipFile(artifact) as z:
        for member in z.infolist():
            path=(root/member.filename).resolve()
            if not path.is_relative_to(root):raise AssertionError('Unsafe ZIP member')
        z.extractall(root)
        for member in z.infolist():
            path=root/member.filename
            if path.is_file():path.chmod((member.external_attr>>16)&0o777 or 0o644)
    app=root/'CUEX.app';exe=app/'Contents/MacOS/cuzebox'
    subprocess.run(['codesign','--verify','--deep','--strict',str(app)],check=True)
    binaries=list((app/'Contents/MacOS').iterdir())+list((app/'Contents/Frameworks').iterdir())
    for binary in binaries:
        dependencies=subprocess.check_output(['otool','-L',str(binary)],text=True)
        for line in dependencies.splitlines()[1:]:
            dep=line.strip().split(' (',1)[0]
            assert dep.startswith(('@loader_path/','@executable_path/','@rpath/','/usr/lib/','/System/Library/')),(binary,dep)
    def contents():return {p.relative_to(app).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in app.rglob('*') if p.is_file()}
    before=contents();data=root/'user data';caller=root/'caller';caller.mkdir()
    env=dict(os.environ,PATH='/usr/bin:/bin',SDL_AUDIODRIVER='dummy')
    env.pop('SDL_VIDEODRIVER',None) # Exercise Cocoa/WindowServer, not SDL dummy video.
    with socket.socket() as probe:probe.bind(('127.0.0.1',0));port=probe.getsockname()[1]
    log_path=root/'gui.log'
    with log_path.open('w') as log:
        process=subprocess.Popen([str(exe),'--data-dir',str(data),'--paused','--api-port',str(port)],cwd=caller,env=env,stdout=log,stderr=subprocess.STDOUT)
        api=CUzeBoxApi(port=port)
        try:
            api.wait_ready(30)
            def request(cmd):
                result=api.command(cmd);assert result.get('ok'),(cmd,result);return result
            request('RUN_FRAMES 12');api.wait_run_complete(timeout=30)
            shot=data/'cocoa.png';request('SCREENSHOT '+str(shot));decode(shot)
            request('CONFIG SAVE')
            request('QUIT');process.wait(timeout=15)
            assert process.returncode==0,process.returncode
        except Exception:
            print(log_path.read_text(errors='replace'));raise
        finally:
            api.close()
            if process.poll() is None:
                process.terminate();process.wait(timeout=10)
    assert not (caller/'config.cfg').exists()
    assert contents()==before,'Launching the app modified its signed bundle'
    subprocess.run(['codesign','--verify','--deep','--strict',str(app)],check=True)
print('macOS app: PASS (ZIP signature, relocated libraries, Cocoa window, API/PNG, clean quit, immutable bundle)')
