# USB Mode.pak (m21 / Allwinner H133, Tina 5.4.61 vendor kernel)

Items: USB Storage (OFF/RW/RO), ADB, USB Serial, USB Network (CDC-ECM), OTG role,
Boot Default (+ Boot USB Network).

## Files
- `usbmode.c`   UI (links against MinUI api/platform/utils, see `crt_glibc.c` for the glibc-2.26 start stub)
- `umsd.c`      FunctionFS mass-storage daemon
- `dhcpd_mini.c` single-lease DHCP server for `usb0` (192.168.7.1 -> 192.168.7.2)
- `usbnet.sh`   `on|off|up` helper called by usbmode (insmod, ecm.usb0 function, ifconfig, DHCP)
- `tomato.m21`  boot hook; reads `.userdata/m21/usb_boot_mode.cfg`
  (line 1: `adb_only|adb_ums_rw|adb_ums_ro|off`, optional line 2: `net`)
- `kmod/`       out-of-tree `u_ether_x.ko` / `f_ecm_x.ko` (mainline 5.4.61 `u_ether.c`/`f_ecm.c`;
  only change: `can_support_ecm()` bypassed because sunxi UDC sets `quirk_altset_not_supp`)

## Building the modules
The vendor kernel is configured without ECM/NCM/RNDIS, so they are built out of tree against a
mainline 5.4.61 tree configured with the vendor `.config` (`kmod/build.sh`). The vendor struct
layouts differ from mainline, so padding fields were added to the headers of that tree:

| struct                      | header               | pad                                        |
|-----------------------------|----------------------|--------------------------------------------|
| `struct module`             | `linux/module.h`     | `char sunxi_pad[36]` before `syms`         |
| `struct usb_function_instance` | `linux/usb/composite.h` | `int sunxi_pad` after `fd`          |
| `struct sk_buff`            | `linux/skbuff.h`     | `char sunxi_pad[16]` after `headers_end[0]`|
| `struct usb_gadget`         | `linux/usb/gadget.h` | `char sunxi_pad[16]` after `udc`, `char sunxi_pad2[184]` after `dev` |

(`*_pad.*` files in `kmod/` hold the exact edits.) Wrong layouts show up as NULL derefs in IRQ context.

Build: `make KCFLAGS="-march=armv7-a" M=<dir with kmod/*> modules`, then `strip --strip-debug`.

## Notes
- The UDC must be unbound (`echo "" > UDC`) before adding/removing function links, otherwise the
  new function is never bound (no `usb0`).
- macOS/Linux hosts: the host gets 192.168.7.2 via DHCP; the device is 192.168.7.1.

## Internet via the host
`usbnet.sh up` (and `tomato` at boot) set `default via 192.168.7.2` and, if missing, DNS in `/tmp/resolv.conf.auto`.
The host must NAT: `host-tools/share-internet-macos.sh on` (sudo, pf anchor `com.apple/250.usbshare`) or
`share-internet-linux.sh on`. Then on the device: `ping -c3 8.8.8.8`, `ping -c3 www.baidu.com`,
`printf 'GET / HTTP/1.0\r\nHost: www.baidu.com\r\n\r\n' | nc www.baidu.com 80 | head -5` (adb shell; the stock curl has no protocols).
