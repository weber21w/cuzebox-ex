#!/usr/bin/env python3
"""Read-only checks of Git's proposed source set, including source archives."""
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LOCAL_DIRS = {"_obj_", "roms", "profiles", "cheats", "savestates", "screenshots", "video"}
LOCAL_NAMES = {"config.cfg", "esp.cfg", "eeprom.bin", "romdump.bin", "rom.bin", "dummybin"}
BINARY_SUFFIXES = {".exe", ".dll", ".o", ".obj", ".so", ".dylib", ".czs", ".log", ".orig", ".pyc"}
SECRET_PATTERNS = [
    re.compile(rb"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----"),
    re.compile(rb"\bgh[pousr]_[A-Za-z0-9]{30,}\b"),
    re.compile(rb"\bgithub_pat_[A-Za-z0-9_]{40,}\b"),
    re.compile(rb"\bAKIA[A-Z0-9]{16}\b"),
    re.compile(rb"\bsk-(?:proj-)?[A-Za-z0-9_-]{40,}\b"),
]


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


def source_files():
    probe = subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=ROOT,
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    if probe.returncode == 0 and Path(probe.stdout.decode().strip()).resolve() == ROOT:
        return git("ls-files", "--cached", "--others", "--exclude-standard", "-z").split(b"\0")
    # A temporary index lets Git apply the real ignore rules without creating
    # metadata or changing the contents of this source archive.
    with tempfile.TemporaryDirectory(prefix="cuzebox-audit-") as temp:
        git("init", "--quiet", temp)
        return git(f"--git-dir={temp}/.git", f"--work-tree={ROOT}",
                   "ls-files", "--others", "--exclude-standard", "-z").split(b"\0")


def main():
    findings = []
    checked = 0
    for entry in sorted(set(source_files())):
        if not entry:
            continue
        name = entry.decode("utf-8", errors="surrogateescape")
        rel = PurePosixPath(name)
        path = ROOT / name
        if not path.is_file():  # Deleted tracked files and directories.
            continue
        checked += 1
        if (rel.parts[0] in LOCAL_DIRS or name in LOCAL_NAMES or
                rel.suffix.lower() in BINARY_SUFFIXES or
                (rel.name.startswith(".env") and rel.name != ".env.example")):
            findings.append(f"{name}: local data or build/backup artifact would be published")
        data = path.read_bytes()
        for pattern in SECRET_PATTERNS:
            match = pattern.search(data)
            if match:
                line = data[:match.start()].count(b"\n") + 1
                findings.append(f"{name}:{line}: possible credential; inspect locally")
    for finding in findings:
        print(f"FAIL: {finding}", file=sys.stderr)
    print(f"Source audit: {checked} files checked; {len(findings)} findings.")
    return 1 if findings else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"Source audit could not complete: {type(exc).__name__}", file=sys.stderr)
        sys.exit(2)
