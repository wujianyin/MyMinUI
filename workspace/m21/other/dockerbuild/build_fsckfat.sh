#!/bin/sh
# Host side. Cross-builds a static ARM fsck.fat (dosfstools 4.2) with the kbuild-arm2 image (see run.sh).
# The stock firmware has no fsck.vfat; this is what repairs a card that the kernel remounted read-only after a FAT error.
#   OUT=/tmp/fsckb sh build_fsckfat.sh        ->  $OUT/fsck.fat (about 470 KB)
# Use it (card mounted read-only is fine, run a dry run first):
#   adb push $OUT/fsck.fat /tmp/fsck.fat && adb shell chmod +x /tmp/fsck.fat
#   adb shell '/tmp/fsck.fat -n -v /dev/mmcblk0p1'    # report only
#   adb shell '/tmp/fsck.fat -a -v /dev/mmcblk0p1'    # repair, then: adb shell 'sync; reboot'
set -e
OUT=${OUT:-/tmp/fsckb}
mkdir -p "$OUT"
docker run --rm -v "$OUT":/o kbuild-arm2 sh -c '
set -e
apt-get update >/dev/null 2>&1
apt-get install -y --no-install-recommends wget ca-certificates make >/dev/null 2>&1
cd /tmp
wget -q https://github.com/dosfstools/dosfstools/releases/download/v4.2/dosfstools-4.2.tar.gz
tar xzf dosfstools-4.2.tar.gz && cd dosfstools-4.2
./configure --host=arm-linux-gnueabihf CC=arm-linux-gnueabihf-gcc CFLAGS="-O2" LDFLAGS="-static" --without-udev --without-iconv >/tmp/conf.log 2>&1 || { tail -5 /tmp/conf.log; exit 1; }
make -j4 >/tmp/make.log 2>&1 || { tail -8 /tmp/make.log; exit 1; }
arm-linux-gnueabihf-strip src/fsck.fat
cp src/fsck.fat /o/fsck.fat
'
ls -la "$OUT/fsck.fat"
