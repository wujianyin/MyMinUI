#!/bin/sh
# Host side. Needs Docker and the image kbuild-arm2 (docker build -f Dockerfile.kbuild-arm -t kbuild-arm .;
# docker build -f Dockerfile.arm2 -t kbuild-arm2 .).
# LIBDIR must contain: m21_glibc/ (the device's glibc libs: libc, libSDL2*, libsamplerate, libmsettings ...),
# sdl2_inc/ (SDL2 headers + samplerate.h), crt_glibc.c.   OUT receives *.elf and *.log.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../../.." && pwd)
LIBDIR=${LIBDIR:-/tmp/ub}
OUT=${OUT:-/tmp/mb}
mkdir -p "$OUT"
cp "$HERE/build.sh" "$OUT/build.sh"
docker run --rm -e BUILD_HASH="$(git -C "$REPO" rev-parse --short HEAD)" \
  -v "$LIBDIR":/t -v "$OUT":/o -v "$REPO/workspace":/w kbuild-arm2 sh /o/build.sh
