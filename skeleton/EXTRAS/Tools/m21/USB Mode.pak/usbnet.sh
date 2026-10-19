#!/bin/sh
# USB gadget helper.  Run with the gadget UDC unbound by the caller (except "up").
#   usbnet.sh on  [ecm|ncm|rndis]   enable USB network function (default ecm)
#   usbnet.sh off                    remove every network function from the config
#   usbnet.sh up                     after the UDC is bound: configure usb0 + DHCP server
#   usbnet.sh serial on|off          CDC-ACM serial (ttyGS0); Windows/macOS/Linux all have a driver
# ecm = Mac/Linux, ncm = Mac/Linux/Windows 11, rndis = Windows (all versions)/Linux.
G=/sys/kernel/config/usb_gadget/g1
DIR="$(cd "$(dirname "$0")" && pwd)"
load_ether() { lsmod | grep -q '^u_ether_x' || insmod "$DIR/modules/u_ether_x.ko" || exit 1; }
case "$1" in
on)
	T="$2"; case "$T" in ecm|ncm|rndis) ;; *) T=ecm ;; esac
	load_ether
	lsmod | grep -q "^f_${T}_x" || insmod "$DIR/modules/f_${T}_x.ko" || exit 1
	[ -d $G/functions/$T.usb0 ] || mkdir $G/functions/$T.usb0 || exit 1
	if [ "$T" = rndis ]; then   # Windows binds its inbox RNDIS driver by class E0/01/03
		echo e0 > $G/functions/rndis.usb0/class
		echo 01 > $G/functions/rndis.usb0/subclass
		echo 03 > $G/functions/rndis.usb0/protocol
	fi
	rm -f $G/configs/c.1/ecm.usb0 $G/configs/c.1/ncm.usb0 $G/configs/c.1/rndis.usb0
	ln -sf $G/functions/$T.usb0 $G/configs/c.1/$T.usb0
	;;
off)
	rm -f $G/configs/c.1/ecm.usb0 $G/configs/c.1/ncm.usb0 $G/configs/c.1/rndis.usb0
	killall dhcpd_mini.elf 2>/dev/null
	ip route del default via 192.168.7.2 2>/dev/null
	;;
serial)
	case "$2" in
	on)
		lsmod | grep -q '^f_acm_x' || insmod "$DIR/modules/f_acm_x.ko" || exit 1
		[ -d $G/functions/acm.0 ] || mkdir $G/functions/acm.0 || exit 1
		ln -sf $G/functions/acm.0 $G/configs/c.1/acm.0
		;;
	off)
		rm -f $G/configs/c.1/acm.0 $G/configs/c.1/gser.0
		;;
	esac
	;;
up)  # after the UDC is bound: bring up the interface and DHCP server
	# the netdev name depends on how many network functions were registered since boot (usb0, usb1, ...)
	T=
	for t in ecm ncm rndis; do [ -e $G/configs/c.1/$t.usb0 ] && T=$t; done
	IF=$(cat $G/functions/$T.usb0/ifname 2>/dev/null | tr -d '\r\n ')
	[ -n "$IF" ] || IF=usb0
	i=0
	while [ $i -lt 20 ] && ! ifconfig $IF >/dev/null 2>&1; do sleep 0.2; i=$((i+1)); done
	for n in usb0 usb1 usb2; do [ "$n" != "$IF" ] && ifconfig $n 0.0.0.0 down 2>/dev/null; done   # stale ones from earlier type switches
	ifconfig $IF 192.168.7.1 netmask 255.255.255.0 up
	# Internet via the host: default route + DNS (needs the host to share its connection, see host-tools/).
	# Skipped when the user chose "local only" in the menu (flag file below).
	if [ ! -e /mnt/SDCARD/.userdata/m21/usb_noshare ]; then
		ip route replace default via 192.168.7.2 dev $IF 2>/dev/null || route add default gw 192.168.7.2 $IF 2>/dev/null
		grep -q '^nameserver' /tmp/resolv.conf.auto 2>/dev/null || \
			printf 'nameserver 223.5.5.5\nnameserver 119.29.29.29\nnameserver 8.8.8.8\n' > /tmp/resolv.conf.auto
	fi
	killall dhcpd_mini.elf 2>/dev/null
	cp -f "$DIR/dhcpd_mini.elf" /tmp/dhcpd_mini.elf && chmod +x /tmp/dhcpd_mini.elf
	/tmp/dhcpd_mini.elf $IF 192.168.7.1 192.168.7.2 >/tmp/dhcpd.log 2>&1 &
	;;
esac
exit 0
