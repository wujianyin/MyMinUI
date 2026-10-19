#!/bin/sh
# Device-side network diagnostics for USB network sharing (run on the device, e.g. through usb-diag-windows.bat).
# Every step that can hang is killed after 6 s.
t() { "$@" & p=$!; i=0; while kill -0 $p 2>/dev/null && [ $i -lt 6 ]; do sleep 1; i=$((i+1)); done
      if kill -0 $p 2>/dev/null; then echo "(timeout after 6 s)"; kill $p 2>/dev/null; fi; wait $p 2>/dev/null; }
echo "== gadget"; ls /sys/kernel/config/usb_gadget/g1/configs/c.1 2>&1 | tr "\n" " "; echo
echo "== time";        date
IF=$(ip route | sed -n "s/^default.* dev \([^ ]*\).*/\1/p" | head -n 1); [ -n "$IF" ] || IF=usb0
echo "== net interface: $IF"; ifconfig $IF 2>&1 | head -8
echo "== route";       ip route
echo "== resolv.conf"; cat /etc/resolv.conf
echo "== ping gateway 192.168.7.2";       t ping -c 2 -W 2 192.168.7.2
echo "== ping 8.8.8.8";                   t ping -c 2 -W 2 8.8.8.8
echo "== ping 223.5.5.5";                 t ping -c 2 -W 2 223.5.5.5
echo "== ping 119.29.29.29";              t ping -c 2 -W 2 119.29.29.29
echo "== ping 114.114.114.114";           t ping -c 2 -W 2 114.114.114.114
echo "== ping -s 1400 8.8.8.8 (big packets)"; t ping -c 2 -s 1400 -W 2 8.8.8.8
echo "== nslookup www.baidu.com @223.5.5.5";     t nslookup www.baidu.com 223.5.5.5
echo "== nslookup www.baidu.com @8.8.8.8";       t nslookup www.baidu.com 8.8.8.8
echo "== nslookup www.baidu.com @119.29.29.29";  t nslookup www.baidu.com 119.29.29.29
echo "== nslookup www.baidu.com @114.114.114.114"; t nslookup www.baidu.com 114.114.114.114
echo "== nslookup www.baidu.com @192.168.7.2 (the PC itself)"; t nslookup www.baidu.com 192.168.7.2
echo "== ping www.baidu.com (system resolver)"; t ping -c 2 -W 2 www.baidu.com
echo "== $IF counters"; ifconfig $IF 2>&1 | grep -E "RX|TX"
echo "== done"
