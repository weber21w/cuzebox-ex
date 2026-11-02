#!/usr/bin/env bash
# Optional Linux -> Windows x86-64 helper build. Native CI uses build-ffmpeg.sh.
# Requires MinGW-w64 cross GCC/binutils, make, pkg-config, nasm and Python3.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="${1:?usage: build-windows-ffmpeg.sh BUILD_DIRECTORY}"
mkdir -p "$WORK"
WORK="$(cd "$WORK" && pwd)"
export CROSS_PREFIX="${CROSS_PREFIX:-x86_64-w64-mingw32-}"
python3 "$ROOT/tools/release_package.py" fetch x264-source --cache "$WORK/downloads"
if [ ! -d "$WORK/x264-source" ]; then
  mkdir "$WORK/x264-source"
  tar -xzf "$WORK/downloads/x264-source" --strip-components=1 -C "$WORK/x264-source"
fi
mkdir -p "$WORK/x264-build" "$WORK/prefix/share/licenses/x264"
cd "$WORK/x264-build"
"$WORK/x264-source/configure" --prefix="$WORK/prefix" --enable-static --disable-cli \
  --host=x86_64-w64-mingw32 --cross-prefix="$CROSS_PREFIX"
make -j"${JOBS:-4}"
make install
cp "$WORK/x264-source/COPYING" "$WORK/prefix/share/licenses/x264/COPYING"
export PKG_CONFIG_LIBDIR="$WORK/prefix/lib/pkgconfig"
export PKG_CONFIG_PATH="$PKG_CONFIG_LIBDIR"
bash "$ROOT/packaging/release/build-ffmpeg.sh" "$WORK"
