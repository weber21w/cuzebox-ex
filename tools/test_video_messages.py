"""Independently decode the clips produced by video-messages-test.
Usage: python3 tools/test_video_messages.py <OBD>
Requires ffmpeg/ffprobe in PATH, as does the emulator's video recorder.
"""
import json
from pathlib import Path
import subprocess
import sys

base=Path(sys.argv[1])/'video-messages-run'
images=[];counts=[]
for name in ['omit.MP4','messages.MP4']:
    path=base/name
    probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-count_frames','-show_streams','-of','json',str(path)]))
    video=next(s for s in probe['streams'] if s['codec_type']=='video')
    audio=next(s for s in probe['streams'] if s['codec_type']=='audio')
    count=int(video['nb_read_frames']);counts.append(count)
    assert (video['width'],video['height'])==(960,720) and count==3,video
    assert int(audio['sample_rate'])==48000 and audio['channels']==1,audio
    rgb=subprocess.check_output(['ffmpeg','-v','error','-i',str(path),'-f','rawvideo','-pix_fmt','rgb24','-'])
    expected=subprocess.check_output(['ffmpeg','-v','error','-f','rawvideo','-s','620x228','-pix_fmt','rgb24','-i',str(path)+'.rgb','-vf','scale=940:228:flags=bilinear,scale=940:684:flags=neighbor,pad=960:720:16:18','-frames:v','1','-pix_fmt','rgb24','-f','rawvideo','-'])
    assert len(rgb)==count*len(expected)==count*960*720*3
    # Preserve the existing 720p scaling and CRF22 encode; allow its RGB error.
    error=sum(abs(a-b) for a,b in zip(rgb[:len(expected)],expected))/len(expected)
    assert error<=2,(name,'encoded pixels differ from the selected source',error)
    images.append(rgb[:len(expected)])
    if name=='omit.MP4':
        assert not any(rgb),'emulator text leaked into the clean black guest fixture'
    else:
        assert all(any(rgb[i*len(expected):(i+1)*len(expected)]) for i in range(count)),'opt-in messages missing'
assert images[0]!=images[1],'omit option did not change encoded output'
# Both source clips contain three frames; final muxing must retain all of them.
assert counts[0]==counts[1],('omission changed frame count',counts)
print(f'Video decode: PASS ({counts[0]} frames in both modes, clean default, opt-in messages, expected pixels, mono 48 kHz audio)')
