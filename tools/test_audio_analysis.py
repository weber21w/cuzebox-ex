#!/usr/bin/env python3
"""Browser-side audio spectrum/pitch/cadence regression."""
from __future__ import annotations
import pathlib, shutil, subprocess, tempfile
ROOT=pathlib.Path(__file__).resolve().parent.parent

def main():
    html=(ROOT/'tools/web_debugger/audio.html').read_text()
    dbg=(ROOT/'tools/web_debugger/debugger.html').read_text()
    for needle in ('Spectral / continuity analysis','function fftAnalysis','function pitchEstimate','function dacCadenceStats','Native DAC cadence'):
        assert needle in html, needle
    assert 'audioDbgCadence' in dbg and '<th>Δcy</th>' in dbg
    node=shutil.which('node')
    if not node:
        print('audio analysis regression: SKIP (node unavailable)')
        return 0
    js=html.split('<script src="/static/common.js"></script><script>',1)[1].split('</script>',1)[0]
    core=js[js.index('function median'):js.index('function renderSource')]
    harness=core+r'''
function assertNear(v,e,t,msg){if(Math.abs(v-e)>t)throw new Error(msg+': '+v+' vs '+e)}
const rate=15734,n=2048,f=440;
const sine=Array.from({length:n},(_,i)=>128+80*Math.sin(2*Math.PI*f*i/rate));
const fft=fftAnalysis(sine,rate,128,2048),pitch=pitchEstimate(sine,rate,128),w=waveStats(sine,128,128,64);
if(!fft||!pitch||!w)throw new Error('analysis returned null');
assertNear(fft.dominant,f,2,'FFT dominant');
assertNear(pitch.hz,f,2,'pitch estimate');
if(pitch.confidence<.9)throw new Error('pitch confidence too low: '+pitch.confidence);
if(w.steps!==0)throw new Error('unexpected discontinuity count: '+w.steps);
const dc=Array.from({length:512},(_,i)=>138+20*Math.sin(2*Math.PI*220*i/rate));
const wd=waveStats(dc,128,128,64);assertNear(wd.dc,10,1,'DC estimate');
const ev=[];let cy=1000;for(let i=0;i<20;i++){ev.push({cycle:cy,seq:i});cy+=(i===10)?3640:1820}
const c=dacCadenceStats(ev);if(!c)throw new Error('cadence returned null');
assertNear(c.median,1820,0.01,'cadence median');if(c.late!==1||c.eventIndex!==11)throw new Error('cadence outlier mismatch '+JSON.stringify(c));
console.log('audio analysis regression: PASS');
'''
    with tempfile.TemporaryDirectory(prefix='cuzebox-audio-analysis-') as td:
        fpath=pathlib.Path(td)/'test.js';fpath.write_text(harness)
        subprocess.run([node,'--check',str(fpath)],check=True)
        subprocess.run([node,str(fpath)],check=True)
    return 0
if __name__=='__main__': raise SystemExit(main())
