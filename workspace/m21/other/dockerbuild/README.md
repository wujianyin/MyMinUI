# Local m21 build (without the dead upstream toolchain)

> 中文版: [README.zh-CN.md](README.zh-CN.md) | Back to [README](../../../../README.md)

The upstream `makefile.toolchain` clones `shauninman/union-m21-toolchain`, which no longer exists. This directory uses `arm-linux-gnueabihf-gcc` in Docker plus the device's own glibc
libraries to build the m21 glibc programs directly (the same method as `USB Mode.pak/usbmode.elf`).

## Contents

- `Dockerfile.kbuild-arm`, `Dockerfile.arm2`: build the image `kbuild-arm2` (Debian bookworm + ARM cross toolchain + libc6-dev-armhf-cross).
- `build.sh`: runs inside the container; builds minui, minarch, keymon, clock, minput, clear_recent, convertboxart, **language** (the language picker) and **usbmode** (USB Mode UI).
- `run.sh`: host entry point. `LIBDIR` (default `/tmp/ub`) holds libraries and headers, `OUT` (default `/tmp/mb`) receives the products.
- `package.sh`: assembles the release zip (see below).
- `verify.sh`: loads every built program with the real device glibc under qemu (see below).
- `build_fsckfat.sh`: builds a static ARM `fsck.fat` for repairing a read-only SD card, see [SD_RESTORE.md](../../../../SD_RESTORE.md) section B.

## What LIBDIR needs (not in the repository, no binary libraries are committed)

- `m21_glibc/`: libc, libm, libpthread, libdl, libz, libSDL2, libSDL2_image, libSDL2_ttf, libsamplerate, libmsettings from the device rootfs. libmsettings can also be rebuilt from `workspace/m21/libmsettings`.
- `sdl2_inc/`: SDL2 headers and `samplerate.h`.
- `crt_glibc.c`: provides `_start` so the products start on the device's old glibc (<= 2.17).
- The current `sdl2_inc` is incomplete (declarations of `IMG_GetError`, `IMG_SavePNG_RW`, `TTF_SetFontOutline` are missing), so `build.sh` uses `-DIMG_GetError=SDL_GetError` as a workaround;
  the other two only give implicit-declaration warnings and their return values do not matter. With complete SDL2_image/SDL2_ttf headers the workarounds can go.

## Verify (always before packaging)

`verify.sh` uses the real device rootfs (glibc 2.26) from the upstream release package plus qemu-user to "load" every built program, which reproduces errors like
`version GLIBC_x not found` before the card is touched. `build.sh` also ends with a glibc version check (2.34 and above is an error).
`LIBDIR/m21_glibc/` also needs `librt.so.1` (take `/lib/librt-2.26.so` from the device rootfs).

## Package

After `run.sh`, `package.sh` assembles the release package following the `setup`/`system`/`cores`/`package` steps of the top-level makefile and writes `releases/MyMinUI-<date>b-0-m21.zip`:

```sh
LIBDIR=/tmp/ub OUT=/tmp/mb sh workspace/m21/other/dockerbuild/run.sh      # build, products in /tmp/mb
BIN=/tmp/mb sh workspace/m21/other/dockerbuild/verify.sh                  # qemu link check
BASEZIP=/path/MyMinUI-20260208b-0-m21.zip BIN=/tmp/mb sh workspace/m21/other/dockerbuild/package.sh
```

- Built here: minui, minarch, keymon, libmsettings, show, overclock, **language.elf** (to `.system/m21/bin/`), **usbmode.elf** (to `Tools/m21/USB Mode.pak/`), and the programs of the Clock/Input/Clear Recently Played/Convert BoxArt paks.
- Chinese: the same-name font replacements in `workspace/m21/other/cjkfont/` are put into the package by `package.sh` (`.system/res/BPreplayBold-unhinted.otf` and `Tools/m21/Files.pak/res/FreeSans.ttf`), no manual install needed.
- UI languages: `skeleton/SYSTEM/res/lang/` and each pak's `lang/` go into the package as they are, see [I18N.md](../../../../I18N.md).
- Taken from the last upstream m21 release (`BASEZIP`): `rootfs.ext2`, the 24 core `.so` files, `MyCommander.elf` (Files.pak), `prboom.wad`, `m21/libmusl/libmsettings.so`.
  These cannot be built locally today (cores need much memory and network access to clone sources, Commander is C++); they come from the same source as the previous release.
- No `ZXS.pak` (the fuse core has no m21 build yet).
- Contains our own `USB Mode.pak` (in the skeleton) and the `Language.pak` tool.
- `tomato` is `workspace/m21/other/usbmode/tomato.m21` (11749 bytes; upstream `boot.sh` + USB boot logic). The package root also holds a copy of `tomato` and `emulationstation`, so you can extract the zip straight into the FAT32 card root;
  `unzip` in `boot.sh` has `-x "m21/*" tomato emulationstation`, so an update never overwrites the running script.
- Package layout equals the upstream 20260208b m21 package: `Bios Cheats Emus Roms Tools m21 MinUI.zip MANUAL.txt` (the old upstream package had `README.txt`, the new makefile renamed it to `MANUAL.txt`).

## Lessons of the first on-device test (2026-10-24)

The first package stuck on the colour bar on the M22Pro: `.userdata/m21/logs/minui.txt` said
`libc.so.6: version GLIBC_2.34 not found (required by .system/m21/lib/libmsettings.so)`.
Cause: `libmsettings.so` and `overclock.elf` were linked against the build machine's libc (Debian bookworm, glibc 2.36), so `shm_open` bound to 2.34, while the device rootfs only has glibc 2.26. The other programs had been linked against the device libraries.
Fix: both are now linked against the device libraries (`libc.so.6`, `libdl.so.2`, `librt.so.1`), plus the version check above and `verify.sh`.

## Problems found and fixed on the device (2026-10-24)

- **GBA only 30 fps, crackling audio:** to support the Sharp scaler upstream changed the display layer from RGB565 to ARGB8888, which doubles the video memory written per frame (about 4.9 MB to 9.8 MB on the 1080x1920 panel),
  the draw thread went from about 35% to 60% CPU, a frame exceeded 16.7 ms and every frame missed vsync, stuck at 30 fps.
  Fix: the m21 display layer is always RGB565 and there is no Sharp (`NO_SHARP_SCALER` in `platform.h`; only Near is left in the menu). Measured: 16.8 ms submit interval, 59.4 fps.
  How it was located: `workspace/m21/other/dtb_60fps/pantrace` (measures `DISP_LAYER_SET_CONFIG`/`FBIOPAN_DISPLAY` intervals); the old minarch ran the same game at 59.5 fps as a reference.
- **HUD CPU frequency always 0:** `static long a[9]` in `getCPUusage()` but 10 values are read, writing out of bounds over a neighbouring variable; with another compiler it zeroes `cur_cpu_freq`. Now `a[10]`.
- **HUD font big and blurry:** upstream picks the 15x24 font by the largest picture side, and the m21 720x1280 picture is also scaled by 1.5x by the display engine. `HUD_FONT_MID_MAX 1280` in `platform.h` keeps 10x16.
- **Brightness/volume not saved** (old upstream bug, fixed): `open()` in `SaveSettings()` had no `O_CREAT` and `write()` wrote the address of the pointer `&settings`.
  Now `O_CREAT|O_TRUNC`, `write(fd, settings, ...)` and `close()`; loading validates version and ranges, `jack`/`hdmi` are not persisted.
  Note: before updating to the fixed version, an old keymon still running overwrites `msettings.bin` with garbage, so restart completely after the update; a garbage file is discarded by the validation and defaults are used.

## Status (2026-10-25)

- Upstream HEAD (`72ed4f3`) minui, minarch, keymon, clock, minput, clear_recent, convertboxart, plus our language and usbmode, all **compile and link**; the glibc symbol versions needed are not above 2.17.
- GBA verified on an SJGAM M22Pro (see above). Other cores and PICO-8 were not regression-tested one by one; back up `.system/m21` before deploying.
- `libmsettings`, `show` and `overclock` are covered too, with the same optimisation flags as upstream (minui `-Os -flto`, minarch `-O3 -flto=auto` ...).
- Not covered: `Commander` (C++), the core `.so` files, the fuse core.
