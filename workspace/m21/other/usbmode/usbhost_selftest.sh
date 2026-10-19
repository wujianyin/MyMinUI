#!/bin/sh
# Self test of the USB Host logic of USB Mode.pak (no UI).  Run detached on the device:
#   adb shell '(trap "" HUP; /mnt/SDCARD/Tools/m21/USB\ Mode.pak/usbhost_selftest.sh 90 >/dev/null 2>&1 &)'
# It switches the Type-C port to host (adb drops), lists USB devices + auto-mounts storage every 2 s,
# then ejects, restores device mode (adb returns) and leaves the report in /mnt/SDCARD/usbhost_selftest.log
SECS=${1:-60}
PAK="/mnt/SDCARD/Tools/m21/USB Mode.pak"
cp -f "$PAK/usbmode.elf" /tmp/usbmode_t.elf; chmod +x /tmp/usbmode_t.elf
chroot /overlay /bin/sh -c "cd /tmp; /tmp/usbmode_t.elf --host-test $SECS" >/mnt/SDCARD/usbhost_selftest.log 2>&1
sync
