#!/usr/bin/env python3
from pathlib import Path
import argparse
ROOT=Path(__file__).resolve().parent/'web_debugger'
OUT=Path(__file__).resolve().parent.parent/'web_assets.h'
FILES=[('index.html','text/html; charset=utf-8'),('controls.html','text/html; charset=utf-8'),('debugger.html','text/html; charset=utf-8'),('sd.html','text/html; charset=utf-8'),('audio.html','text/html; charset=utf-8'),('serial.html','text/html; charset=utf-8'),('network.html','text/html; charset=utf-8'),('common.css','text/css; charset=utf-8'),('common.js','application/javascript; charset=utf-8')]
def cname(n): return 'web_asset_'+n.replace('.','_').replace('-','_')
def main():
    parser = argparse.ArgumentParser(description='Generate embedded web assets or check that they are current.')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    parts=['#ifndef WEB_ASSETS_H','#define WEB_ASSETS_H','','typedef struct { const char* path; const char* mime; const unsigned char* data; unsigned int len; } web_asset_t;','']
    entries=[]
    for name,mime in FILES:
        data=(ROOT/name).read_bytes(); cn=cname(name)
        parts.append(f'static const unsigned char {cn}[] = {{')
        for i in range(0,len(data),16): parts.append('    '+','.join(f'0x{b:02X}' for b in data[i:i+16])+',')
        parts.append('};\n')
        route='/' + name
        entries.append((route,mime,cn,len(data)))
    parts.append('static const web_asset_t web_assets[] = {')
    for route,mime,cn,l in entries: parts.append(f'    {{"{route}","{mime}",{cn},{l}U}},')
    parts.append('};')
    parts.append(f'#define WEB_ASSET_COUNT {len(entries)}U')
    parts.append('\n#endif\n')
    text='\n'.join(parts)
    changed = not OUT.exists() or OUT.read_text()!=text
    if args.check:
        if changed:
            print(f'{OUT.name} is stale; run make regen-web-assets')
            return 1
        print(f'{OUT.name} is current')
    else:
        if changed: OUT.write_text(text)
        print(f'generated {OUT.name} ({sum(x[3] for x in entries)} bytes assets)')
    return 0
if __name__=='__main__': raise SystemExit(main())
