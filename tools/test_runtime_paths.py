"""Check installed/portable startup with isolated files and a real SD root.
Usage: python3 tools/test_runtime_paths.py /path/to/cuzebox
"""
from pathlib import Path
import json,os,shutil,socket,subprocess,sys,tempfile
from cuzebox_api import CUzeBoxApi

exe=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='cuex-paths-') as tmp:
    root=Path(tmp);launch=root/'launch';launch.mkdir();assets=root/'assets';assets.mkdir()
    (assets/'config.default.cfg').write_text('ApiServer=0\nVideoDumpOmitMessages=1\n')
    (assets/'themes').mkdir();(assets/'themes/default.cfg').write_text('GuiTheme=TEST\n')
    data=root/'user data'
    def paths(*args):
        return json.loads(subprocess.check_output([str(exe),*args,'--print-paths'],cwd=launch,text=True))
    legacy=paths();assert legacy['mode']=='legacy' and Path(legacy['data'])==launch
    assert not (launch/'config.cfg').exists()
    p=paths('--data-dir','../user data','--resource-dir','../assets')
    assert p['mode']=='user-data' and Path(p['config']).resolve()==data/'config.cfg'
    assert (data/'config.cfg').read_text().startswith('ApiServer=0')
    assert (data/'themes/default.cfg').read_text()=='GuiTheme=TEST\n'
    (data/'themes/default.cfg').write_text('USER EDIT\n');(data/'config.cfg').write_text('ApiServer=0\nScreenshotYScale=3\n')
    paths('--data-dir','../user data','--resource-dir','../assets')
    assert (data/'themes/default.cfg').read_text()=='USER EDIT\n'
    assert 'ScreenshotYScale=3' in (data/'config.cfg').read_text()
    (launch/'explicit.cfg').write_text('ApiServer=0\n')
    p=paths('--data-dir','../user data','--config','explicit.cfg','--resource-dir','../assets')
    assert Path(p['config']).resolve()==launch/'explicit.cfg'
    result=subprocess.run([str(exe),'--portable','--data-dir',str(data),'--print-paths'],cwd=launch,capture_output=True)
    assert result.returncode!=0
    # Copy just the executable: the path-only command must not require ROMs,
    # GUI drivers, package resources, or a writable current directory.
    portable=root/'portable';portable.mkdir();copied=portable/exe.name;shutil.copy2(exe,copied)
    if os.name=='nt':
        # Resolve runtime DLLs for the copied executable without modifying it.
        os.environ['PATH']=str(exe.parent)+os.pathsep+os.environ.get('PATH','')
    (portable/'cuex-portable').touch()
    p=json.loads(subprocess.check_output([str(copied),'--print-paths'],cwd=launch,text=True))
    assert p['mode']=='portable' and Path(p['data']).resolve()==portable
    (portable/'cuex-portable').unlink();(portable/'cuex-user-data').touch()
    env=dict(os.environ)
    # Avoid the real account's preference directory in the auto-mode check.
    if sys.platform.startswith('linux'):env['XDG_DATA_HOME']=str(root/'xdg')
    if sys.platform.startswith('linux'):
        p=json.loads(subprocess.check_output([str(copied),'--print-paths'],cwd=launch,text=True,env=env))
        assert p['mode']=='user-data' and Path(p['data']).is_relative_to(root)
    game=launch/'game files';game.mkdir()
    (game/'GAME.HEX').write_text(':02000000FFCF30\n:00000001FF\n')
    (game/'SIBLING.BIN').write_bytes(b'virtual SD sibling')
    with socket.socket() as probe:probe.bind(('127.0.0.1',0));port=probe.getsockname()[1]
    with (root/'emulator.log').open('w') as log:
        process=subprocess.Popen([str(exe),'--data-dir','../user data','--resource-dir','../assets','--headless','--paused','--api-port',str(port),'game files/GAME.HEX'],cwd=launch,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        api=CUzeBoxApi(port=port)
        try:
            api.wait_ready(15)
            state=api.command('EMU_STATUS');assert state['ok'],state
            assert state['rom_dir']=='roms' and Path(state['rom_path']).resolve()==game/'GAME.HEX',state
            payload=api.command('SD_PAYLOAD SECTOR 513 0 512')
            raw=bytes(payload['stream'])
            assert b'SIBLING BIN' in raw and b'CONFIG  CFG' not in raw,payload
            r=api.command('SAVESTATE SAVE 0');assert r['ok'],r
            assert list((data/'savestates').glob('*/slot_0.czs'))
            api.command('CONFIG SAVE')
            assert not (launch/'config.cfg').exists()
            assert 'ScreenshotYScale=3' in (data/'config.cfg').read_text()
        finally:
            try:api.command('QUIT')
            except Exception:pass
            api.close()
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=5)
print('Runtime paths: PASS (legacy, portable/package markers, isolated user data, preserved config/templates, relative ROM and SD siblings)')
