#!/bin/sh
# Runs INSIDE the kbuild-arm2 container (see run.sh).
#   /w = repo workspace/   /t = libs + headers dir   /o = output dir
# Builds the m21 (glibc, ARMv7 hard-float) binaries without the dead upstream toolchain.
set -u
CC=arm-linux-gnueabihf-gcc
ARCH="-marm -mtune=cortex-a7 -mfpu=neon -mfloat-abi=hard -march=armv7ve"
INC="-I/w/all/common -I/w/m21/platform -I/w/m21/libmsettings -I/t/sdl2_inc -I/t/sdl2_inc/SDL2 -I/t"
# crt_glibc.c: private _start so the binary runs against the device's old glibc (<= 2.17)
LNK="-Wl,--no-as-needed -nostartfiles -no-pie -L/o -L/t/m21_glibc -lSDL2 -lSDL2_image -lSDL2_ttf -lmsettings -lsamplerate -lpthread -ldl -lm -lz -l:libc.so.6 -Wl,--dynamic-linker=/lib/ld-linux-armhf.so.3 -Wl,--allow-shlib-undefined"
COMMON="/w/all/common/utils.c /w/all/common/SDL2_rotozoom.c /w/all/common/api.c /w/m21/platform/platform.c /o/crt.o"
HASH=${BUILD_HASH:-unknown}
DATE=$(date +%Y.%m.%d)
FLAGS="-fomit-frame-pointer -fno-pie -std=gnu99 -DPLATFORM=\"m21\" -DUSE_SDL2 -DIMG_GetError=SDL_GetError -DBUILD_DATE=\"$DATE\" -DBUILD_HASH=\"$HASH\""
LRC=-I/w/all/minarch/libretro-common/include

# crt compiled separately and WITHOUT lto, otherwise lto drops main (undefined reference to main)
$CC $ARCH -O2 -fno-lto -fno-pie -c /t/crt_glibc.c -o /o/crt.o || exit 1

build() { name=$1; shift; echo "=== $name"
  $CC $ARCH $FLAGS $INC "$@" $LNK -o /o/$name.elf > /o/$name.log 2>&1 \
    && echo "OK $(ls -l /o/$name.elf | awk '{print $5}') bytes" \
    || { echo FAIL; grep -E "error|undefined" /o/$name.log | head -15; }; }

# NOTE: msettings.c SaveSettings() has an upstream bug (open without O_CREAT, writes &settings i.e. the pointer);
# newer gcc flags it, so only that warning is demoted to keep upstream behaviour. See README.
# libmsettings first: always from current source (SETTINGS_VERSION/struct changed after 20260208)
echo "=== libmsettings"
$CC $ARCH -Os -fpic -Werror -Wno-error=stringop-overread -I/w/m21/libmsettings -c /w/m21/libmsettings/msettings.c -o /o/msettings.o > /o/libmsettings.log 2>&1 \
  && $CC -shared -o /o/libmsettings.so /o/msettings.o -L/t/m21_glibc -l:libdl.so.2 -l:librt.so.1 -l:libc.so.6 -s >> /o/libmsettings.log 2>&1 \
  && echo "OK $(ls -l /o/libmsettings.so | awk '{print $5}') bytes" || { echo FAIL; cat /o/libmsettings.log | head; }

build minui          -Os -flto $LRC /w/all/minui/minui.c $COMMON
build minarch        -O3 -flto=auto -ftree-vectorize -funroll-loops -funsafe-math-optimizations $LRC /w/all/minarch/minarch.c $COMMON
build keymon         -Os /w/m21/keymon/keymon.c
build clock          -Os /w/all/clock/clock.c $COMMON
build minput         -Os /w/all/minput/minput.c $COMMON
build clear_recent   -Os /w/all/clear_recent/clear_recent.c $COMMON
build convertboxart  -Os $LRC /w/all/convertboxart/convertboxart.c $COMMON
build show           -Ofast $LRC /w/m21/show/show.c $COMMON
echo "=== overclock"
$CC $ARCH -Os -fno-pie -DPLATFORM=\"m21\" -I. /w/m21/overclock/overclock.c /o/crt.o -nostartfiles -no-pie -L/t/m21_glibc -l:libdl.so.2 -l:librt.so.1 -l:libc.so.6 -Wl,--dynamic-linker=/lib/ld-linux-armhf.so.3 -Wl,--gc-sections -o /o/overclock.elf > /o/overclock.log 2>&1 && echo "OK" || { echo FAIL; head /o/overclock.log; }
# strip everything we built (the original makefiles pass -s)
for f in /o/*.elf; do arm-linux-gnueabihf-strip "$f"; done

# guard: the device rootfs has glibc 2.26; anything newer (e.g. 2.34 from the build host) will not start there
bad=0
for f in /o/*.elf /o/libmsettings.so; do
  v=$(arm-linux-gnueabihf-readelf -V "$f" 2>/dev/null | grep -o "GLIBC_[0-9.]*" | sed 's/GLIBC_//' | sort -uV | tail -1)
  case "$v" in 2.34|2.35|2.36|2.37|2.38|2.39|2.4[0-9]) echo "TOO NEW: $f needs GLIBC_$v"; bad=1;; esac
  echo "$(basename $f): max GLIBC_$v"
done
[ $bad = 0 ] || { echo "glibc version check FAILED"; exit 1; }
