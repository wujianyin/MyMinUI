#!/bin/sh
# Runs INSIDE the kbuild-arm2 container (see run.sh).
#   /w = repo workspace/   /t = libs + headers dir   /o = output dir
# Builds the m21 (glibc, ARMv7 hard-float) binaries without the dead upstream toolchain.
set -u
CC=arm-linux-gnueabihf-gcc
ARCH="-marm -mtune=cortex-a7 -mfpu=neon -mfloat-abi=hard -march=armv7ve"
INC="-I/w/all/common -I/w/m21/platform -I/w/m21/libmsettings -I/t/sdl2_inc -I/t/sdl2_inc/SDL2 -I/t"
# crt_glibc.c: private _start so the binary runs against the device's old glibc (<= 2.17)
LNK="-Wl,--no-as-needed -nostartfiles -no-pie -L/t/m21_glibc -lSDL2 -lSDL2_image -lSDL2_ttf -lmsettings -lsamplerate -lpthread -ldl -lm -lz -l:libc.so.6 -Wl,--dynamic-linker=/lib/ld-linux-armhf.so.3 -Wl,--allow-shlib-undefined"
COMMON="/w/all/common/utils.c /w/all/common/SDL2_rotozoom.c /w/all/common/api.c /w/m21/platform/platform.c /t/crt_glibc.c"
HASH=${BUILD_HASH:-unknown}
DATE=$(date +%Y.%m.%d)
FLAGS="-fomit-frame-pointer -fno-pie -std=gnu99 -DPLATFORM=\"m21\" -DUSE_SDL2 -DIMG_GetError=SDL_GetError -DBUILD_DATE=\"$DATE\" -DBUILD_HASH=\"$HASH\""
LRC=-I/w/all/minarch/libretro-common/include

build() { name=$1; shift; echo "=== $name"
  $CC $ARCH $FLAGS $INC "$@" $LNK -o /o/$name.elf > /o/$name.log 2>&1 \
    && echo "OK $(ls -l /o/$name.elf | awk '{print $5}') bytes" \
    || { echo FAIL; grep -E "error|undefined" /o/$name.log | head -15; }; }

build minui          -Os $LRC /w/all/minui/minui.c $COMMON
build minarch        -O3 $LRC /w/all/minarch/minarch.c $COMMON
build keymon         -Os /w/m21/keymon/keymon.c
build clock          -Os /w/all/clock/clock.c $COMMON
build minput         -Os /w/all/minput/minput.c $COMMON
build clear_recent   -Os /w/all/clear_recent/clear_recent.c $COMMON
build convertboxart  -Os $LRC /w/all/convertboxart/convertboxart.c $COMMON
