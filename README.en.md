# MyMinUI-M22Pro (English summary)

Fork of [Turro75/MyMinUI](https://github.com/Turro75/MyMinUI) (itself a fork of [shauninman/MinUI](https://github.com/shauninman/MinUI)).
The upstream README is kept as [README.upstream.md](README.upstream.md). The main (Chinese) README of this fork is [README.md](README.md).

## Focus
Future work targets the **SJGAM M22Pro** handheld (Allwinner H133 / `sun8iw20`, 256 MB RAM; `m21` platform upstream):
a hackable device (USB networking/storage/debug), documented low-level internals, and staying close to upstream.

## Device data
- Stock firmware dump (all SPI NOR partitions) and analysis: [firmware/sjgam-m22pro/](firmware/sjgam-m22pro/) — see [ANALYSIS.md](firmware/sjgam-m22pro/ANALYSIS.md) (Chinese).
- Short version: Tina Linux (musl), Linux 5.4.61, 16 MiB SPI NOR with a full read-only squashfs rootfs. The stock `guimenu` runs `/mnt/SDCARD/tomato`
  when `/usr/tomato` exists, so MinUI boots from the SD card only and the device cannot be bricked by it.

## What this fork adds
**USB Mode.pak** (`Tools/m21/USB Mode.pak`): switch USB functions at runtime — USB storage (RW/RO), ADB, USB serial,
**USB network (CDC-ECM, 192.168.7.1 with a built-in DHCP server handing out 192.168.7.2)**, OTG role, and a boot-time default (storage mode + network).

| Feature | Status |
|---|---|
| Storage / ADB / Serial / OTG role | tested |
| USB Network (ECM) | tested on macOS (boot + switching flow); menu UI not yet verified on the device screen |
| Boot defaults | `adb_only`, `adb_ums_ro`, and network combinations tested after reboot; `adb_ums_rw` and `off` not individually tested |

The vendor kernel has no ECM/NCM/RNDIS, so out-of-tree modules (`u_ether_x.ko`, `f_ecm_x.ko`) are built against a mainline 5.4.61 tree patched
with struct padding that matches the vendor layouts. The boot hook `tomato` was also fixed (unbind UDC before linking functions, bind-mount the
FunctionFS mount into the chroot, ECM fallback). Full work log, pitfalls and remaining tasks: [workspace/m21/other/usbmode/NOTES.md](workspace/m21/other/usbmode/NOTES.md) (Chinese).

## Known issues / TODO
- Menu items not yet exercised on the device; `adb_ums_rw` / `off` boot modes untested.
- Only macOS verified. Windows (RNDIS/NCM) modules compile but are untested and not shipped.
- Few USB endpoints: ADB + storage + network use all of them; adding USB serial will probably not fit.
- Network is point-to-point only (single DHCP lease, no gateway/DNS/NAT).
- The real source of `tomato` was not found in the repo (`build/FULL/m21/tomato` is git-ignored); `tomato.m21` is a copy of the on-device file.
- The pak is not wired into the workspace makefile; prebuilt `.elf`/`.ko` are committed (build steps are in NOTES.md).
- Exposing the SD card read-write while the device uses it can corrupt it; a hard reset can leave it read-only (repair steps in NOTES.md).

## Disclaimer
Firmware images under `firmware/` are unmodified dumps; copyright belongs to Allwinner and the device vendor. They are provided for research, backup
and compatibility work only. Do **not** flash the SPI NOR. Not affiliated with SJGAM, Allwinner or the MinUI authors. Use at your own risk.
