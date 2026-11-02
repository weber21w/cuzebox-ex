"""Exercise real file/API PNG capture while paused, optionally with a game ROM.
Usage: python3 tools/test_screenshot_api.py /path/to/cuzebox [--rom game.uze]
"""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile

from cuzebox_api import CUzeBoxApi
from test_screenshot_png import decode,metadata

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('emulator',type=Path)
    parser.add_argument('--rom',type=Path)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    out=(args.output or root/'_obj_/screenshot-api').resolve();out.mkdir(parents=True,exist_ok=True)
    with socket.socket() as probe:
        probe.bind(('127.0.0.1',0));port=probe.getsockname()[1]
    with tempfile.TemporaryDirectory(prefix='runtime-',dir=out) as temp:
        work=Path(temp)
        (work/'config.cfg').write_text('ApiServer=0\nScreenshotBypassFilters=1\nScreenshotIncludeGui=0\n')
        with (out/'emulator.log').open('w') as log:
            proc=subprocess.Popen([str(args.emulator.resolve()),'--headless','--paused','--unthrottled','--api-port',str(port)],cwd=work,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
            api=CUzeBoxApi(port=port)
            try:
                api.wait_ready(15)
                def command(text):
                    result=api.command(text);assert result.get('ok'),(text,result);return result
                if args.rom:command('LOAD_ROM WAIT '+args.rom.resolve().as_posix())
                command('RUN_FRAMES 120');api.wait_run_complete(timeout=30)
                before=command('GET_STATE')['frame']
                image=out/'cycle.png';command('SCREENSHOT '+image.as_posix())
                info=command('SCREENSHOT_CAPTURE');assert info['format']=='png'
                payload=bytearray()
                for offset in range(0,info['size'],4096):
                    payload.extend(command(f'SCREENSHOT_READ {offset} {min(4096,info["size"]-offset)}')['values'])
                assert image.read_bytes()==payload,'file/API PNG disagreement'
                assert metadata(image)==info['capture'],'PNG/API metadata disagreement'
                assert command('GET_STATE')['frame']==before,'capture advanced paused emulation'
                dims,rgb=decode(image)
                assert dims[0]==1440 and dims[1]%5==0
                if args.rom:assert any(rgb),'game produced no visible cycle samples'
                source_rows=info['capture']['source_rows'];settings=command('SCREENSHOT_SETTINGS')
                assert settings['preset']=='CYCLE' and settings['y_scale']==5
                resized=[]
                for width,sampling in [(256,'NEAREST'),(240,'AREA'),(192,'LINEAR'),(160,'AREA')]:
                    settings=command(f'SCREENSHOT_SETTINGS PRESET WIDTH{width}')
                    assert settings['x_width']==width and settings['y_scale']==1
                    command('SCREENSHOT_SETTINGS SET SAMPLING '+sampling)
                    command('SCREENSHOT_SETTINGS SET Y_SCALE 3')
                    assert command('SCREENSHOT_SETTINGS')['preset']=='CUSTOM'
                    scaled=out/f'width-{width}.png';command('SCREENSHOT '+scaled.as_posix())
                    si=command('SCREENSHOT_CAPTURE');sm=metadata(scaled)
                    assert sm==si['capture'] and sm['sampling']==sampling and not sm['cycle_exact_x']
                    sd,_=decode(scaled);assert sd==(width,source_rows*3),(sd,width)
                    assert (si['width'],si['height'])==sd
                    resized.append({'width':width,'height':sd[1],'sampling':sampling})
                for invalid in ['SET X_WIDTH 0','SET Y_SCALE 11','SET SAMPLING CUBIC','SET X_WIDTH 240garbage','PRESET UNKNOWN']:
                    assert not api.command('SCREENSHOT_SETTINGS '+invalid)['ok'],invalid
                command('SCREENSHOT_SETTINGS PRESET CYCLE')
                assert command('GET_STATE')['frame']==before,'settings/capture advanced paused emulation'
                assert command('SCREENSHOT_CAPTURE BMP')['format']=='bmp24'
                assert command('SCREENSHOT_READ 0 2')['values']==[66,77]
                for name in ('legacy-screenshot','legacy-screenshot.capture'):
                    legacy=out/name
                    command('SCREENSHOT '+legacy.as_posix())
                    assert legacy.read_bytes()[:2]==b'BM','legacy explicit filename lost BMP compatibility'
                result={'width':dims[0],'height':dims[1],'bytes':len(payload),'paused_frame':before,'file_api_equal':True,'capture_did_not_advance':True,'legacy_bmp':True,'resized':resized,'metadata_matches':True}
                (out/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
            finally:
                try:api.command('QUIT')
                except Exception:pass
                try:api.close()
                except Exception:pass
                try:proc.wait(timeout=5)
                except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=5)

if __name__=='__main__':main()
