"""Independently verify PNG chunks, CRCs, DEFLATE, pixels and screenshot defaults.
Run after cycle-screenshot-test and screenshot-tool-test, passing their OBD.
Only Python's standard library is required; Pillow is an optional second decoder.
"""
from pathlib import Path
import struct
import sys
import zlib
import json

def metadata(path):
    data=path.read_bytes();pos=8
    while pos<len(data):
        n=struct.unpack_from('>I',data,pos)[0]
        if data[pos+4:pos+8]==b'tEXt':
            key,_,value=data[pos+8:pos+8+n].partition(b'\0')
            if key==b'CUEX.Capture':return json.loads(value)
        pos+=12+n
    raise AssertionError(('missing capture metadata',path))

def decode(path):
    data=path.read_bytes()
    assert data[:8]==b'\x89PNG\r\n\x1a\n',path
    pos=8;stream=bytearray();dims=None;ended=False
    while pos<len(data):
        n=struct.unpack_from('>I',data,pos)[0]
        kind=data[pos+4:pos+8];payload=data[pos+8:pos+8+n]
        assert len(payload)==n and pos+12+n<=len(data),path
        crc=struct.unpack_from('>I',data,pos+8+n)[0]
        assert zlib.crc32(kind+payload)&0xffffffff==crc,(path,kind)
        if kind==b'IHDR':
            w,h,depth,color,compression,filtering,interlace=struct.unpack('>IIBBBBB',payload)
            assert (depth,color,compression,filtering,interlace)==(8,2,0,0,0)
            dims=(w,h)
        elif kind==b'IDAT':stream.extend(payload)
        elif kind==b'IEND':assert n==0;ended=True
        pos+=12+n
    assert ended and pos==len(data) and dims
    w,h=dims;inflater=zlib.decompressobj();raw=inflater.decompress(stream)+inflater.flush()
    assert inflater.eof and not inflater.unused_data and len(raw)==(w*3+1)*h,path
    rgb=bytearray();previous=bytes(w*3)
    for y in range(h):
        off=y*(w*3+1)
        assert raw[off]==2,(path,'expected PNG Up filter')
        previous=bytes((a+b)&255 for a,b in zip(raw[off+1:off+1+w*3],previous))
        rgb.extend(previous)
    try:
        from PIL import Image
    except ImportError:
        pass
    else:
        with Image.open(path) as image:
            image.load();assert image.size==dims and image.convert('RGB').tobytes()==rgb
    return dims,bytes(rgb)

def main():
    base=Path(sys.argv[1]).resolve()
    case=base/'cycle-screenshot-run'
    dims,rgb=decode(case/'cycles.png')
    meta=metadata(case/'cycles.png')
    assert meta['path']=='cycle' and meta['cycle_exact_x'] and meta['source_valid']
    assert (meta['width'],meta['height'])==dims and meta['y_scale_num']==5
    assert dims==(1440,1120) and rgb==(case/'cycles.rgb').read_bytes()
    # Opaque black rows in the game are legitimate content, never crop them.
    assert not any(rgb[:1440*3*5]) and not any(rgb[-1440*3*5:])
    codec_files=list(case.glob('codec-*.png'));assert len(codec_files)==8
    for path in codec_files:
        _,decoded=decode(path);assert decoded==path.with_suffix('.rgb').read_bytes(),path
    dims,_=decode(case/'filtered.png');assert dims[0]<dims[1] and dims[1]==456
    tool=base/'screenshot-tool-run'
    for entry in (tool/'png-fixtures.txt').read_text().splitlines():
        kind,name=entry.split(' ',1);dims,rgb=decode(tool/name)
        if kind=='GUI':assert dims==(640,560) and any(rgb),'missing rendered GUI'
        else:assert dims==(1440,1125) and not any(rgb),'default screenshot added GUI/borders'
    print('PNG verification passed: independent zlib/CRC decode, RGB equality, GUI opt-in and cycle-only defaults.')

if __name__=='__main__':main()
