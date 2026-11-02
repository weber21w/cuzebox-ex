"""Merge architecture artifacts without silently replacing different sources."""
from pathlib import Path
import hashlib,shutil,sys
source=Path(sys.argv[1]).resolve();dest=Path(sys.argv[2]).resolve();dest.mkdir(parents=True,exist_ok=True)
for p in sorted(source.rglob('*')):
    if not p.is_file():continue
    target=dest/p.name
    if target.exists():
        if hashlib.sha256(target.read_bytes()).digest()!=hashlib.sha256(p.read_bytes()).digest():
            raise RuntimeError(f'Conflicting release asset {p.name}; all jobs must package identical source')
        else:continue
    shutil.copy2(p,target)
