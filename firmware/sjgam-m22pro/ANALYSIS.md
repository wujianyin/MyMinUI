# SJGAM M22Pro Stock Firmware Analysis

> 中文版: [ANALYSIS.zh-CN.md](ANALYSIS.zh-CN.md)

Subject: SJGAM M22Pro (grouped under the `m21` platform in upstream MyMinUI); firmware is stock images I exported from the device SPI NOR.
All conclusions come from inspection of the images and the running device (`adb shell`); **judgments marked “inferred” were not directly verified**.

Image files are in [`images/`](images/), extracted configs/scripts in [`extracted/`](extracted/), checksums in [`images/SHA256SUMS`](images/SHA256SUMS).

---

## 1. Hardware overview

| Item | Details | Source |
|---|---|---|
| SoC | Allwinner **H133** (called `sun8iw20p1` in kernel/DTS, same series as T113), dual Cortex-A7, ARMv7, NEON/VFPv3 | DTS `compatible = "allwinner,h133", "arm,sun8iw20p1"`, `/proc/cpuinfo` |
| CPU frequency | 480 / 720 / 912 / 1008 / 1104 / **1200 MHz** | cpufreq `scaling_available_frequencies` |
| RAM | **256 MB** DRAM (≈234 MB usable, 8 MB CMA) | `androidboot.dramsize=256`, `/proc/meminfo`. Note: DTS memory node says 512 MB (0x20000000); actual size is overridden by U-Boot bootargs |
| On-board storage | **16 MiB SPI NOR** (U-Boot, kernel, stock rootfs) | `/proc/mtd`, partition sum exactly 0x1000000 |
| External storage | microSD (32 GB FAT32 on this unit), MinUI/games/`tomato` | `/dev/mmcblk0` |
| Display | MIPI-DSI panel **hx8399**, 1080×1920 portrait; framebuffer 720×1280 (`fb0` virtual 720×2560 double-buffered), display engine scales to 1080×1920 | Runtime `/proc/device-tree/.../lcd_driver_name` and `/sys/class/disp` |
| USB | One OTG port (`sunxi_usb_udc`, UDC name `4100000.udc-controller`), device/host switchable | sysfs `usbc0` `usb_device` / `usb_host` / `usb_null` |
| Audio | `audiocodec` (ALSA), line-out/MIC params in `/etc/rc.local` | rootfs |
| Input | `event0-3`, `js0-2` (gamepad nodes) | `/dev/input` |
| Serial | `ttyS0` 115200 (uart0, base 0x02500000) | U-Boot env / bootargs |

### 1.1 Two DTBs (important)
Two device trees appear in `mtd0`:

| File | Offset | Panel driver | Resolution | Notes |
|---|---|---|---|---|
| `extracted/dts/mtd0_dtb_0_0x862b8.dts` | 0x862b8 | `st7703s` | 640×480 (lcd_x=0x280, lcd_y=0x1e0) | **[inferred]**: config for a landscape 4:3 variant in the same series (M21), matches MinUI `m21` platform `FIXED_WIDTH/HEIGHT = 640×480` |
| `extracted/dts/mtd0_dtb_1_0xd6000.dts` | 0xd6000 | `hx8399` | 1080×1920 | Same as `2_dtb.bin` in the U-Boot package (60,608 bytes); **this unit runs hx8399** |

Same firmware framework supports multiple panels/models, distinguished by DTB. MyMinUI `m21` platform code must handle both.

---

## 2. Partitions and image formats

`/proc/mtd` (same as `images/mtd_info.txt`):

| Partition | Size | Contents | Image file | Format |
|---|---|---|---|---|
| mtd0 `uboot` | 0x100000 (1 MiB) | boot0 SPL + boot package (U-Boot, OP-TEE, DTB) | `mtd0_uboot.bin` | Allwinner `eGON.BT0` + `sunxi-package` |
| mtd1 `env` | 0x20000 (128 KiB) | U-Boot environment | `mtd1_env.bin` | U-Boot env (CRC + text) |
| mtd2 `boot` | 0x400000 (4 MiB) | Kernel | `mtd2_boot.bin` | Android boot image v0 (page size 2048) |
| mtd3 `boot-resource` | 0xed800 (≈950 KiB) | Boot logo and resources | `mtd3_boot-resource.bin` | FAT16 |
| mtd4 `rootfs` | 0x9f2800 (≈10.2 MB) | Stock system | `mtd4_rootfs.squashfs` | SquashFS 4.0, xz, 128 KiB blocks |
| mtd5 `UDISK` | 0 | Unused (empty) | (not provided) | — |

> On the running device, `mtd4_rootfs.squashfs` is `/dev/root`, **100% full (9.6M) and read-only**, so nothing can be written there —
> which is why MyMinUI puts a separate ext2 `rootfs.ext2` on SD and `chroot`s into it.

### 2.1 mtd0: SPL and boot package
- `0x0`: `eGON.BT0` boot0 SPL (extracted as `boot0_spl.bin`, 64 KiB, starts with `f0 00 00 ea` (ARM branch) + `eGON.BT0` magic).
- `0x10000`: `sunxi-package` boot package containing
  - `u-boot.bin` (507,904 bytes, U-Boot **2018.05-g89c9751-dirty, built 2022-08-10**, from kernel boot param `uboot_message`)
  - `optee.bin` (300,344 bytes, OP-TEE; DTS has `linaro,optee-tz` node)
  - `dtb.bin` (60,608 bytes, the hx8399 DTB above)
- Extract DTB: search `mtd0.bin` for FDT magic `d0 0d fe ed`, read `totalsize` from offset, then `dtc -I dtb -O dts`.

### 2.2 mtd1: U-Boot environment
See [`extracted/bootloader/mtd1_uboot_env.txt`](extracted/bootloader/mtd1_uboot_env.txt); highlights:
```
bootdelay=0
bootcmd=run setargs_nor boot_normal
boot_normal=sunxi_flash read 43000000 ${boot_partition};bootm 43000000
setargs_nor=... console=ttyS0,115200 loglevel=8 root=/dev/mtdblock4 rootfstype=squashfs init=/sbin/init cma=8M ... gpt=1
```
- `mac` / `wifi_mac` / `bt_mac` / `snum` are empty (no per-device unique data written; safe to publish).
- Actual kernel command line (runtime `/proc/cmdline`) also includes `partitions=env@mtdblock1:boot@mtdblock2:boot-resource@mtdblock3:rootfs@mtdblock4:UDISK@mtdblock5`, `androidboot.dramsize=256`, `disp_reserve=3686400,0x4bf32bc0`, etc.

### 2.3 mtd2: Kernel
- Android boot image: kernel 3,901,168 bytes, load address `0x40008000`; ramdisk only 12 bytes (placeholder); kernel image is **gzip-compressed zImage**, gzip stream inside zImage at offset `0x6893` (zImage itself at offset 2048 in `mtd2`), decompressed raw `vmlinux.bin` is 10,805,168 bytes (base `0xc0008000`).
- Kernel: **Linux 5.4.61 #664 SMP PREEMPT, 2024-10-25 06:03:41 UTC**, toolchain `arm-openwrt-linux-muslgnueabi-gcc 6.4.1` (Linaro 6.4-2017.11).
- `CONFIG_ARM_APPENDED_DTB=y`, `CONFIG_KERNEL_GZIP=y`, `CONFIG_KALLSYMS=y`, **`CONFIG_MODVERSIONS` not enabled** (out-of-tree modules need no version CRC, only vermagic match).
- Full config: [`extracted/kernel/kernel_config.txt`](extracted/kernel/kernel_config.txt); symbols: [`extracted/kernel/kallsyms.txt`](extracted/kernel/kallsyms.txt) (for disassembling `vmlinux.bin`; see §6).

### 2.4 mtd3: boot-resource
FAT16 image containing: `BOOTLOGO.BMP` (480×480×24bpp, saved as `BOOTLOGO.png`), `MAGIC.BIN` (512 bytes).

### 2.5 mtd4: rootfs
- SquashFS 4.0 / xz / 10,064,856 bytes / 891 inodes / built 2024-10-26.
- Expand with `unsquashfs -d mtd4_rootfs images/mtd4_rootfs.squashfs` (full tree ≈37 MB, ~14 MB built-in NES games; this repo does not include the full expanded tree, only scripts/configs and full listing `extracted/rootfs_filelist.txt`).

---

## 3. Stock system (Tina Linux)

- OpenWrt-based **Tina Linux 4.0.0**, codename *Neptune*; build `tina.root.20241025.060322`, target `h133-p2nor/generic v3.5`.
- `/etc/os_version`: `2024.10.26-v1.1.1`; `/etc/model`: `M22`.
- C library **musl** (`/lib/ld-musl-armhf.so.1`), busybox, procd/ubus/uci; **no glibc**.
- Kernel modules only `fuse.ko` and netfilter/iptables set (including `nf_nat`, `iptable_nat`, `xt_REDIRECT`) — so “shared internet/NAT” is feasible on the stock kernel.
- Stock rootfs root password hash is vendor default (`/etc/shadow`); serial/adb give root shell with no hardening.

### 3.1 Boot flow
```
BROM → boot0 SPL (SPI NOR 0x0)
     → U-Boot 2018.05 + OP-TEE (boot package)
     → Read mtd2 `boot`, bootm 0x43000000 (zImage + appended DTB in Android boot image)
     → Linux 5.4.61, root=/dev/mtdblock4 (squashfs)
     → /sbin/init (procd) → /etc/init.d/rcS
         · adbd   (START=80)
         · S99run (START=99)
```
Key points of `/etc/init.d/S99run`:
1. `mount /dev/mmcblk0p1 /mnt/SDCARD` (SD partition 1), create symlinks `/sdcard`, `/data`.
2. Run `/usr/bin/switchev` in background (**[inferred]** key/input event handling; not confirmed by disassembly).
3. Loop: `while [ -f /usr/guimenu ]`: read `/sys/class/gpadc/data`, `charge` for low-battery check (`/usr/bin/lowpower`), then run `/usr/guimenu`.

`/usr/guimenu` (script) picks the front-end:
```sh
export LD_LIBRARY_PATH=/mnt/SDCARD/lib
export HOME=/mnt/SDCARD
CMD=/usr/bin/smallui                      # built-in fallback UI
if [ -f /usr/tomato ]; then MISCBIN=/mnt/SDCARD/tomato
else MISCBIN=/mnt/SDCARD/emulationstation; fi
[ -f $MISCBIN ] && CMD=$MISCBIN           # use SD path if present
if $CMD; then [ -f /tmp/.next ] && sh /tmp/.next ...; fi
```
> **Key finding**: rootfs has a 1-byte `/usr/tomato` file, so `guimenu` runs **`/mnt/SDCARD/tomato` on the SD card**.
> That is the entry for MinUI third-party systems (vendor hook for custom stacks); without it, fallback is `emulationstation`, then built-in `smallui`.
> `tomato` is a shell script: fuse2fs mounts SD `rootfs.ext2` on `/overlay`, bind-mounts dev/proc/sys/configfs/SDCARD/tmp, then `chroot /overlay` to start MinUI.
> USB-related changes to `tomato` in this repo: `workspace/m21/other/usbmode/tomato.m21`.

### 3.2 Stock apps and libraries
- Front-end: `smallui`, `switchev`, `lowpower`, `testmode` (test mode when `/tmp/.testmode` exists).
- Graphics: DirectFB 1.7.7 (`/usr/lib/directfb-1.7-7`, `dfb*` tools), tslib, freetype; audio: ALSA, `libsamplerate`.
- Emulator: `/usr/bin/fceumm_libretro.so` (NES core), ~14 MB NetChip NES games under `/game/` (vendor built-in, **not included**).
- Network: `wpa_supplicant`, `hostapd`, `dnsmasq`, `ntpd`, `curl`/`wget`; BT helper scripts `bt_as_on/off/play`, `sbcenc/dec`.
- `/www/luci-static` static assets only; no web server seen under rootfs `usr/sbin`.
- Note: stock rootfs (musl) vs MinUI chroot (glibc 2.26) are **two userlands** sharing one kernel and `/dev`, `/sys`.

### 3.3 USB / ADB (stock implementation)
- `/etc/init.d/adbd`: calls `/bin/setusbconfig adb`, creates gadget `g1`: VID `0x18d1` / PID `0xD002`, manufacturer "Allwinner", product "Tina ADB", default serial `20080411`; `adbd -D` via FunctionFS (`/dev/usb-ffs/adb`).
- `/bin/setusbconfig` claims `mtp / adb / rndis / adb,mtp` (and `uac1`, `hid`, etc.), but **current kernel only has built-in `gser` and `ffs`**: `rndis`/`ecm`/`ncm` kernel function modules were not built; rndis option in the script does not work on this device —
  which is why USB networking needs custom out-of-tree modules (see `workspace/m21/other/usbmode/NOTES.md`).
- UDC driver `sunxi_udc` sets `quirk_altset_not_supp`, and `struct usb_gadget`, `struct module`, `struct sk_buff`, etc. differ from mainline 5.4.61 layout (details in NOTES).
- Endpoint resources: ep1/ep2/ep5 bulk pairs each, ep4 interrupt, ep3 iso.

---

## 4. Conclusions useful for MyMinUI porting
1. Entry is `/usr/tomato` + `/mnt/SDCARD/tomato`; no need to rewrite SPI NOR — **a bad install is recovered by replacing/editing SD content only; no brick from that path**.
2. Stock rootfs is full and read-only; new userland must live on SD (ext2 image + `chroot`).
3. Kernel lacks `CONFIG_MODVERSIONS`; vendor `.config` can build out-of-tree modules; header struct offsets must be verified (NOTES has full method and patch table).
4. Display resolution/rotation differ by model (different DTB).
5. `panic=0` and no persistent ramoops: kernel crash in interrupt waits for watchdog reset, no log. RTC GP registers (physical `0x07090100`) survive reset; I used them for a “crash black box” (`crashlog.ko`, see NOTES).

---

## 5. How to reproduce extraction yourself

```sh
# Read partitions on device (sizes from /proc/mtd)
adb shell cat /dev/mtdblock0 > mtd0.bin   # likewise 1..4
adb shell cat /proc/mtd

# rootfs
unsquashfs -d mtd4_rootfs mtd4_rootfs.squashfs

# Device tree
dtc -I dtb -O dts -o x.dts x.dtb          # DTB locations see §1.1 / §2.1

# Kernel (verified: byte-identical to exported vmlinux.bin)
python3 - <<'EOF'
import zlib, struct
d = open('mtd2_boot.bin','rb').read()
ksize = struct.unpack('<I', d[8:12])[0]            # Android boot image header: kernel_size = 3901168
zimage = d[2048:2048+ksize]                        # kernel from page 1 (page size 2048) = zImage
open('zImage','wb').write(zimage)
z = zimage.find(b'\x1f\x8b\x08')                  # gzip stream inside zImage, offset 0x6893
open('vmlinux.bin','wb').write(zlib.decompressobj(31).decompress(zimage[z:]))   # 10805168 bytes
EOF
# Disassemble (no arm objdump on Mac; run in docker)
arm-linux-gnueabihf-objdump -D -b binary -m arm --adjust-vma=0xc0008000 vmlinux.bin
```

---

## 6. Known open / to confirm
- Which exact models the `hx8399` vs `st7703s` DTBs map to — no evidence; above is inferred.
- How U-Boot overrides DTS 512 MB DRAM (only confirmed actual 256 MB).
- Contents of `MAGIC.BIN`, `optee.bin` not analyzed.
- `/etc/config/smartlinkd`, `/etc/miracast.cfg` suggest cast/provisioning features; usability not tested.
- Whether more kernel `CONFIG_USB_*` can be enabled (only confirmed ECM/NCM/RNDIS not built).

## 7. Copyright and risk notice
- These images are **verbatim exports of stock device firmware**; copyright Allwinner and device vendor; hosted for research/compatibility/backup only; may not match your device batch.
- **Flashing SPI NOR carries brick risk**; this repository does not provide or recommend flashing; MyMinUI only needs an SD card.
- Images contain no MAC, serial, or other unique device IDs (U-Boot env empty), but `/etc/shadow` has vendor default root password hash.
