#!/usr/bin/env bash
# Small recording helper: H.264/AAC MP4, WAV/raw input and CUEX's filters.
# Prerequisites: C toolchain, make, Python3, pkg-config, nasm, x264 development files.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="${1:?usage: build-ffmpeg.sh BUILD_DIRECTORY}"
mkdir -p "$WORK"
WORK="$(cd "$WORK" && pwd)"
python3 "$ROOT/tools/release_package.py" fetch ffmpeg-source --cache "$WORK/downloads"
if [ ! -d "$WORK/source" ]; then
  mkdir "$WORK/source"
  tar -xzf "$WORK/downloads/ffmpeg-source" --strip-components=1 -C "$WORK/source"
fi
# Archives nested in another Git checkout must not pick up CUEX's revision
# through FFmpeg's version.sh. RELEASE is from the SHA256-verified stable tag.
cp "$WORK/source/RELEASE" "$WORK/source/VERSION"
mkdir -p "$WORK/build" "$WORK/prefix"
cd "$WORK/build"
cross_args=()
if [ -n "${CROSS_PREFIX:-}" ]; then
  cross_args+=(--enable-cross-compile --target-os=mingw32 --arch=x86_64
    --cross-prefix="$CROSS_PREFIX" --pkg-config=pkg-config --pkg-config-flags=--static)
fi
"$WORK/source/configure" --prefix="$WORK/prefix" \
  ${cross_args[@]+"${cross_args[@]}"} \
  --disable-everything --disable-autodetect --disable-doc --disable-debug \
  --disable-shared --enable-static --disable-avdevice --disable-network \
  --enable-ffmpeg --enable-ffprobe --enable-gpl --enable-libx264 \
  --enable-protocol=file,pipe,fd --enable-demuxer=rawvideo,wav,mov,pcm_u8 \
  --enable-muxer=mp4,wav,rawvideo --enable-parser=h264,aac \
  --enable-decoder=rawvideo,pcm_u8,h264,aac \
  --enable-encoder=libx264,pcm_u8,aac,rawvideo \
  --enable-filter=crop,scale,pad,format,aresample,anull,null \
  --enable-swscale --enable-swresample --enable-avfilter
make -j"${JOBS:-4}"
make install
if [ -z "${CROSS_PREFIX:-}" ]; then "$WORK/prefix/bin/ffmpeg" -version; fi
