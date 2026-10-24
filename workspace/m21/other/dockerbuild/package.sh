#!/bin/bash
# Host side (macOS/Linux). Assembles the m21 release zip the same way `make m21` does
# (setup + system + cores + package targets of the top-level makefile), but:
#   - the binaries come from run.sh (BIN, default /tmp/mb)
#   - things we cannot rebuild yet are taken from the last upstream m21 release (BASEZIP):
#       rootfs.ext2, the 24 core .so files, MyCommander.elf (Files.pak), prboom.wad,
#       m21/libmusl/libmsettings.so (musl build used only by the stock-side boot stage)
#   - the ZXS (fuse core) pak is left out because there is no fuse_libretro.so for m21 yet
#
#   BASEZIP=/path/MyMinUI-20260208b-0-m21.zip BIN=/tmp/mb sh package.sh
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../../.." && pwd)
BIN=${BIN:-/tmp/mb}
BASEZIP=${BASEZIP:-/tmp/rel/MyMinUI-20260208b-0-m21.zip}
PLATFORM=m21
HASH=$(git -C "$REPO" rev-parse --short HEAD)
RELEASE_NAME=${RELEASE_NAME:-MyMinUI-$(TZ=GMT date +%Y%m%d)b-0}
B=${B:-/tmp/pkg}
OLD=$B.old

cd "$REPO"
for f in minui minarch keymon clock minput clear_recent convertboxart show overclock libmsettings; do
  ext=elf; [ $f = libmsettings ] && ext=so
  [ -f "$BIN/$f.$ext" ] || { echo "missing $BIN/$f.$ext (run run.sh first)"; exit 1; }
done
[ -f "$BASEZIP" ] || { echo "missing BASEZIP $BASEZIP"; exit 1; }

# ---- things taken from the last upstream release -------------------------------------------
rm -rf "$OLD"; mkdir -p "$OLD/outer" "$OLD/inner"
(cd "$OLD/outer" && unzip -q "$BASEZIP")
(cd "$OLD/inner" && unzip -q ../outer/MinUI.zip)

# ---- setup ---------------------------------------------------------------------------------
rm -rf "$B"
mkdir -p "$B/SYSTEM" "$B/EXTRAS/Emus" "$B/EXTRAS/Tools" "$B/PAYLOAD"
cp -R skeleton/BASE "$B/BASE"
cp -R skeleton/SYSTEM/res "$B/SYSTEM/res"
cp -R skeleton/SYSTEM/$PLATFORM "$B/SYSTEM/$PLATFORM"
cp -R skeleton/EXTRAS/Bios "$B/EXTRAS/Bios"
cp -R skeleton/EXTRAS/Roms "$B/EXTRAS/Roms"
cp -R skeleton/EXTRAS/Cheats "$B/EXTRAS/Cheats"
cp -R skeleton/EXTRAS/Emus/$PLATFORM "$B/EXTRAS/Emus/$PLATFORM"
cp -R skeleton/EXTRAS/Tools/$PLATFORM "$B/EXTRAS/Tools/$PLATFORM"
rm -rf "$B/EXTRAS/Emus/$PLATFORM/ZXS.pak"          # no fuse core for m21 yet
find "$B" -type f -name '.keep' -delete
find "$B" -type f -name '*.meta' -delete

# ---- platform/makefile.copy ----------------------------------------------------------------
S=$B/SYSTEM/$PLATFORM
cp "$BIN/show.elf" "$BIN/overclock.elf" "$S/bin/"
mkdir -p "$B/BASE/$PLATFORM"
cp workspace/$PLATFORM/boot/boot.sh "$B/BASE/$PLATFORM/emulationstation"
cp workspace/$PLATFORM/boot/boot.sh "$B/BASE/$PLATFORM/tomato"
cp workspace/$PLATFORM/boot/*.png "$B/BASE/$PLATFORM/"
cp -R workspace/$PLATFORM/boot/libmusl "$B/BASE/$PLATFORM/"
cp -R workspace/$PLATFORM/boot/binmusl "$B/BASE/$PLATFORM/"
cp "$OLD/inner/.system/$PLATFORM/rootfs.ext2" "$S/"
cp "$OLD/outer/$PLATFORM/libmusl/libmsettings.so" "$B/BASE/$PLATFORM/libmusl/"
cp workspace/$PLATFORM/boot/update.sh "$S/bin/install.sh"

# ---- system target -------------------------------------------------------------------------
cp "$BIN/keymon.elf"  "$S/bin/"
cp "$BIN/libmsettings.so" "$S/lib/"
cp "$BIN/minui.elf"   "$S/bin/"
cp "$BIN/minarch.elf" "$S/bin/"
T=$B/EXTRAS/Tools/$PLATFORM
cp "$BIN/clock.elf"          "$T/Clock.pak/"
cp "$BIN/minput.elf"         "$T/Input.pak/"
cp "$BIN/clear_recent.elf"   "$T/Clear Recently Played.pak/"
cp "$BIN/convertboxart.elf"  "$T/Convert BoxArt.pak/"
cp "$OLD/outer/Tools/$PLATFORM/Files.pak/MyCommander.elf" "$T/Files.pak/"
cp -r workspace/all/Commander/res "$T/Files.pak/"

# ---- cores target --------------------------------------------------------------------------
mkdir -p "$S/cores"
cp "$OLD/inner/.system/$PLATFORM/cores/"*.so "$S/cores/"
cp "$OLD/outer/Bios/DOOM/prboom.wad" "$B/EXTRAS/Bios/DOOM/"

# ---- readme / version / commits ------------------------------------------------------------
docker run --rm -v "$REPO":/r kbuild-arm2 sh -c 'fmt -w 40 -s /r/MANUAL.md' > "$B/BASE/MANUAL.txt"
printf '%s\n%s\n' "$RELEASE_NAME" "$HASH" > "$S/../version.txt"
./commits.sh > "$B/SYSTEM/commits.txt" 2>/dev/null || true
find "$B" -type f -name '.DS_Store' -delete

# ---- package -------------------------------------------------------------------------------
mv "$B/SYSTEM" "$B/PAYLOAD/.system"
(cd "$B/PAYLOAD" && zip -qr MinUI.zip .system)
mv "$B/PAYLOAD/MinUI.zip" "$B/BASE/"
rm -rf "$B/FULL"; mkdir "$B/FULL"
cp -fR "$B/BASE/"* "$B/FULL/"
cp -fR "$B/EXTRAS/"* "$B/FULL/"
mkdir -p "$REPO/releases"
OUT="$REPO/releases/$RELEASE_NAME-$PLATFORM.zip"
rm -f "$OUT"
(cd "$B/FULL" && zip -qr "$OUT" Bios Emus Roms Cheats Tools m21 MinUI.zip MANUAL.txt)
ls -la "$OUT"
echo "$RELEASE_NAME" > "$B/latest.txt"
