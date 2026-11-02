"""Collect release assets, accepting equivalent gzip wrappers of identical source."""
from pathlib import Path
import gzip,hashlib,shutil,sys,zlib

source=Path(sys.argv[1]).resolve();dest=Path(sys.argv[2]).resolve();dest.mkdir(parents=True,exist_ok=True)
digest=lambda data:hashlib.sha256(data).hexdigest()
accepted={}
for p in sorted(source.rglob('*')):
    if not p.is_file():continue
    data=p.read_bytes();target=dest/p.name
    if target.exists():
        previous=target.read_bytes()
        if previous!=data:
            equivalent=False
            if p.name.startswith('CUEX-') and p.name.endswith('-source.tar.gz'):
                try:equivalent=gzip.decompress(previous)==gzip.decompress(data)
                except (OSError,EOFError,zlib.error):pass
            if not equivalent:
                raise RuntimeError(f'Conflicting release asset {p.name}; all jobs must package identical source')
            # Python/zlib versions may produce different gzip streams for the
            # exact same TAR bytes. Retain the first, verified source archive.
        accepted.setdefault(p.name,set()).add(digest(data))
        continue
    shutil.copy2(p,target)
    accepted.setdefault(p.name,set()).add(digest(data))

# Per-platform manifests originally refer to that platform's gzip variant.
# Validate every declared input hash, then reconcile final download hashes.
for manifest in sorted(dest.glob('*SHA256SUMS.txt')):
    lines=[]
    for line in manifest.read_text().splitlines():
        if not line.strip():continue
        expected,name=line.split(None,1);name=name.lstrip('*')
        if name not in accepted or expected not in accepted[name]:
            raise RuntimeError(f'Checksum mismatch in {manifest.name}: {name}')
        lines.append(f'{digest((dest/name).read_bytes())}  {name}\n')
    manifest.write_text(''.join(lines),newline='\n')
