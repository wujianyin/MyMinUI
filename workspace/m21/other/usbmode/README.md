# USB Mode.pak (m21 / Allwinner H133, Tina 5.4.61 vendor kernel)

Items: USB Storage (OFF/RW/RO), ADB, USB Serial (CDC-ACM), USB Network (OFF / +Internet / local) with a
Network Type selector (ECM = Mac/Linux, NCM = Mac/Linux/Win11, RNDIS = Windows 10/11; RNDIS is placed first in the config and uses MS OS compatible-ID descriptors), USB Host (OTG) sub page,
Boot Defaults sub page (checkboxes: ADB / Storage / Serial / Network + type); USB Network sub page (mode, type, per-OS "How to connect" help).

Serial and Network are mutually exclusive: the UDC has a single interrupt endpoint. When either is on,
the device class is set to EF/02/01 (IAD) so Windows binds its inbox drivers (usbser for ACM, usb8023 for RNDIS).

See `FEATURES_AND_ISSUES.md` for a summary of all features, problems/root causes and open items.

## Files
- `usbmode.c`   UI (links against MinUI api/platform/utils, see `crt_glibc.c` for the glibc-2.26 start stub)
- `umsd.c`      FunctionFS mass-storage daemon
- `dhcpd_mini.c` single-lease DHCP server for `usb0` (192.168.7.1 -> 192.168.7.2)
- `usbnet.sh`   `on [ecm|ncm|rndis]|off|up|serial on|off` helper called by usbmode (insmod, function, ifconfig, DHCP)
- `tomato.m21`  boot hook; reads `.userdata/m21/usb_boot_mode.cfg`
  (line 1: `adb_only|adb_ums_rw|adb_ums_ro|off`, optional line 2: `net`); network type in `.userdata/m21/usb_nettype`
- `kmod/`       out-of-tree `u_ether_x.ko` / `f_ecm_x.ko` / `f_ncm_x.ko` / `f_rndis_x.ko` / `f_acm_x.ko` (mainline 5.4.61;
  changes: `can_support_ecm()` bypassed in ECM/NCM because sunxi UDC sets `quirk_altset_not_supp` (`f_ncm.patch`),
  `u_ether.c` DMA alignment fixes)

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
| `struct net_device`         | `linux/netdevice.h`  | `char sunxi_tail_pad[256]` at the end (`netdevice_h_pad.patch`): vendor `netdev_priv()` starts at 1600, mainline at 1344. Without it `struct eth_dev` overlays vendor net_device fields; it only worked for ECM because `wrap`/`unwrap` are NULL there. NCM/RNDIS set them and oops in `if_nlmsg_size` (`dev->rtnl_link_ops`). |

(`*_pad.*` files in `kmod/` hold the exact edits.) Wrong layouts show up as NULL derefs in IRQ context.

Build: `make KCFLAGS="-march=armv7-a" M=<dir with kmod/*> modules`, then `strip --strip-debug`.

## Notes
- The UDC must be unbound (`echo "" > UDC`) before adding/removing function links, otherwise the
  new function is never bound (no `usb0`).
- macOS/Linux hosts: the host gets 192.168.7.2 via DHCP; the device is 192.168.7.1.

## Internet via the host
`usbnet.sh up` (and `tomato` at boot) set `default via 192.168.7.2` and, if missing, DNS in `/tmp/resolv.conf.auto`.
The host must NAT: `host-tools/share-internet-macos.sh on` (sudo, pf anchor `com.apple/250.usbshare`) or
`share-internet-linux.sh on`, or on Windows `share-internet-windows.bat` (admin; WinNAT via `New-NetNat`, not ICS which forces 192.168.137.1 - untested on real hardware). Then on the device: `ping -c3 8.8.8.8`, `ping -c3 www.baidu.com`,
`printf 'GET / HTTP/1.0\r\nHost: www.baidu.com\r\n\r\n' | nc www.baidu.com 80 | head -5` (adb shell; the stock curl has no protocols).

`kmod/f_rndis.patch`: Call Management `bDataInterface` = real data interface (mainline hardcodes 1). `kmod/f_ncm.patch`: `can_support_ecm` bypass.
