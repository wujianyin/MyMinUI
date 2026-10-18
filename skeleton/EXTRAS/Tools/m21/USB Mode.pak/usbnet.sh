#!/bin/sh
# USB Network (CDC-ECM) control. usage: usbnet.sh on|off   (run with gadget UDC unbound by caller)
G=/sys/kernel/config/usb_gadget/g1
DIR="$(cd "$(dirname "$0")" && pwd)"
case "$1" in
on)
	lsmod | grep -q '^u_ether_x' || insmod "$DIR/modules/u_ether_x.ko" || exit 1
	lsmod | grep -q '^f_ecm_x'   || insmod "$DIR/modules/f_ecm_x.ko"   || exit 1
	[ -d $G/functions/ecm.usb0 ] || mkdir $G/functions/ecm.usb0 || exit 1
	ln -sf $G/functions/ecm.usb0 $G/configs/c.1/ecm.usb0
	;;
off)
	rm -f $G/configs/c.1/ecm.usb0
	killall dhcpd_mini.elf 2>/dev/null
	;;
up)  # after the UDC is bound: bring up the interface and DHCP server
	i=0
	while [ $i -lt 20 ] && ! ifconfig usb0 >/dev/null 2>&1; do sleep 0.2; i=$((i+1)); done
	ifconfig usb0 192.168.7.1 netmask 255.255.255.0 up
	killall dhcpd_mini.elf 2>/dev/null
	cp -f "$DIR/dhcpd_mini.elf" /tmp/dhcpd_mini.elf && chmod +x /tmp/dhcpd_mini.elf
	/tmp/dhcpd_mini.elf usb0 192.168.7.1 192.168.7.2 >/tmp/dhcpd.log 2>&1 &
	;;
esac
exit 0
