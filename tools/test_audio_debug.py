#!/usr/bin/env python3
"""Regression for pay-for-play cycle-correlated native DAC debugging."""
from __future__ import annotations
import os, pathlib, shutil, subprocess, tempfile
ROOT=pathlib.Path(__file__).resolve().parent.parent
SDL='''#ifndef SDL_H\n#define SDL_H\n#include <stdint.h>\ntypedef uint8_t Uint8; typedef uint16_t Uint16; typedef uint32_t Uint32; typedef int16_t Sint16; typedef int32_t Sint32; typedef uint32_t SDL_AudioDeviceID;\n#endif\n'''

def pp(cc,inc,*defs):
    cmd=[cc,'-E','-P','-std=gnu17','-I',str(inc),'-I',str(ROOT),*['-D'+d for d in defs],str(ROOT/'cu_avr.c')]
    return subprocess.check_output(cmd,text=True,env={**os.environ,'TERM':'dumb'})

def main():
    cc=os.environ.get('CC') or shutil.which('cc') or shutil.which('gcc')
    if not cc: raise SystemExit('No host C compiler')
    cfg=(ROOT/'Make_config.mk').read_text()
    defs=(ROOT/'Make_defines.mk').read_text()
    src=(ROOT/'cu_avr.c').read_text()
    html=(ROOT/'tools/web_debugger/audio.html').read_text()
    dbg=(ROOT/'tools/web_debugger/debugger.html').read_text()
    assert 'FLAG_AUDIO_TRACE=$(FLAG_DEBUGGER)' in cfg
    assert 'ENABLE_AUDIO_TRACE=0' in defs and '-DENABLE_AUDIO_TRACE=1' in defs
    assert 'if (audio_debug_active){ cu_avr_audio_debug_event(cval); }' in src
    assert 'debug_event_break = TRUE;' in src
    assert 'Native Uzebox DAC writes' in html and 'AUDIO_DEBUG' in html
    assert 'Audio DAC debugger' in dbg and 'selectAudioTraceEvent' in dbg
    with tempfile.TemporaryDirectory(prefix='cuzebox-audio-debug-') as td:
        inc=pathlib.Path(td);(inc/'SDL2').mkdir();(inc/'SDL2'/'SDL.h').write_text(SDL)
        off=pp(cc,inc,'ENABLE_DEBUGGER=1','ENABLE_API_SERVER=1')
        on=pp(cc,inc,'ENABLE_DEBUGGER=1','ENABLE_API_SERVER=1','ENABLE_AUDIO_TRACE=1')
        release=pp(cc,inc)
        assert 'audio_debug_active' not in off
        assert 'cu_avr_audio_debug_event' not in off
        assert 'audio_trace_buf' not in off
        assert 'audio_debug_active' not in release
        assert 'cu_avr_audio_debug_event' not in release
        assert 'audio_debug_active' in on and 'audio_trace_buf' in on
        subprocess.run([cc,'-fsyntax-only','-std=gnu17','-Wall','-Wextra','-I',str(inc),'-I',str(ROOT),'-DENABLE_DEBUGGER=1','-DENABLE_API_SERVER=1','-DENABLE_AUDIO_TRACE=1',str(ROOT/'cu_avr.c')],check=True,env={**os.environ,'TERM':'dumb'})
    print('audio DAC debugger pay-for-play regression: PASS')
    return 0
if __name__=='__main__': raise SystemExit(main())
