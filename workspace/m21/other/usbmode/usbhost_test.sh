#!/bin/sh
# USB OTG host-mode test (run on the device, detached so it survives the adb link dropping).
#
#   adb push usbhost_test.sh /mnt/SDCARD/usbhost_test.sh
#   adb shell 'chmod +x /mnt/SDCARD/usbhost_test.sh; (trap "" HUP; /mnt/SDCARD/usbhost_test.sh 150 >/dev/null 2>&1 &)'
#
# Timeline: 8 s after start the Type-C port is switched to HOST mode (adb disappears).
# Unplug the PC cable, plug the powered OTG hub + USB stick, wait.  After <seconds> (default 120)
# the script switches the port back to device mode and re-binds the gadget so adb returns.
# Result: /mnt/SDCARD/usbhost.log   (the stick is only ever mounted read-only)
T=${1:-120}
L=/mnt/SDCARD/usbhost.log
U=/sys/devices/platform/soc@3000000/soc@3000000:usbc0@0
G=/sys/kernel/config/usb_gadget/g1
exec >"$L" 2>&1

echo "== usbhost_test start, duration ${T}s"
dmesg > /tmp/usbhost_dmesg0.txt
sleep 8

UDC_NAME=$(ls /sys/class/udc | head -1)
echo "== switching to HOST (udc=$UDC_NAME)"
echo "" > $G/UDC 2>/dev/null
cat $U/usb_null >/dev/null 2>&1
sleep 1
cat $U/usb_host >/dev/null 2>&1
echo "otg_role: $(cat $U/otg_role)"

mounted=""
i=0
while [ $i -lt $T ]; do
	sleep 2
	i=$((i+2))
	devs=$(ls /sys/bus/usb/devices/ 2>/dev/null | grep -E '^[0-9]+-[0-9.]+$' | tr '\n' ' ')
	sd=$(ls /dev 2>/dev/null | grep -E '^sd[a-z][0-9]*$' | tr '\n' ' ')
	echo "[t=${i}s] usb devices: ${devs:-none} | block: ${sd:-none}"
	if [ -n "$sd" ] && [ -z "$mounted" ]; then
		sleep 5
		part=$(ls /dev | grep -E '^sd[a-z][0-9]+$' | head -1)
		[ -z "$part" ] && part=$(ls /dev | grep -E '^sd[a-z]$' | head -1)
		disk=$(echo $part | sed 's/[0-9]*$//')
		echo "== stick found: disk=/dev/$disk part=/dev/$part"
		echo "-- blkid:"; blkid /dev/$disk /dev/$part 2>&1
		echo "-- first sector of /dev/$disk (partition table):"; dd if=/dev/$disk bs=512 count=1 2>/dev/null | hexdump -C | head -40
		echo "-- first 96 bytes of /dev/$part (filesystem signature):"; dd if=/dev/$part bs=512 count=1 2>/dev/null | hexdump -C | head -8
		echo "-- /proc/filesystems:"; cat /proc/filesystems | tr '\n' ' '; echo
		echo "-- mounts mentioning sd/exUDISK (stock hotplug automounts the stick):"; grep -E "sda|exUDISK" /proc/mounts
		if grep -q "^/dev/$part " /proc/mounts; then
			echo "== already mounted by the system hotplug:"
			df /mnt/exUDISK 2>&1
			ls -la /mnt/exUDISK 2>&1 | head -20
			mounted=2
		fi
		mkdir -p /tmp/usbdisk
		for fs in auto vfat exfat ntfs; do
			[ "$mounted" = 2 ] && break
			if [ $fs = auto ]; then opt=""; else opt="-t $fs"; fi
			out=$(mount $opt -o ro /dev/$part /tmp/usbdisk 2>&1) && { echo "== mounted OK ($fs)"; mounted=1; break; }
			echo "-- mount $fs failed: $out"
		done
		echo "-- kernel messages:"; dmesg | tail -40
		if [ "$mounted" = 2 ]; then
			:
		elif [ "$mounted" = 1 ]; then
			df /tmp/usbdisk
			ls -la /tmp/usbdisk | head -20
			umount /tmp/usbdisk
		else
			echo "== mount failed for all filesystems"
			mounted=2
		fi
	fi
done

echo "== USB devices detail"
for d in /sys/bus/usb/devices/*; do
	[ -f "$d/idVendor" ] && echo "$(basename $d): $(cat $d/idVendor):$(cat $d/idProduct) $(cat $d/manufacturer 2>/dev/null) $(cat $d/product 2>/dev/null) speed=$(cat $d/speed 2>/dev/null)"
done
echo "== new dmesg lines"
dmesg > /tmp/usbhost_dmesg1.txt
diff /tmp/usbhost_dmesg0.txt /tmp/usbhost_dmesg1.txt | grep '^>' | head -80

echo "== syncing/unmounting the stick (stock automount is rw,async)"
sync
umount /mnt/exUDISK 2>&1
echo "== restoring device mode"
cat $U/usb_device >/dev/null 2>&1
sleep 2
echo "$UDC_NAME" > $G/UDC 2>/dev/null
echo "otg_role: $(cat $U/otg_role)  UDC: $(cat $G/UDC)"
echo "== done"
sync
