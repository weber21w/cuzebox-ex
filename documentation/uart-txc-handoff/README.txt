Portable UART-fix evidence copied on 2026-11-01.

These five files are a verified copy from the Windows workspace's ignored
_obj_/uart-txc-fix/ directory: patch, validation summary, performance summary,
valid performance.json, and the original manifest.json. They do not include
the Windows executable, original source snapshots, build logs or benchmark
binaries/scripts. Some original summary paths refer to that larger directory.

Manifest paths are relative to the active emulator source root. Its binary
entry records the previously tested Windows build, which is not part of this
portable evidence folder. Read ../../DEVELOPMENT_HANDOFF.txt for Linux commands,
limits, transfer requirements and publication status.

The UART fix is already applied to the active emulator source. The patch is
provided for review/recovery/migration; do not apply it twice. It was prepared
against our local pre-fix source, so review before applying to newer upstream.
