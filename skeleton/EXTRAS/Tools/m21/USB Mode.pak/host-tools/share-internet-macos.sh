#!/bin/sh
# Share this Mac's Internet connection with the handheld over USB (NAT).
#   sudo ./share-internet-macos.sh on  [egress_interface]
#   sudo ./share-internet-macos.sh off
#   ./share-internet-macos.sh status
# The device is 192.168.7.1 and already points its default route/DNS at 192.168.7.2 (this Mac).
# Uses a pf anchor under com.apple/* so the system pf ruleset is not replaced.
SUBNET=192.168.7.0/24
ANCHOR="com.apple/250.usbshare"
TOKEN_FILE=/tmp/usbshare.pftoken
run() { if [ -n "$DRYRUN" ]; then echo "+ $*"; else "$@"; fi; }

case "$1" in
on)
	[ -n "$DRYRUN" ] || [ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }
	IF=${2:-$(route -n get 8.8.8.8 2>/dev/null | awk '/interface:/{print $2}')}
	[ -n "$IF" ] || { echo "no Internet route found; pass the egress interface (e.g. en0)"; exit 1; }
	echo "egress interface: $IF"
	case "$IF" in utun*) echo "note: egress is a VPN tunnel ($IF); if it does not work, disconnect the VPN or pass the Wi-Fi interface";; esac
	run sysctl -w net.inet.ip.forwarding=1
	if [ -n "$DRYRUN" ]; then echo "+ echo 'nat on $IF from $SUBNET to any -> ($IF)' | pfctl -a $ANCHOR -f -"
	else echo "nat on $IF from $SUBNET to any -> ($IF)" | pfctl -a "$ANCHOR" -f - 2>&1 | grep -v "^No ALTQ" ; fi
	if [ -z "$DRYRUN" ]; then pfctl -E 2>&1 | sed -n 's/^Token *: *//p' > "$TOKEN_FILE"; fi
	echo "on. On the device try:  ping -c3 8.8.8.8 ; ping -c3 www.baidu.com"
	;;
off)
	[ -n "$DRYRUN" ] || [ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }
	run pfctl -a "$ANCHOR" -F all
	[ -s "$TOKEN_FILE" ] && run pfctl -X "$(cat $TOKEN_FILE)" && rm -f "$TOKEN_FILE"
	run sysctl -w net.inet.ip.forwarding=0
	echo off
	;;
status)
	sysctl net.inet.ip.forwarding
	pfctl -a "$ANCHOR" -s nat 2>/dev/null
	;;
*)
	echo "usage: sudo $0 on [egress_if] | off | status"; exit 1;;
esac
