#!/bin/sh

export PLATFORM="m21"
export SDCARD_PATH="/mnt/SDCARD"
export SYSTEM_PATH="/mnt/SDCARD/.system/m21"
export USERDATA_PATH="/mnt/SDCARD/.userdata/m21"
export SHARED_USERDATA_PATH="/mnt/SDCARD/.userdata/shared"
export CPU_SPEED_MENU=${CPU_SPEED_MENU:-912000}
export CPU_SPEED_POWERSAVE=${CPU_SPEED_POWERSAVE:-912000}
export CPU_SPEED_GAME=${CPU_SPEED_GAME:-1008000}
export CPU_SPEED_PERF=${CPU_SPEED_PERF:-1104000}
export CPU_SPEED_MAX=${CPU_SPEED_MAX:-1200000}
export CPU_SPEED_SLEEP=${CPU_SPEED_SLEEP:-480000}
export PATH="/mnt/SDCARD/.system/m21/bin:/bin:/sbin:/usr/bin:/usr/sbin:$PATH"
export LD_LIBRARY_PATH="/mnt/SDCARD/.system/m21/lib:/usr/lib:/lib:$LD_LIBRARY_PATH"

PAK_DIR="$(cd "$(dirname "$0")" && pwd)"
export USBMODE_PAK_DIR="$PAK_DIR"
cp -f "$PAK_DIR/umsd.elf" /tmp/umsd.elf
cp -f "$PAK_DIR/usbmode.elf" /tmp/usbmode.elf
chmod +x /tmp/umsd.elf /tmp/usbmode.elf
cd /tmp

/tmp/usbmode.elf >/tmp/usbmode.log 2>&1
sync
echo 3 > /proc/sys/vm/drop_caches 2>/dev/null
