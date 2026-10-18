#!/bin/sh
# Share this Linux host's Internet connection with the handheld over USB (NAT).
#   sudo ./share-internet-linux.sh on [egress_interface]   |   sudo ./share-internet-linux.sh off
# The device is 192.168.7.1 and already points its default route/DNS at 192.168.7.2 (this host).
SUBNET=192.168.7.0/24
case "$1" in
on)
	IF=${2:-$(ip route get 8.8.8.8 2>/dev/null | sed -n 's/.* dev \([^ ]*\).*/\1/p' | head -n1)}
	[ -n "$IF" ] || { echo "no Internet route found; pass the egress interface"; exit 1; }
	sysctl -w net.ipv4.ip_forward=1
	iptables -t nat -C POSTROUTING -s $SUBNET -o "$IF" -j MASQUERADE 2>/dev/null || \
		iptables -t nat -A POSTROUTING -s $SUBNET -o "$IF" -j MASQUERADE
	iptables -C FORWARD -s $SUBNET -j ACCEPT 2>/dev/null || iptables -I FORWARD -s $SUBNET -j ACCEPT
	iptables -C FORWARD -d $SUBNET -m state --state RELATED,ESTABLISHED -j ACCEPT 2>/dev/null || \
		iptables -I FORWARD -d $SUBNET -m state --state RELATED,ESTABLISHED -j ACCEPT
	echo "on via $IF"
	;;
off)
	for IF in $(ip -o link | awk -F': ' '{print $2}'); do iptables -t nat -D POSTROUTING -s $SUBNET -o "$IF" -j MASQUERADE 2>/dev/null; done
	iptables -D FORWARD -s $SUBNET -j ACCEPT 2>/dev/null
	iptables -D FORWARD -d $SUBNET -m state --state RELATED,ESTABLISHED -j ACCEPT 2>/dev/null
	sysctl -w net.ipv4.ip_forward=0
	echo off
	;;
*) echo "usage: sudo $0 on [egress_if] | off"; exit 1;;
esac
