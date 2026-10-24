# USB Mode.pak Work Log and Handoff Notes

> 中文版: [NOTES.zh-CN.md](NOTES.zh-CN.md)

Device: Allwinner H133/T113 (sun8iw20, 256MB RAM) handheld, Tina Linux 5.4.61 (armv7l), vendor musl rootfs on SPI NOR;
MyMinUI (`m21` platform) runs inside `chroot /overlay` on a 32GB FAT SD card (glibc 2.26 ext2 image, mounted with fuse2fs).
Host machine: Mac. This repo: `/Users/wujianyin/work/gitwork/MyMinUI`.

> This document records: final state → overall architecture → step-by-step how it was done → pitfalls and fixes → open items / next steps → environment reproduction and backup locations.

---------------------------------------------------------------------

## 1. Final State (as of end of this work)

`/mnt/SDCARD/Tools/m21/USB Mode.pak` is an interactive USB management tool with menu items:

| Menu item | Values | Status |
|---|---|---|
| USB Storage (SD Card) | OFF / RW / RO | Tested (early sessions) |
| ADB Debugging | ON / OFF | Tested |
| USB Serial (ttyGS0) | ON / OFF | Tested (early sessions) |
| **USB Network (submenu)** | Mode OFF / +Internet / Local; Type ECM / NCM / RNDIS; How to connect (Win / macOS / Linux) | On-screen display and submenu confirmed by user; ECM/NCM on Mac and NCM/RNDIS on Windows tested live |
| USB Port Role | Device / OTG Host | Tested (early sessions) |
| Boot Defaults (submenu) | Checkboxes: ADB / Storage (Off, RW, RO) / Serial / Network + Network Type; serial and network mutually exclusive | Config file 3 lines: `mode\nnet|-\nserial|-`; tomato boot tested with serial and net combinations |
| USB Network (submenu) | Mode (OFF / +Internet / Local), Network Type, How to connect (Windows / macOS / Linux help pages, scrollable) | Scripts live only under `Tools/m21/USB Mode.pak/host-tools/` (no longer copied to SD root); help pages document the path |
| Return to MinUI | | |

Boot-tested combinations (checked after reboot):
- `adb_only` + `net`: ADB OK, "Tina ADB" NIC on Mac, DHCP gives 192.168.7.2, mutual ping ~1ms.
- `adb_ums_ro` + `net`: ADB + read-only SD (SAMSUNGDISK on Mac) + USB network all work at once.
- Device boot default has been restored to `adb_only` (no network).

**Not tested**: `off` boot mode (turns off USB, adb gone too—pull SD to fix config); `adb_ums_rw` reboot-tested OK (ffs.adb+ffs.ums, umsd running, SD visible on Mac); manual network toggle from menu (logic simulated in chroot with `t_net.sh` off/on/up flow—works);
Windows host (RNDIS/NCM); Linux host (should work in theory, not tried).

---------------------------------------------------------------------

## 2. File Inventory and Locations

### In repo (uncommitted, `git status` shows untracked)
- `skeleton/EXTRAS/Tools/m21/USB Mode.pak/`
  - `launch.sh` (exports `USBMODE_PAK_DIR`, copies umsd/usbmode to /tmp to run)
  - `usbmode.elf`, `umsd.elf`, `dhcpd_mini.elf`, `usbnet.sh`
  - `modules/u_ether_x.ko`, `modules/f_ecm_x.ko`
- `workspace/m21/other/usbmode/`
  - `usbmode.c` (UI), `umsd.c` (FunctionFS storage daemon), `dhcpd_mini.c`, `usbnet.sh`, `crt_glibc.c`
  - `tomato.m21` (current on-device `/mnt/SDCARD/tomato`)
  - `kmod/`: `f_ecm.c`, `u_ether.c`, `Kbuild`, `build.sh`, `Dockerfile`, various `*_pad.*` (header padding patches)
  - `README.md` (short overview), this file

Note: `build/FULL/m21/tomato` (build artifact) is `.gitignore`d; **the real tomato source was not found in the repo**—
USB-related changes in `tomato.m21` must be merged into the actual source (see §6).

### On device (SD card)
- `/mnt/SDCARD/tomato` (boot hook, updated; old backup: Desktop `USB Mode.pak/tomato.before_net`)
- `/mnt/SDCARD/Tools/m21/USB Mode.pak/*` (same as skeleton)
- `/mnt/SDCARD/.userdata/m21/usb_boot_mode.cfg`
  - Line 1: `adb_only | adb_ums_rw | adb_ums_ro | ums_rw | ums_ro | off` (`ums_*` = no ADB at boot)
  - Line 2: `net` or `-` (enable USB network at boot, type in `usb_nettype`)
  - Line 3: `serial` or `-` (enable CDC-ACM serial at boot, mutually exclusive with net)
  - Old 1–2 line format still compatible; written by USB Mode → Boot Defaults submenu
- `/mnt/SDCARD/usb_adb_debug.log` (boot USB init log—first place to debug; rewritten each boot, do not commit / no need to clean)

### Backups (Desktop—`/tmp` gets wiped!)
`/Users/wujianyin/Desktop/mtd_dump/USB Mode.pak/`
- All source and binary copies, `tomato.before_net`, `tomato.with_net`
- `kmod_src/` (older kernel module sources and header patches)
- `build_env/`: cross-build backup (`m21_glibc` (device glibc 2.26 libs), `sdl2_inc`,
  `api.o/platform.o/utils.o`, `crt_glibc.c`, `device.config` (vendor .config), two Dockerfiles, debug tools/scripts under `tools/`)
- Also `/Users/wujianyin/Desktop/mtd_dump/unpacked/`: factory flash unpack (`vmlinux.bin`, `kallsyms.txt`, `kernel_config.txt`, `mtd4_rootfs`)

---------------------------------------------------------------------

## 3. Architecture Essentials (read first)

1. **Dual rootfs**: Vendor musl root runs `/bin/adbd -D`; `tomato` uses fuse2fs to mount `rootfs.ext2` on `/overlay`,
   bind-mounts dev/proc/sys/configfs/SDCARD/tmp, then `chroot /overlay` for MinUI.
   - Things **mounted later in the host namespace** (e.g. functionfs at `/dev/usb-ffs/ums`) are invisible inside chroot—
     must `mount -o bind /dev/usb-ffs/ums /overlay/dev/usb-ffs/ums`.
2. **USB gadget via configfs**: `/sys/kernel/config/usb_gadget/g1`, UDC name `4100000.udc-controller`.
   Vendor kernel only has built-in `gser`, `ffs`; no ECM/NCM/RNDIS—out-of-tree modules required.
3. **UDC must be unbound before adding/removing function symlinks** (`echo "" > UDC`), or new functions won't bind
   (symptom: no `usb0` / `(unnamed net_device)`).
4. **ECM netdev (`usb0`) registers only after UDC bind**, so IP and DHCP must run after bind (`usbnet.sh up`).
5. **ADB adbd runs on host side** (`/bin/adbd -D`); `tomato` has a keepalive loop.

---------------------------------------------------------------------

## 4. Implementation Steps and Problem Solving (this and prior sessions)

### 4.1 Why USB network can't use FunctionFS
Initially tried user-space FunctionFS `netd.elf` for ECM. Descriptor write failed with `Invalid argument`:
vendor `f_fs.c` only accepts HID/CCID (0x21) class-specific descriptors, rejects CDC 0x24.
Conclusion: FFS can't do ECM; must use kernel `f_ecm` → **cross-compile out-of-tree modules**.
(`netd.c/.elf` abandoned, only in Desktop backup.)

### 4.2 Out-of-tree kernel module build environment
- Download mainline 5.4.61 (`https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.4.61.tar.xz`), vendor `.config`,
  enable `USB_CONFIGFS_ECM/NCM/RNDIS=m`, `make modules_prepare` (see `kmod/build.sh`).
- Docker image `kbuild-arm` (debian bookworm + gcc-arm-linux-gnueabihf, see `kmod/Dockerfile`), workdir mounted as `/work`.
- Build: `make KCFLAGS="-march=armv7-a" M=/work/<dir> modules`, then `arm-linux-gnueabihf-strip --strip-debug`.
- Module names suffixed `_x` (`u_ether_x.ko`, `f_ecm_x.ko`).

### 4.3 Struct layout mismatch with vendor kernel (core difficulty)
Several vendor-kernel structs have extra fields vs mainline; OOT modules using mainline layout read wrong offsets. Method:
Disassemble `vmlinux.bin` (raw image, base `0xc0008000`):
`arm-linux-gnueabihf-objdump -D -b binary -m arm --adjust-vma=0xc0008000 --start-address=... --stop-address=...`,
compare with probe modules (`tools/offs2.c`, `skbo.c`, `gp.c`—print via `offsetof`, read from `.rodata` with `objdump -s -j .rodata`),
then pad 5.4.61 headers:

| Struct | Header | Padding | Discovery / symptoms |
|---|---|---|---|
| `struct module` | `linux/module.h` | `char sunxi_pad[36]` before `syms` | insmod fail / weird behavior |
| `struct usb_function_instance` | `linux/usb/composite.h` | `int sunxi_pad` after `fd` | mkdir function fails |
| `struct sk_buff` | `linux/skbuff.h` | `char sunxi_pad[16]` after `headers_end[0]` | Vendor offsets tail=168 end=172 head=176 data=180 len=84 data_len=88 mac_header=150; symptoms: UDC prints `_req is invalid`, `buf=NULL`, RX/TX spam |
| `struct usb_gadget` | `linux/usb/gadget.h` | `char sunxi_pad[16]` after `udc`; `char sunxi_pad2[184]` after `dev` | **Last fix this round**: `gadget->ep0` at offset 40 on vendor (24 mainline) → read NULL → `usb_ep_queue(NULL)` crash in IRQ; `mA`/flags at 716/725 (+184) on vendor |

Confirmed matching mainline: `net_device`, `usb_function`, `usb_configuration`, `usb_ep`, `usb_request`.
*Before any new module using other kernel structs, verify offsets the same way.*

### 4.4 `can_support_ecm()` failure
Vendor UDC (`sunxi_udc`) sets `quirk_altset_not_supp=1`; `ecm_bind` returns -EINVAL.
In `f_ecm.c` changed `if (!can_support_ecm(...))` to `if (0 && ...)` to bypass (ECM works in practice).

### 4.5 "Device reboots as soon as ECM is enabled"—how it was traced
Symptom: with ECM interrupt (INT) notify endpoint in descriptors, Mac enumerates and device resets (no crash text).
- `panic=0`, IRQ-context oops hangs until watchdog reset; DRAM doesn't survive reboot (tested), no crash in `dmesg`.
- **Fix: use RTC GP registers as a black box.** RTC GP regs (phys `0x07090100 + 4*n`) gp0–5, gp7
  survive `reboot -f`/watchdog (gp6 cleared). Wrote `crashlog.ko` (`tools/crashlog.c`):
  `register_die_notifier` + panic notifier; on crash writes `0xdead0000|val` (in_interrupt flag),
  `regs->ARM_pc`, `ARM_lr`, `ARM_sp`, `thread.address`, `err`, `r0` to gp0–gp7; after reboot `cat /proc/crashlog`.
  (Writing anything to `/proc/crashlog` clears; `pmem.c` was earlier manual read/write version.)
- One crash: PC=`c04403d0` = first insn of `usb_ep_queue` `ldrb r3,[r0,#25]`, r0=NULL;
  LR=`bf0a5788` (module addr, in `f_ecm_x` `.text.unlikely` offset 0x17c) = `ecm_setup`'s
  `usb_ep_queue(cdev->gadget->ep0,...)`. → Wrong `gadget->ep0` offset (see last row of 4.3).
- After gadget padding, **stock ECM (INT notify ep4, notify enabled) is fully stable**. Earlier suspects ("INT endpoint broken", "notify use-after-free") were wrong.
- Lesson: `crashlog.ko` helps any future "crash in IRQ, no log" issue (resolve PC from `kallsyms.txt`; module addresses via `.ko` `objdump -dr` and section sizes).

### 4.6 No RX / log spam
- When `rx_fill` fails it loops `defer_kevent` and floods dmesg ring (early logs lost).
  Debug build capped retries; **final clean build uses mainline u_ether.c without cap**—
  after sk_buff padding fix, failures don't recur.
- One test: Mac packets not received (RX=1); after reboot same modules RX/TX OK → script didn't start as expected
  (see 4.9) / stale state, not a code bug.

### 4.7 DHCP and IP
- Device `usb0=192.168.7.1/24`. Busybox in chroot has no `udhcpd`; vendor `dnsmasq` needs vendor musl and won't run in chroot,
  so wrote `dhcpd_mini.c` (minimal static DHCP: any client gets 192.168.7.2, lease 3600s, mask and server only;
  no gateway/DNS).
- On Mac if VPN (utun) steals routes, ping 169.254.x.x needs `ping -b enNN`; with DHCP, 192.168.7.x works directly.

### 4.8 Two tomato boot-hook bugs (requirement B)
1. `setusbconfig adb` already bound UDC; later `ln -sf` new functions don't apply → `echo "" > $G/UDC` before symlinks
   (both UMS and ECM paths).
2. `umsd.elf` in chroot: `open ep0: No such file or directory` → bind into chroot:
   `mount -o bind /dev/usb-ffs/ums ${ROOTFS_MOUNTPOINT}/dev/usb-ffs/ums`.
3. ECM bind fallback: if `UDC` empty, remove `ecm.usb0` link and rebind; on success wait for `usb0` → IP → `dhcpd_mini.elf` in chroot.
4. `sh -n` before pushing to SD; recommend `adb shell sh -n /mnt/SDCARD/tomato` after every tomato change.

### 4.9 Device-side scripting / debug pitfalls (avoid repeating)
- No `nohup`/`setsid`. Background scripts:
  `start-stop-daemon -b -S -m -p /tmp/x.pid -x /bin/sh -- /tmp/script.sh`.
  **Unreliable**: second `/bin/sh` daemon at same time is often no-op; stale pid file blocks start ( `rm -f /tmp/x.pid` first).
  Script should start its own logger; don't rely on adb in script (unbind UDC kills adb).
- Gadget changes drop adb; scripts need rollback (unbind → remove new links → rebind).
- **Hard reset can corrupt SD → read-only** (`fat_free_clusters: deleting FAT entry beyond EOF` → FS RO, redirects to SD fail, logs lost).
  Fix: expose SD to Mac over USB (`umsd` on `/dev/mmcblk0`, see `tools/umsrun.sh`),
  Mac `diskutil repairVolume /dev/diskN` (no sudo), reboot.
  For SD debug logs use preallocated file + `dd conv=notrunc` (`tools/dmlog.sh`).
- Device `/tmp` is tmpfs, cleared on reboot; Mac `/tmp` may be cleared too—**key build assets backed up to Desktop/build_env**.
- On Mac `sed -i` needs `sed -i ''`.
- Docker: `kbuild-arm` (kernel modules) + `kbuild-arm2` (adds `libc6-dev-armhf-cross` for userspace; see `build_env/Dockerfile.arm2`).

### 4.10 Building userspace (usbmode / umsd / dhcpd_mini)
(Earlier compile commands weren't saved; rediscovered this round—**follow this to reproduce**)
```
docker run --rm -v /tmp:/t -v <MyMinUI>/workspace:/w kbuild-arm2 bash -c '
cd /t && arm-linux-gnueabihf-gcc -O2 -marm -march=armv7-a -mfpu=neon -mfloat-abi=hard \
  -fomit-frame-pointer -fno-pie -no-pie -nostartfiles -DPLATFORM=\"m21\" -DUSE_SDL2 \
  -I/w/all/common -I/w/m21/platform -I/w/m21/libmsettings -I/t/sdl2_inc -I/t/sdl2_inc/SDL2 \
  usbmode.c api.o platform.o utils.o crt_glibc.c -o usbmode.elf \
  -L/t/m21_glibc -lSDL2 -lSDL2_image -lSDL2_ttf -lmsettings -lsamplerate -lpthread -ldl -lm -lz \
  -l:libc.so.6 -Wl,--dynamic-linker=/lib/ld-linux-armhf.so.3 -Wl,--allow-shlib-undefined
arm-linux-gnueabihf-strip usbmode.elf'
```
Notes:
- Must use `-no-pie -fno-pie -marm` (Debian defaults PIE; custom `_start` in `crt_glibc.c` needs ARM mode).
- Link against device glibc 2.26 libs (`m21_glibc/`), headers from Debian `libc6-dev-armhf-cross`; `crt_glibc.c` supplies `_start` and `__errno` shim to avoid new glibc symbols.
- `api.o/platform.o/utils.o` are prebuilt MyMinUI objects (backup in `build_env`); to rebuild, compile from
  `workspace/all/common/{api,utils}.c` and `workspace/m21/platform/platform.c` (see CFLAGS in `workspace/all/clock/makefile`).
- Alternatively repo `toolchains/m21-toolchain` (`make PLATFORM=m21 toolchain` etc.; Docker image must be built first; not used this round).
- `dhcpd_mini` same way, only needs `crt_glibc.c` and `-l:libc.so.6`.

---------------------------------------------------------------------

## 5. Deploy and Regression Checklist (after future changes)

1. Build → `adb push` to `/mnt/SDCARD/Tools/m21/USB Mode.pak/` (`chmod +x`).
2. Boot hook: write `usb_boot_mode.cfg`, `sync`, `adb reboot`, wait ~60s:
   - `ls /sys/kernel/config/usb_gadget/g1/configs/c.1` (expect `ffs.adb`, optional `ffs.ums`, `ecm.usb0`)
   - `cat /sys/kernel/config/usb_gadget/g1/UDC` (should be `4100000.udc-controller`)
   - `ifconfig usb0`, `ps | grep -E "umsd|dhcpd_mini"`, `cat /tmp/umsd.log`, `/mnt/SDCARD/usb_adb_debug.log`
   - Mac: `networksetup -listallhardwareports | grep -A1 Tina`, `ifconfig enNN` (192.168.7.2), `ping 192.168.7.1`, `ls /Volumes`
3. Simulate menu toggles (no screen): see `tools/ecmtest.sh`; or `chroot /overlay sh "<pak>/usbnet.sh" off|on|up` with UDC unbind/rebind.
4. Before changing kernel modules, probe struct offsets (4.3); if crash with no log, use `crashlog.ko` (4.5).

---------------------------------------------------------------------

## 6. Open Items / Next Steps (by priority)

> **Status update (2026-10-20 evening)**: Items 1–10 below are an early list; some done:
> 1 committed (`fork/main`); 2 menu display/submenus confirmed on screen; 4 serial–network mutex done; 5 Windows (NCM/RNDIS) done and tested (§7);
> 6 Internet sharing done (Mac/Linux/Windows scripts, see §7.2); 8 SD-root debug files and script copies cleaned.
> User test summary: Storage (Win+Mac) ✅; Serial (Windows COM, shell) ✅; NCM/ECM (Mac) incl. ping www.baidu.com ✅; Windows RNDIS/NCM recognized but ping www.baidu.com problematic; USB Host with powered hub FAT32/EXT4/NTFS mount read ✅; wget http only, curl no protocols.
> Still open: Windows sharing DNS hang, ADB flaky after toggling other switches, Mac ECM large-packet loss, boot `off` / boot without ADB combos.
> Full list in `FEATURES_AND_ISSUES.md` §4.

1. **Commit to repo**: `skeleton/.../USB Mode.pak/` and `workspace/m21/other/usbmode/` still untracked.
   Merge USB changes from `tomato.m21` into real tomato source (only ignored `build/FULL/m21/tomato` found in repo—
   find what generates it: trace `m21` target in `makefile` and `skeleton/BASE`).
2. **On-device menu test**: USB Network / Boot USB Network items, status text, combos with Storage/ADB/Serial;
   and "bind failed auto-rollback" branch (`usbmode.c` `cur_net` failure path).
3. **Boot mode retest** `off` (`adb_ums_rw` passed); combos besides `adb_ums_ro/rw` + `net` (e.g. serial + net).
4. **Endpoint budget**: UDC has only ep1/ep2/ep5 bulk pairs + ep4 interrupt + ep3 iso.
   ADB (1 pair) + UMS (1 pair) + ECM data (1 pair) + ECM notify (ep4) exactly fills; adding `gser` (1 bulk + 1 INT) likely won't fit—
   menu mutex/hints or live test needed.
5. **Windows / Linux hosts**: `f_rndis_x.ko`, `f_ncm_x.ko` build same way (`kmod/Kbuild`); not loaded on device yet, not in pak, no menu entry. RNDIS needs `rndis.c`—
   re-verify struct offsets before deploy. Linux `cdc_ether` should work in theory.
6. **Internet sharing (NAT)**: Currently L2 only; no gateway/DNS or forwarding. If needed: extend `dhcpd_mini` with router/DNS +
   device `iptables` MASQUERADE (iptables modules present).
7. **DHCP service**: Single fixed lease (192.168.7.2), ignores xid/state, no ARP check; not for multiple hosts.
8. **Cleanup**: Remove stray SD-root debug files if any (`dm.bin`, `nettest.log` deleted); `/mnt/SDCARD/usb_adb_debug.log` overwritten each boot—OK;
   repo `kmod/*_pad.txt` are full header copies (large)—could trim to diffs.
9. **Reproducible builds**: 5.4.61 tree now in `/tmp/kbuild/linux-5.4.61` (ephemeral). Rebuild:
   download → vendor `device.config` as `.config` → three `USB_CONFIGFS_*=m` → `make olddefconfig modules_prepare`
   → manual padding per 4.3 table (or `kmod/*_pad.*`) → build `kmod/`. Prefer real `.patch` files in repo.
10. **Safety**: RW SD exposed to PC while device uses it—corruption risk; menu pops storage on exit by default. If SD RO after hard reset, fix per 4.9.

---------------------------------------------------------------------

## 6.1 Internet Sharing (follow-up)
- Device **has no WiFi hardware/driver** (no `wlan0`, `/sys/class/ieee80211`, `rfkill`); only `usb0`; vendor `curl 7.54.1` built with no protocols
  (`curl --version` `Protocols:` empty), so `curl http(s)://…` always `Protocol not supported`, unrelated to connectivity;
  busybox `wget` segfaults on device. Test with `ping`, `nslookup`, `nc` (adb shell has `nc`, not in chroot).
- Implemented (device): `usbnet.sh up` and `tomato` after `usb0` up do `ip route replace default via 192.168.7.2 dev usb0`,
  and if `/tmp/resolv.conf.auto` (final target of `/etc/resolv.conf`, shared host/chroot) has no nameserver, write
  223.5.5.5 / 119.29.29.29 / 8.8.8.8. `usbnet.sh off` removes that default route.
- Host side (`skeleton/.../USB Mode.pak/host-tools/`):
  - `share-internet-macos.sh on|off|status`: `sysctl net.inet.ip.forwarding=1` + pf anchor `com.apple/250.usbshare`
    `nat on <egress> from 192.168.7.0/24 to any -> (<egress>)` (system `/etc/pf.conf` has `nat-anchor "com.apple/*"`, doesn't replace main ruleset).
    Egress from `route -n get 8.8.8.8`; with VPN it's `utunN`—disconnect VPN or set Wi‑Fi manually. Needs sudo.
  - `share-internet-linux.sh on|off`: iptables MASQUERADE + FORWARD.
- **Verified (hardware)**: After Mac `share-internet-macos.sh on`, device `ping 8.8.8.8`, `ping www.baidu.com` OK;
  `chroot /overlay /bin/busybox wget -qO- http://www.baidu.com` returns page (chroot busybox wget works; vendor segfaults);
  1MB download MD5 matches. Device `nc` too minimal (sends no data)—don't use for HTTP tests.
- Done: menu *USB Network* line is tri-state `OFF / ON (+Internet via PC) / ON (Local only)` (no extra row—screen space).
  "Local only" touches flag file `/mnt/SDCARD/.userdata/m21/usb_noshare`; `usbnet.sh up` and `tomato` skip default route/DNS when present (default = sharing).
  Tested: boot with flag → no default route; without → default route; `usbmode.elf` rebuilt but menu not screen-tested.
- Not done: Windows use built-in "Internet Connection Sharing";
  `dhcpd_mini` **intentionally** omits gateway/DNS—it serves the host, device has no WAN; giving gateway would make host use device as default egress and break host networking.
  Device gateway/DNS (toward host) set directly in `usbnet.sh up`—no dhcpd change needed.

---------------------------------------------------------------------

## 6.2 Large Frames (>512 bytes) Corruption—Fixed (`u_ether.c`)
- **Symptoms**: ping/small HTTP OK; large downloads timeout, `ping -s 600+` 100% loss; `usb0` `RX dropped`/`errors` climb;
  MTU ≤400 downloads; ~480 MD5 mismatch; ≥540 fails.
- **Trace**: debug in `rx_complete` (below)—dest MAC became `03:74:ab:40:ce:34` (should `d2:59:03:74:ab:40`),
  i.e. **payload shifted 2 bytes**, `eth_type_trans` unknown protocol → `rx_dropped`. Small packets PIO OK; large DMA only.
- **Root cause**: sunxi UDC DMA needs 4-byte-aligned buffers; `u_ether` default `skb_reserve(NET_IP_ALIGN=2)` (RX) and stack skb->data offset 2 (TX) misalign;
  DMA reads/writes from aligned boundary → 2-byte skew.
- **Fix** (`kmod/u_ether.c`): RX force `dev->no_skb_reserve = true`; TX before `usb_ep_queue` if `skb->data & 3` copy to aligned skb.
  After fix: 1MB download MD5 match (twice), `ping -s 1400` both ways, `RX/TX errors/dropped = 0`.
- **Debug**: in `rx_complete` `pr_info` len/MAC for non-0800/0806/86dd ethertypes; put module in pak `modules/`, reboot
  (`usb_boot_mode.cfg` line 2 `net` for network at boot). **Don't unbind UDC from `adb shell`** (adb on same gadget—script killed,
  device stuck unbound until reboot); runtime toggle use `(trap "" HUP; script &)` background. No `setsid`/`nohup` on device.
  Don't `rmmod f_ecm_x`/`u_ether_x` live—hard reset without log (`crashlog.ko` won't catch); replace `.ko` and reboot.

---------------------------------------------------------------------

## 6.3 OTG Host Mode / USB Drives (follow-up)
- **Type-C port doesn't output 5V (HW/DT limit)**: `usbc0` `usb_port_type=0` (device only), no `usb_id_gpio`, `usb_det_vbus_gpio`,
  no `usb_drv_vbus_gpio` in entire DT; `ehci0/ohci0` `drvvbus-supply` → `usb1-vbus` but it's `regulator-fixed` without `gpio`
  (stub). Host mode only bumps that regulator users count—no pin action, adapter LED off.
  (On-board boost not in DT not ruled out; confirm via GPIO regs or VBUS measure.)
- **Powered hub works**: `usbhost_test.sh` (this dir, copy to SD, run background, host→wait→device) tested: hub (2148:7022),
  SanDisk stick (0781:5591) as `sda/sda1` (FAT32, 28.6GiB); Realtek RTL8152 (0bda:8152) on hub enumerated but
  no driver (`CONFIG_USB_NET_DRIVERS` off—could add `r8152`). Kernel has usb-storage, SCSI disk, vfat, exfat, ntfs (RO driver), no ext4.
- **Vendor rootfs auto-mounts sticks**: `/etc/hotplug.d/block/10-mount` + `/etc/config/fstab` mount `/dev/sda`, `/dev/sda1` `rw,async`
  at `/mnt/exUDISK` (outside chroot). Manual `mount -o ro` → `Resource busy` (already rw); must mount rw to share.
- **USB Host submenu (`usbhost.h` + `usbmode.c: run_host_page`)**: main *USB Host (OTG)* line, A for submenu:
  - First row `Host mode: < ON/OFF >` (left/right/A). ON = unbind UDC, stop `umsd`, read `usb_null`/`usb_host` for OTG role (**same Type-C port—
    ADB/gadget unavailable while host ON**); OFF = eject all sticks, device role, rebind gadget (ADB back).
  - Host ON: poll `/sys/bus/usb/devices` each second, list `[Storage]/[Hub]/[Input]/[Network]/[Audio]/[Internal]/[Other] name`,
    storage shows `reading... / mounting... / mounted (xxx MB) / mount failed`. Internal pads (ehci1/ohci1 Twin USB JoysticK) `[Internal]`.
  - Cursor on device row: bottom detail (bus path, VID:PID, speed, `sdXn:fs -> mount` or error).
  - Auto-mount storage: each partition (whole disk if no table), FS from first sector (FAT/exFAT/NTFS; ext2/3/4 reported unsupported),
    after 3s (wait vendor hotplug) rw mount `/mnt/SDCARD/USB/<sdXn>` (visible in/out chroot; **must rw**, see above), retry every 2s max 5;
    NTFS kernel driver RO—rw fail falls back RO.
  - A on storage row: mounted → safe eject (`sync`, `umount`, `rmdir`, `echo 1 > /sys/block/sdX/device/delete`); not mounted → retry mount.
  - Unplug: block gone → `umount -l` and cleanup mount point. Leaving host (OFF or main menu Storage/ADB/Serial/Network) ejects all first.
  - **Host persists after exiting pak**: Host ON starts background `usbmode.elf --host-daemon` (pid `/tmp/usbhost.pid`, state `/tmp/usbhost.state`,
    UI→daemon `/tmp/usbhost.cmd`). Daemon polls mount/cleanup each second **while port stays host** (exits and ejects when back to device);
    UI reads state, sends `eject/retry`. Re-enter pak `detect_current_state()` reads `otg_role` to restore host UI
    (old build skipped this—re-entry always showed device though role unchanged).
  - CLI self-test (no screen): `usbhost_selftest.sh <seconds>` (in pak dir, background) → host, every 2s list/mount and print first 12 files →
    eject and device, log `/mnt/SDCARD/usbhost_selftest.log`. **Empty run passed** (internal pad, switch OK);
    hub + stick mount/eject/screen **not tested yet**.
- Note: `/mnt/exUDISK` (vendor) outside chroot; after eject vendor mount may stale (can't clear in chroot)—doesn't affect `/mnt/SDCARD/USB/*`.
  Default `rw,async`—yank without eject risks corruption.
- Unsupported/TBD: ext4 sticks (fuse2fs exists), NTFS rw (vendor `ntfs-3g` musl, won't run glibc chroot), RTL8152 driver,
  volume labels, multi-hub indent.

---------------------------------------------------------------------

## 7. Quick Reference

- Device shell: `adb shell`; device `usb0=192.168.7.1`; Mac NIC often `en16` ("Tina ADB"), varies.
- Crash black box: `insmod /tmp/crashlog.ko; cat /proc/crashlog` (PC lookup: `tr -d '\r' < kallsyms.txt | sort`, nearest symbol ≤ PC).
- Disassemble vendor kernel: objdump in 4.3 (in `kbuild-arm` container; Mac has no arm objdump).
- Standard gadget relink: `echo "" > UDC` → add/remove `configs/c.1/*` → `echo 4100000.udc-controller > UDC` → verify UDC non-empty → configure IP.


### ext4 USB sticks (fuse2fs)
- No kernel ext4 (`CONFIG_EXT4_FS` off); vendor-side SD has musl `fuse2fs` (1.43.7) and libs (`/mnt/SDCARD/m21/{binmusl,libmusl}`),
  /overlay mounted same way on `rootfs.ext2`, `fuse.ko` loaded.
- glibc in chroot conflicts with musl—daemon started via init root and stock loader:
  `/proc/1/root/lib/ld-musl-armhf.so.1 --library-path /mnt/SDCARD/m21/libmusl:/proc/1/root/usr/lib:/proc/1/root/lib /mnt/SDCARD/m21/binmusl/fuse2fs <dev> <mnt> -o rw,noatime` (fallback ro).
  64MB ext4 image read/write OK in chroot on device.
- Limit: fuse2fs 1.43 no journal write; disks needing journal recovery fail mount (UI shows fuse2fs error)—fsck on PC.

### Some sticks read errors / garbage (hostbyte=0x07)
- Symptom: 58.5GB 3-partition "Mass Storage Device" (FAT/ext?/NTFS) via hub, `dmesg` spam `UNKNOWN(0x2003) hostbyte=0x07`,
  `usb 3-1.1: reset high-speed USB device ... using sunxi-ehci`, failed READ(10) 16KB/32KB (phys_seg 3–8, multi-segment SG);
  probe `/dev/sda2` occasionally `USBC` CBW garbage (buffer polluted). Small reads (partition table, FAT boot) OK.
- Guess: sunxi-ehci mishandles multi-segment bulk. Mitigation: on disk discovery daemon sets `/sys/block/<sd>/queue/max_sectors_kb` to 4
  (one phys segment per request), retry mount with `sync; echo 3 > /proc/sys/vm/drop_caches`.
- Failed probe writes first 2KB of partition header hex to `/mnt/SDCARD/usbprobe.txt` for offline analysis.


## 7. Windows Support: CDC-ACM Serial + NCM/RNDIS Network (this round)
- **Why Windows showed no driver**: `gser` is vendor class 0xFF; Windows auto-binds `usbser` only for standard CDC-ACM; ECM has no inbox driver.
  Kernel lacks ACM/NCM/RNDIS—cross-build `f_acm_x.ko` (`f_acm.c` stock), `f_ncm_x.ko` (bypass `can_support_ecm`, see `kmod/f_ncm.patch`),
  `f_rndis_x.ko` (`f_rndis.c`+`rndis.c` stock). Rebuild 5.4.61 tree: download → apply `kmod/*_pad.*` and `netdevice_h_pad.patch` → `kmod/build.sh`
  → `make M=<dir with kmod sources and f_acm/f_ncm/f_rndis/rndis.c>`.
- **New struct issue (`netdev_priv` offset)**: vendor `alloc_netdev_mqs` `ALIGN(sizeof(struct net_device))=1600`, mainline 1344; module `netdev_priv()` inline constant
  → `struct eth_dev` overlaps vendor net_device tail fields (`rtnl_link_ops` at 1412 etc.). ECM "worked by luck" (zeros there); NCM/RNDIS wrote `wrap/unwrap` pointers into `rtnl_link_ops`,
  `ifconfig usb0 up` oops in `if_nlmsg_size → strlen` (`strlen(e59f337c)`, r4 → `ncm_unwrap_ntb`). Fix: 256-byte pad at end of `struct net_device`, priv offset=1600.
  **All net modules (incl. ECM, u_ether) rebuilt with new headers.**
- **RNDIS notes**: `f_rndis` default IAD class 02/ACM/vendor—Windows rejects; set `functions/rndis.usb0/{class,subclass,protocol}` = `e0/01/03`. Configfs attrs use `sscanf("%02hhx")`—
  write `e0`, not `0xe0` (parses as 0). Device descriptor class `EF/02/01` (IAD composite).
- **Interface names**: each registered net function gets `usbN` (ecm→usb0, switch ncm→usb1…); `usbnet.sh up` reads `functions/<type>.usb0/ifname`, clears IP and downs other `usbN`, DHCP on correct iface.
- **Endpoint limit**: ACM/ECM/NCM/RNDIS each need one INT (ep4); UDC has one—serial and network mutex in menu; toggling one closes the other. ADB+storage+network can coexist.
- **Verified (Mac)**: `f_acm_x` → `/dev/cu.usbmodemXXXX`, bidirectional OK; NCM DHCP 192.168.7.2, ping 1400 and 8000-byte frag OK; ECM regression OK;
  boot paths (`usb_nettype`=ncm / rndis) reboot-verified. RNDIS on Mac: enumeration and descriptors only (class e0/01/03, device EF/02/01)—**real Windows driver bind awaits user test**.
- **Test tip**: gadget changes from adb need `echo "" > UDC` first; script must background and **use `usleep` not `sleep`** (unbind kills stock `sleep`, script vanishes); start with `(trap "" HUP; sh script &)`.

## 7.1 Real RNDIS Requirements on Windows (11)—Verified
Debug path: Device Manager showed "Remote NDIS based Internet Sharing Device" (E0/01/03) and "Remote NDIS Compatible Device" (MS OS compat ID)—both **code 10**,
device `rndis_setup` never called (Windows sent no requests after SET_CONFIGURATION). What finally worked:
1. **RNDIS must be first function in config (interfaces 0/1)**: `ffs.adb` first (RNDIS at 1/2) → code 10; RNDIS first → OK (Android rndis order).
   Config interface numbers follow **symlink creation order** in `configs/c.1/`, so `usbnet.sh on rndis` and `tomato` remove ffs/acm links before `rndis.usb0`, restore after.
2. Mainline `f_rndis.c` hardcodes CDC Call Management `bDataInterface`=1; wrong when RNDIS not on 0/1 vs Union desc—fixed to actual data ifnum (`kmod/f_rndis.patch`).
3. MS OS compat ID path (`os_desc`: `use=1`, `b_vendor_code=0xcd`, `qw_sign=MSFT100`, `rndis.usb0/os_desc/interface.rndis/compatible_id=RNDIS`, `sub_compatible_id=5162001`,
   `ln -s configs/c.1 os_desc/c.1`); function class/subclass/protocol stay default 02/02/ff; `bcdDevice` 0x0511 (Windows caches MS OS desc by VID/PID/REV). Other modes `osdesc_off` back to 0x0504.
- Note: `dhcpd_mini.elf` is glibc—run only in `chroot /overlay`. `adb shell` `usbnet.sh up` → `/tmp/dhcpd_mini.elf: not found` (Windows gets 169.254); menu runs in chroot OK.
  Test: `chroot /overlay /tmp/dhcpd_mini.elf usb0 192.168.7.1 192.168.7.2` (copy to `/overlay/tmp` first). `dhcpd_mini` logs DHCP packets to stderr (`/tmp/dhcpd.log`).
- Debug: `pr_info` in `f_rndis.c` `rndis_setup`/`rndis_command_complete`/`rndis_set_alt`; UDC `queue_debug`, `irq_debug` (`/sys/devices/platform/soc@3000000/4100000.udc-controller/`) =1 for ep queue/IRQ;
  Mac libusb: read config desc, MS OS strings/compat ID, manual RNDIS INIT (`SEND/GET_ENCAPSULATED_*`) to verify control path.

## 7.2 Windows Internet Sharing
- Symptom: Windows RNDIS gets 192.168.7.2, `ping 192.168.7.2` OK, device `ping 8.8.8.8`/`www.baidu.com` fail—same as Mac without NAT; host must NAT.
- Avoid Windows ICS: forces adapter to 192.168.137.1 with its own DHCP, conflicts with device 192.168.7.x (default route to 192.168.7.2).
- `host-tools/share-internet-windows.ps1 on|off|status` (`.bat` double-click elevates): find 192.168.7.x adapter and default-route egress,
  both `Set-NetIPInterface -Forwarding Enabled`, then `New-NetNat -Name USBShare -InternalIPInterfaceAddressPrefix 192.168.7.0/24`.
  Failures: overlap with WSL/Docker/Hyper-V NAT, `winnat` stopped, VPN egress (use `-Uplink`). **Script not verified on real Windows hardware.**
- **Tested (Windows Home)**: `New-NetNat` "invalid class" (missing NetNat WMI). Script falls back to ICS:
  registry `HKLM\SYSTEM\CurrentControlSet\Services\SharedAccess\Parameters` `ScopeAddress`/`ScopeAddressBackup`/`StandaloneDhcpAddress`=192.168.7.2 (default 192.168.137.1),
  stop `SharedAccess`, `HNetCfg.HNetShare` COM `EnableSharing(0)` on uplink, `EnableSharing(1)` on USB NIC, USB NIC static 192.168.7.2 matching device gateway.
  `off` disables sharing, removes those registry keys, USB NIC back to DHCP. **ICS fallback not verified on real hardware.**
