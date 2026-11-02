#!/usr/bin/env python3
"""Build native CUEX download packages from explicitly supplied executables.
No live configs, ROMs, saves or developer output directories are copied.
All downloads are pinned by URL and SHA256 in packaging/release/tools.lock.json.
"""
import argparse
import hashlib
import gzip
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import struct
import subprocess
import tarfile
import tempfile
import urllib.request
import zipfile

ROOT=Path(__file__).resolve().parents[1]
LOCK=json.loads((ROOT/'packaging/release/tools.lock.json').read_text())

def run(args,**kw):
    return subprocess.run([str(a) for a in args],check=True,**kw)

def output(args):
    return subprocess.check_output([str(a) for a in args],text=True)

def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()

def fetch(name,cache):
    item=LOCK[name];cache=Path(cache).resolve();cache.mkdir(parents=True,exist_ok=True)
    dest=cache/name
    if not dest.exists():
        req=urllib.request.Request(item['url'],headers={'User-Agent':'CUEX-release-packaging'})
        with urllib.request.urlopen(req,timeout=90) as src,dest.with_suffix('.part').open('wb') as dst:
            shutil.copyfileobj(src,dst)
        dest.with_suffix('.part').replace(dest)
    if digest(dest)!=item['sha256']:raise RuntimeError(f'SHA256 mismatch for {name}; review/update the tool lock, not the verification')
    dest.chmod(0o755)
    return dest

def source_files():
    # Explicit source roots and suffixes prevent shipping private game/runtime data.
    files=set()
    for p in ROOT.iterdir():
        if p.is_file() and (p.suffix in ('.c','.h','.inc') or p.name in ('Makefile','Make_config.mk','Make_defines.mk','Make_platform.mk','LICENSE','readme.md','Bootloader.hex','icon.png','icon.ico','icon.rc','.gitattributes','.gitignore','DEVELOPMENT_HANDOFF.txt','OPTIMIZATION_HANDOFF.md')):files.add(p)
    for folder in ('microui','third_party','tools','tests','packaging','documentation','.github','themes','assets','extras'):
        for directory,dirs,names in os.walk(ROOT/folder):
            dirs[:]=[d for d in dirs if d not in ('node_modules','__pycache__','_obj_','.git','build','downloads','.venv','venv')]
            for name in names:
                p=Path(directory)/name
                if p.is_symlink() or name=='custom.cfg':continue
                if p.suffix.lower() in ('.c','.cpp','.h','.inc','.s','.asm','.py','.js','.sh','.ps1','.mk','.in','.md','.txt','.cfg','.json','.yml','.yaml','.html','.css','.desktop') or name.startswith(('LICENSE','COPYING','Makefile','README')) or (folder=='third_party' and (p.suffix.lower() in ('.hex','.sha256','.xcf') or name=='.gitignore')) or (folder in ('assets','packaging') and p.suffix.lower() in ('.png','.ico')):files.add(p)
    return sorted(files,key=lambda p:p.relative_to(ROOT).as_posix())

def provenance(resources,args):
    for name in ('COPYING.GPLv2','COPYING.GPLv3','LICENSE.md'):
        notice=args.ffmpeg.parent.parent.parent/'source'/name
        if notice.is_file():shutil.copy2(notice,resources/'licenses'/('FFmpeg-'+name))
    copy_notices(args.ffmpeg.parent.parent,resources/'licenses','ffmpeg')
    manifest={str(p.relative_to(ROOT)).replace('\\','/'):digest(p) for p in source_files()}
    tree=hashlib.sha256(json.dumps(manifest,sort_keys=True).encode()).hexdigest()
    # The active nested Windows source is not its parent's Git checkout.
    # Never present the parent's old revision as this package's source revision.
    revision=None
    try:
        if Path(output(['git','-C',ROOT,'rev-parse','--show-toplevel']).strip()).resolve()==ROOT:
            revision=output(['git','-C',ROOT,'rev-parse','HEAD']).strip()
    except (OSError,subprocess.CalledProcessError):pass
    record={'version':args.version,'platform':args.platform,'architecture':args.arch,'source_revision':revision,
            'source_tree_sha256':tree,'source_files':manifest,'input_binary_sha256':digest(args.binary),
            'input_ffmpeg_sha256':digest(args.ffmpeg),'input_hash_scope':'Build inputs before dependency relocation or signing; SHA256SUMS verifies final downloads.',
            'ffmpeg_version':output([args.ffmpeg,'-version']).splitlines()[0],
            'macos_signing':'ad-hoc, not Developer ID/notarized' if args.platform=='macos' else None}
    (resources/'BUILDINFO.json').write_text(json.dumps(record,indent=2)+'\n')

def resources(dest):
    dest.mkdir(parents=True,exist_ok=True)
    for source,name in [('packaging/config.default.cfg','config.default.cfg'),('LICENSE','LICENSE'),('readme.md','readme.md'),('icon.png','icon.png')]:
        shutil.copy2(ROOT/source,dest/name)
    (dest/'themes').mkdir()
    for p in sorted((ROOT/'themes').glob('*.cfg')):
        if p.name!='custom.cfg':shutil.copy2(p,dest/'themes'/p.name)
    db=ROOT/'gamecontrollerdb.txt'
    if db.is_file():shutil.copy2(db,dest/db.name)
    (dest/'THIRD-PARTY.txt').write_text(
        'CUEX: GNU GPL version 3 or later; see LICENSE and the matching source archive.\n'
        'SDL2: zlib license, https://github.com/libsdl-org/SDL/tree/SDL2\n'
        'FFmpeg recording helper: GPL-enabled build with libx264.\n'
        'FFmpeg source/revision/SHA256 and minimal build recipe: packaging/release/tools.lock.json and build-ffmpeg.sh in the source archive.\n'
        'x264: GNU GPL version 2 or later, https://code.videolan.org/videolan/x264\n'
        'Additional shared-library notices are supplied under licenses/.\n')
    (dest/'licenses').mkdir()

def exe_copy(src,dest):
    dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dest);dest.chmod(0o755)

def copy_notices(prefix,dest,label):
    prefix=Path(prefix);candidates=list(prefix.glob('LICENSE*'))+list(prefix.glob('COPYING*'))
    for folder in ('share/doc','share/licenses'):
        if (prefix/folder).is_dir():
            candidates.extend(p for p in (prefix/folder).rglob('*') if p.is_file() and any(k in p.name.lower() for k in ('license','copying','copyright')))
    for p in candidates:
        if p.is_file():
            name=label+'-'+str(p.relative_to(prefix)).replace('/','_').replace('\\','_')
            shutil.copy2(p,dest/name)

def windows_dependencies(executables,dest,search,objdump):
    pending=list(executables);seen=set()
    dirs=[Path(p) for p in search]+[Path(e).parent for e in executables]
    dirs += [Path(p) for p in os.environ.get('PATH','').split(os.pathsep) if p]
    system=Path(os.environ.get('SystemRoot','C:/Windows'))/'System32'
    while pending:
        binary=Path(pending.pop())
        for name in re.findall(r'DLL Name:\s*(\S+)',output([objdump,'-p',binary])):
            key=name.lower()
            if key in seen:continue
            seen.add(key)
            if key.startswith(('api-ms-','ext-ms-')) or (system/name).is_file():continue
            found=next((p/name for p in dirs if (p/name).is_file()),None)
            if found is None:raise RuntimeError(f'Missing runtime DLL {name} required by {binary.name}')
            target=dest/name;shutil.copy2(found,target);pending.append(target)
    # SDK distributions supply original license text alongside the DLLs.
    for sdk in search:
        licenses=Path(sdk).parent/'share/licenses'
        if licenses.is_dir():
            shutil.copytree(licenses,dest/'licenses'/'mingw-sdk',dirs_exist_ok=True)

def appimage_tool(name,cache,args):
    binary=fetch(name,cache)
    env=dict(os.environ,APPIMAGE_EXTRACT_AND_RUN='1',ARCH='x86_64')
    run([binary,*args],env=env)

def linux_package(stage,args):
    app=stage/'CUEX.AppDir';bindir=app/'usr/bin';res=app/'usr/share/cuex'
    resources(res);(res/'cuex-user-data').touch()
    exe_copy(args.binary,bindir/'cuzebox');exe_copy(args.ffmpeg,bindir/'ffmpeg')
    if args.ffprobe:exe_copy(args.ffprobe,bindir/'ffprobe')
    desktop=app/'cuex.desktop';icon=app/'cuex.png'
    shutil.copy2(ROOT/'packaging/cuzebox.desktop',desktop);shutil.copy2(ROOT/'icon.png',icon)
    # Match Desktop Entry icon and preserve ROM arguments including spaces.
    desktop.write_text(desktop.read_text().replace('Icon=cuzebox','Icon=cuex').replace('Exec=cuzebox','Exec=cuzebox %f'))
    launcher='''#!/bin/sh
set -eu
HERE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
export PATH="$HERE/usr/bin:$PATH"
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$HERE/usr/bin/cuzebox" --resource-dir "$HERE/usr/share/cuex" "$@"
'''
    (app/'AppRun').write_text(launcher);(app/'AppRun').chmod(0o755)
    params=['--appdir',app,'--executable',bindir/'cuzebox','--executable',bindir/'ffmpeg','--desktop-file',desktop,'--icon-file',icon]
    if args.ffprobe:params+=['--executable',bindir/'ffprobe']
    appimage_tool('linuxdeploy-x86_64',args.cache,params)
    # Keep our launcher: linuxdeploy may create an AppRun link only when absent.
    provenance(res,args)
    libraries=set()
    for binary in (args.binary,args.ffmpeg):
        libraries.update(re.findall(r'=> (/\S+)',output(['ldd',binary])))
    for lib in libraries:
        try:
            owner=subprocess.check_output(['dpkg-query','-S',str(Path(lib).resolve())],text=True,stderr=subprocess.DEVNULL).split(':',1)[0]
            notice=Path('/usr/share/doc')/owner/'copyright'
            if notice.is_file():shutil.copy2(notice,res/'licenses'/f'{owner}.copyright')
        except (OSError,subprocess.CalledProcessError):pass
    base=f'CUEX-{args.version}-linux-{args.arch}'
    archive=args.out/(base+'.tar.gz')
    with tarfile.open(archive,'w:gz') as tar:tar.add(app,arcname='CUEX')
    image=args.out/(base+'.AppImage')
    runtime=fetch('runtime-x86_64',args.cache)
    appimage_tool('appimagetool-x86_64',args.cache,['--runtime-file',runtime,'--mksquashfs-opt=-processors','--mksquashfs-opt=2',app,image])
    return [archive,image]

def macho_deps(path):
    return [line.strip().split(' (',1)[0] for line in output(['otool','-L',path]).splitlines()[1:]]

def macho_rpaths(path):
    return re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.+?) \(offset',output(['otool','-l',path]))

def macos_libraries(executables,frameworks,license_dir=None):
    frameworks.mkdir();pending=[(Path(p),Path(p)) for p in executables];seen={};copied=[]
    while pending:
        original,target=pending.pop()
        for dep in macho_deps(original):
            if dep.startswith(('/System/Library/','/usr/lib/')):continue
            candidates=[Path(dep)] if Path(dep).is_absolute() else []
            if dep.startswith('@loader_path/'):
                candidates=[original.parent/dep[len('@loader_path/'):]]
            if dep.startswith('@rpath/'):
                for prefix in macho_rpaths(original):
                    prefix=prefix.replace('@loader_path',str(original.parent)).replace('@executable_path',str(executables[0].parent))
                    candidates.append(Path(prefix)/dep[len('@rpath/'):])
                candidates += [Path('/opt/homebrew/lib')/Path(dep).name,Path('/usr/local/lib')/Path(dep).name]
            source=next((p.resolve() for p in candidates if p.is_file()),None)
            if source is None:raise RuntimeError(f'Cannot resolve macOS dependency {dep} of {original}')
            dest=frameworks/source.name
            if dest.name in seen and seen[dest.name]!=source:raise RuntimeError(f'Conflicting library basename: {source}')
            if dest.name not in seen:
                seen[dest.name]=source;shutil.copy2(source,dest);dest.chmod(0o755)
                if license_dir:copy_notices(source.parent.parent,license_dir,dest.name)
                run(['install_name_tool','-id','@rpath/'+dest.name,dest]);pending.append((source,dest));copied.append(dest)
            replacement=('@loader_path/' if target.parent==frameworks else '@executable_path/../Frameworks/')+dest.name
            run(['install_name_tool','-change',dep,replacement,target])
    return copied

def macos_package(stage,args):
    app=stage/'CUEX.app';contents=app/'Contents';bindir=contents/'MacOS';res=contents/'Resources'
    resources(res);(res/'cuex-user-data').touch()
    exe_copy(args.binary,bindir/'cuzebox');exe_copy(args.ffmpeg,bindir/'ffmpeg')
    executables=[bindir/'cuzebox',bindir/'ffmpeg']
    if args.ffprobe:exe_copy(args.ffprobe,bindir/'ffprobe');executables.append(bindir/'ffprobe')
    # Resolve libraries from original binaries before changing their load paths.
    originals=[args.binary,args.ffmpeg]+([args.ffprobe] if args.ffprobe else [])
    for p in originals:
        arches=output(['lipo','-archs',p]).split()
        if args.arch not in arches:raise RuntimeError(f'{p} does not contain {args.arch}')
    libs=contents/'Frameworks';libs.mkdir()
    # Dependency traversal expects target executables; their absolute Homebrew
    # load paths are still intact after copying.
    libs.rmdir();copied=macos_libraries(executables,libs,res/'licenses')
    for p in copied:
        if args.arch not in output(['lipo','-archs',p]).split():raise RuntimeError(f'Wrong architecture in bundled library: {p.name}')
    icons=stage/'cuex.iconset';icons.mkdir()
    for size in (16,32,128,256,512):
        for retina in (False,True):
            pixels=size*(2 if retina else 1);name=f'icon_{size}x{size}'+('@2x' if retina else '')+'.png'
            run(['sips','-z',pixels,pixels,ROOT/'icon.png','--out',icons/name],stdout=subprocess.DEVNULL)
    run(['iconutil','-c','icns',icons,'-o',res/'cuex.icns'])
    numeric=re.search(r'\d+(?:\.\d+){0,2}',args.version)
    info={'CFBundleIdentifier':'us.uzenet.CUEX','CFBundleName':'CUEX','CFBundleDisplayName':'CUEX','CFBundleExecutable':'cuzebox',
          'CFBundlePackageType':'APPL','CFBundleShortVersionString':numeric.group(0) if numeric else '0.0.0',
          'CFBundleVersion':'1','CFBundleIconFile':'cuex.icns','LSMinimumSystemVersion':'15.0',
          'NSHighResolutionCapable':True}
    with (contents/'Info.plist').open('wb') as f:plistlib.dump(info,f)
    provenance(res,args)
    # Ad-hoc signing makes native Apple Silicon test packages runnable. This
    # is deliberately not described as Developer ID signing or notarization.
    for p in copied+executables:run(['codesign','--force','--sign','-',p])
    run(['codesign','--force','--sign','-',app]);run(['codesign','--verify','--deep','--strict',app])
    archive=args.out/f'CUEX-{args.version}-macos-{args.arch}-unsigned.zip'
    write_zip(archive,app,app.name)
    return [archive]

def write_zip(archive,folder,prefix):
    with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for p in sorted(folder.rglob('*')):
            if p.is_file():z.write(p,Path(prefix)/p.relative_to(folder))

def windows_package(stage,args):
    app=stage/'CUEX';resources(app)
    exe_copy(args.binary,app/'cuzebox.exe');exe_copy(args.ffmpeg,app/'ffmpeg.exe')
    executables=[app/'cuzebox.exe',app/'ffmpeg.exe']
    if args.ffprobe:exe_copy(args.ffprobe,app/'ffprobe.exe');executables.append(app/'ffprobe.exe')
    windows_dependencies(executables,app,[args.binary.parent,args.ffmpeg.parent,*args.dependency_dir],args.objdump)
    (app/'cuex-portable').touch();shutil.copy2(ROOT/'packaging/config.default.cfg',app/'config.cfg')
    (app/'START-HERE.txt').write_text('Run cuzebox.exe. This ZIP defaults to portable data beside the executable.\nUse cuzebox.exe --user-data to keep settings/saves in your account instead.\nKeep the DLLs and ffmpeg.exe with the executable. No games are included.\n')
    provenance(app,args)
    archive=args.out/f'CUEX-{args.version}-windows-{args.arch}.zip';write_zip(archive,app,app.name)
    return [archive]

def build(args):
    for name in ('binary','ffmpeg'):
        setattr(args,name,Path(getattr(args,name)).resolve())
        if not getattr(args,name).is_file():raise RuntimeError(f'Missing {name}: {getattr(args,name)}')
    if args.ffprobe:args.ffprobe=Path(args.ffprobe).resolve()
    for p in (args.binary,args.ffmpeg):
        with p.open('rb') as f:header=f.read(64)
        if args.platform=='linux' and (header[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',header,18)[0]!=62):raise RuntimeError(f'Expected a Linux x86_64 ELF executable: {p}')
        if args.platform=='windows':
            if header[:2]!=b'MZ':raise RuntimeError(f'Expected a Windows PE executable: {p}')
            with p.open('rb') as f:f.seek(struct.unpack_from('<I',header,60)[0]);signature=f.read(6)
            if signature!=b'PE\0\0\x64\x86':raise RuntimeError(f'Expected a Windows x86_64 executable: {p}')
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,80}',args.version):raise RuntimeError('Invalid release version')
    if args.platform=='linux' and args.arch!='x86_64':raise RuntimeError('The pinned AppImage toolchain currently supports x86_64 only')
    args.out=Path(args.out).resolve();args.out.mkdir(parents=True,exist_ok=True)
    stage=Path(tempfile.mkdtemp(prefix='cuex-stage-',dir=args.out))
    try:
        artifacts={'windows':windows_package,'linux':linux_package,'macos':macos_package}[args.platform](stage,args)
        source=args.out/f'CUEX-{args.version}-source.tar.gz'
        with source.open('wb') as raw,gzip.GzipFile(filename='',mode='wb',fileobj=raw,mtime=0) as compressed,tarfile.open(fileobj=compressed,mode='w') as tar:
            for p in source_files():
                name=str(Path('CUEX')/p.relative_to(ROOT)).replace('\\','/')
                item=tar.gettarinfo(str(p),arcname=name)
                item.uid=item.gid=item.mtime=0;item.uname=item.gname=''
                item.mode=0o755 if p.suffix in ('.sh','.py') else 0o644
                with p.open('rb') as data:tar.addfile(item,data)
        artifacts.append(source)
        helper_source=args.ffmpeg.parent.parent.parent/'downloads/ffmpeg-source'
        if helper_source.is_file() and digest(helper_source)==LOCK['ffmpeg-source']['sha256']:
            copy=args.out/f"FFmpeg-{LOCK['ffmpeg-source']['version']}-source.tar.gz"
            shutil.copy2(helper_source,copy);artifacts.append(copy)
        x264_source=args.ffmpeg.parent.parent.parent/'downloads/x264-source'
        if x264_source.is_file() and digest(x264_source)==LOCK['x264-source']['sha256']:
            copy=args.out/f"x264-{LOCK['x264-source']['revision']}-source.tar.gz"
            shutil.copy2(x264_source,copy);artifacts.append(copy)
        manifest=args.out/f'CUEX-{args.version}-{args.platform}-{args.arch}-SHA256SUMS.txt'
        manifest.write_text(''.join(f'{digest(p)}  {p.name}\n' for p in artifacts))
        print(json.dumps({'artifacts':[str(p) for p in artifacts+[manifest]]},indent=2))
    finally:
        # Only remove the fresh staging directory created under this output
        # directory; never clean a user's supplied package/source directory.
        if stage.resolve().parent!=args.out or not stage.name.startswith('cuex-stage-'):raise RuntimeError('Unsafe staging cleanup target')
        shutil.rmtree(stage)

def main():
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    f=sub.add_parser('fetch');f.add_argument('name',choices=LOCK);f.add_argument('--cache',default='_obj_/release-downloads')
    b=sub.add_parser('build');b.add_argument('--platform',choices=['windows','linux','macos'],required=True)
    b.add_argument('--arch',default='x86_64');b.add_argument('--version',required=True);b.add_argument('--binary',required=True)
    b.add_argument('--ffmpeg',required=True);b.add_argument('--ffprobe');b.add_argument('--out',default='dist')
    b.add_argument('--cache',default='_obj_/release-downloads');b.add_argument('--dependency-dir',action='append',default=[])
    b.add_argument('--objdump',default='objdump')
    args=p.parse_args()
    if args.command=='fetch':print(fetch(args.name,args.cache))
    else:build(args)

if __name__=='__main__':main()
