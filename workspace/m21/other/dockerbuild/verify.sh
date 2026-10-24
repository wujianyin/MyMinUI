#!/bin/sh
# Host side. Loads every built binary with the REAL device glibc (2.26 from the release rootfs.ext2) under qemu-user,
# i.e. reproduces "version `GLIBC_x' not found" / missing library failures before the card is touched.
#   BIN=/tmp/mb  BASEZIP=/path/MyMinUI-20260208b-0-m21.zip  sh verify.sh
set -e
BIN=${BIN:-/tmp/mb}
BASEZIP=${BASEZIP:-/tmp/rel/MyMinUI-20260208b-0-m21.zip}
W=$(mktemp -d)
(cd "$W" && unzip -q "$BASEZIP" MinUI.zip && mkdir inner && cd inner && unzip -q ../MinUI.zip '.system/m21/rootfs.ext2')
docker run --rm -v "$W/inner/.system/m21":/r -v "$BIN":/o kbuild-arm2 sh -c '
apt-get update -qq && apt-get install -y -qq qemu-user-static >/dev/null 2>&1 || { echo "cannot install qemu-user-static"; exit 2; }
mkdir -p /rfs && debugfs -R "rdump / /rfs" /r/rootfs.ext2 >/dev/null 2>&1
bad=0
for f in /o/*.elf; do
  out=$(LD_LIBRARY_PATH=/o:/rfs/usr/lib:/rfs/lib qemu-arm-static -L /rfs /rfs/lib/ld-linux-armhf.so.3 --list $f 2>&1 | grep -E "not found|version|error|cannot" || true)
  if [ -n "$out" ]; then echo "FAIL $(basename $f): $out"; bad=1; else echo "ok   $(basename $f)"; fi
done
# libmsettings is loaded through the binaries above (LD_LIBRARY_PATH=/o); also check it on its own
out=$(LD_LIBRARY_PATH=/rfs/usr/lib:/rfs/lib qemu-arm-static -L /rfs /rfs/lib/ld-linux-armhf.so.3 --list /o/libmsettings.so 2>&1 | grep -E "not found|version|error|cannot" || true)
[ -z "$out" ] && echo "ok   libmsettings.so" || { echo "FAIL libmsettings.so: $out"; bad=1; }
exit $bad'
rc=$?
rm -rf "$W"
exit $rc
