# MyMinUI-M22Pro

> 中文版: [README.zh-CN.md](README.zh-CN.md). Every document in this fork has an English primary version (`X.md`) and a Chinese version (`X.zh-CN.md`) next to it; upstream's own documents are English only.

This is a fork of [Turro75/MyMinUI](https://github.com/Turro75/MyMinUI), which is itself a fork of [shauninman/MinUI](https://github.com/shauninman/MinUI).
The complete upstream README (supported devices, features, installation) is kept as [README.upstream.md](README.upstream.md).
This file only covers **what this fork is for, what it changed and where it stands**.

## Goal

Development focuses on one device: the **SJGAM M22Pro** (Allwinner H133 / `sun8iw20`, 256 MB RAM; the `m21` platform upstream).

- Treat it as a handheld that can be debugged and extended at any time (USB networking, storage, debug ports ...).
- Collect low-level knowledge about the device (stock firmware, partitions, boot flow, kernel/USB stack differences).
- Stay close to upstream; general fixes go back upstream, device specific work stays here.

Other platforms are left as upstream has them and have **not** been tested by this fork.

## Documents

| Document | What is in it | Chinese |
|---|---|---|
| [INSTALL.md](INSTALL.md) / [MANUAL.md](MANUAL.md) | Upstream installation and user manual (SJGAM section in INSTALL.md) | - |
| [I18N.md](I18N.md) | UI languages: first-start picker, `Tools/Language.pak`, how to add a language | [I18N.zh-CN.md](I18N.zh-CN.md) |
| [M21_STATUS.md](M21_STATUS.md) | State of the `m21` port upstream, known problems, audit of upstream changes since 20260208 | [M21_STATUS.zh-CN.md](M21_STATUS.zh-CN.md) |
| [SD_RESTORE.md](SD_RESTORE.md) | Rebuilding a damaged SD card, repairing a read-only card (`fsck.fat`) | [SD_RESTORE.zh-CN.md](SD_RESTORE.zh-CN.md) |
| [workspace/m21/other/dockerbuild/](workspace/m21/other/dockerbuild/README.md) | Local Docker build, packaging and verification of the m21 release zip | [README.zh-CN.md](workspace/m21/other/dockerbuild/README.zh-CN.md) |
| [workspace/m21/other/cjkfont/](workspace/m21/other/cjkfont/README.md) | CJK font replacement files (already shipped in the package) | [README.zh-CN.md](workspace/m21/other/cjkfont/README.zh-CN.md) |
| [workspace/m21/other/usbmode/](workspace/m21/other/usbmode/README.md) | USB Mode source, kernel modules, `tomato` patch, [NOTES.md](workspace/m21/other/usbmode/NOTES.md) (full work log), [FEATURES_AND_ISSUES.md](workspace/m21/other/usbmode/FEATURES_AND_ISSUES.md) | [README.zh-CN.md](workspace/m21/other/usbmode/README.zh-CN.md), [NOTES.zh-CN.md](workspace/m21/other/usbmode/NOTES.zh-CN.md) |
| [workspace/m21/other/dtb_60fps/](workspace/m21/other/dtb_60fps/RISK_ASSESSMENT.md) | 60 Hz internal panel patch (verified on M22Pro) and FEL recovery, [pantrace](workspace/m21/other/dtb_60fps/pantrace/README.md) | [RISK_ASSESSMENT.zh-CN.md](workspace/m21/other/dtb_60fps/RISK_ASSESSMENT.zh-CN.md) |
| [firmware/sjgam-m22pro/](firmware/sjgam-m22pro/) | Stock SPI NOR dump and [ANALYSIS.md](firmware/sjgam-m22pro/ANALYSIS.md) | [README.zh-CN.md](firmware/sjgam-m22pro/README.zh-CN.md), [ANALYSIS.zh-CN.md](firmware/sjgam-m22pro/ANALYSIS.zh-CN.md) |

Short version of the device: Tina Linux (musl), Linux 5.4.61, 16 MiB SPI NOR with a full read-only rootfs. The stock `guimenu` runs
`/mnt/SDCARD/tomato` when `/usr/tomato` exists, so MinUI boots from the SD card only. A bad install is fixed by restoring the SD card, the device cannot be bricked by it.

## What this fork adds

### 1. A buildable m21 release (no upstream toolchain needed)

The upstream toolchain repository is gone. `workspace/m21/other/dockerbuild/` builds all m21 programs with a Docker cross compiler against the device's own glibc 2.26,
verifies them under qemu with the real device rootfs (`verify.sh`) and assembles `releases/MyMinUI-<date>b-0-m21.zip` (`package.sh`).
The zip is extract-to-card (FAT32 root), contains the CJK fonts, the fixed `tomato`, the language catalogs and `USB Mode.pak`.

M22Pro fixes found while auditing the upstream commits after 20260208 (details in [M21_STATUS.md](M21_STATUS.md) section 6.1):
RGB565-only display layer (GBA back to 59 fps), HUD frequency/font, brightness/volume saving, several undefined-behaviour bugs.

### 2. Switchable UI language (English / Simplified Chinese)

Everything is translatable: MinUI desktop, in-game menu (minarch), the tools and USB Mode. The first start shows a language picker,
and `Tools/Language.pak` changes it later. Each pak keeps its own translations inside the pak. See [I18N.md](I18N.md).

The four `Toggle*` tools now show their current value in the list (for example `ToggleSleepMode: Sleep` or `ToggleHideState: On`), so you can tell what the next press will do.

### 3. USB Mode.pak (`Tools/m21/USB Mode.pak`)

An interactive USB manager that switches USB functions at runtime (no reboot):

| Feature | Notes | Status |
|---|---|---|
| USB Storage | Expose the SD card to a computer, read-write or read-only (userspace FunctionFS `umsd`) | Works on Windows and macOS |
| ADB | Switch adb on/off | Tested. Occasionally adb stops after toggling Network/Host: turn it off and on again |
| USB Serial | CDC-ACM serial (`ttyGS0` + login shell), driverless on Windows/macOS/Linux | Tested (COM port on Windows, also selectable at boot) |
| USB Network (sub menu) | ECM / NCM / RNDIS Ethernet, device `192.168.7.1`, host gets `192.168.7.2` from a built-in DHCP server; per-OS "how to connect" pages | macOS (ECM/NCM) incl. ping to the Internet works; Windows (NCM, RNDIS) enumerate, but name resolution after sharing is still a problem |
| Share the host's Internet | Device side adds the default route `192.168.7.2` and DNS automatically; host NAT scripts in the pak's `host-tools/` (`share-internet-macos.sh`, `-linux.sh`, `-windows.bat`) | macOS verified; Windows reaches 8.8.8.8 but DNS hangs; Linux script not verified on hardware |
| OTG role | Device (to a computer) or Host (gamepad, USB drive; needs a self-powered hub, the Type-C port does not supply 5 V) | Tested |
| USB Network tri-state | OFF / ON (Internet via the PC) / ON (local only, no default route/DNS) | Tested |
| Boot Defaults (sub menu) | Check what is enabled at boot: ADB, storage (off/RW/RO), serial, network (and type); serial and network are exclusive | `adb_only`, `adb_ums_ro`, `adb_ums_rw`, `adb_only`+`net`, `adb_only`+`serial` tested after reboot; `off` and "ADB off at boot" not tested |

The vendor kernel has no ECM/NCM/RNDIS, so USB networking uses **out-of-tree modules** (`u_ether_x.ko`, `f_ecm_x.ko`) built against 5.4.61 headers
padded to match the vendor struct layouts. Source, modules and the full work log: [workspace/m21/other/usbmode/](workspace/m21/other/usbmode/README.md).

### 3b. Boot hook `tomato`

`workspace/m21/other/usbmode/tomato.m21` is the current `/mnt/SDCARD/tomato` (11749 bytes): it is the upstream `boot.sh` plus the USB logic
(unbind the UDC before linking functions, bind-mount the FunctionFS mount into the chroot, ECM fallback). The release zip ships it as the root `tomato`.
Boot config `/.userdata/m21/usb_boot_mode.cfg`: line 1 `adb_only | adb_ums_rw | adb_ums_ro | ums_rw | ums_ro | off`, line 2 `net` or `-`, line 3 `serial` or `-`.
Boot log: `/mnt/SDCARD/usb_adb_debug.log`.

### 4. Chinese fonts

The shared UI font and the Files.pak font are replaced with same-name files that contain GB2312 + kana (Latin glyphs and vertical metrics unchanged),
so Chinese ROM names and menus render without recompiling anything. See [workspace/m21/other/cjkfont/](workspace/m21/other/cjkfont/README.md).

## Quick start

1. Format an SD card as FAT32.
2. Build the zip (`workspace/m21/other/dockerbuild/`, see its README) or take a release zip, and extract it to the card root.
3. Put ROMs in `Roms/` and BIOS files in `Bios/`; boot the device (the first start installs and creates swap, be patient).
4. On the first desktop start you pick the language. USB features are under *Tools > USB Mode*; a USB network adapter ("Tina ADB") then appears on the computer and `ping 192.168.7.1` works.
5. To share the host's Internet, copy `host-tools/` from the pak to the computer and run the script for your OS with `on` (use `off` to stop).
   Details: *USB Mode > USB Network > How to connect* on the device.

If you already have a card running an older `tomato`, compare it with `tomato.m21` and check syntax with `sh -n tomato`; do not blindly overwrite it.

## Known issues / TODO

- **Unusable (measured):** the stock `curl` (no protocols compiled in), `wget` over https, ECM on Windows (no driver; use NCM/RNDIS), RNDIS on macOS (no driver; use ECM/NCM).
- **Recoverable:** adb sometimes stops after toggling other switches; turn adb off and on in USB Mode.
- **Partial:** after sharing the Internet from Windows, `ping 8.8.8.8` works but name resolution does not. See `workspace/m21/other/usbmode/FEATURES_AND_ISSUES.md` section 0.
- The device has **no WiFi hardware/driver**; the only way online is the host sharing its connection. `dhcpd_mini` intentionally hands out a single lease without gateway/DNS (it would hijack the host's default route).
- USB endpoints are limited (3 bulk pairs + 1 interrupt): serial and network are exclusive; ADB + storage + network can coexist.
- The prebuilt `.elf`/`.ko` of `USB Mode.pak` are committed; they are not part of the workspace makefile (build steps are in NOTES.md).
- The real source of `tomato` was never found in the repository (`build/FULL/m21/tomato` is git-ignored); `tomato.m21` is merged by hand with the current upstream `boot.sh`.
- Exposing the SD card read-write to a computer while the device uses it can corrupt it, and a hard power cut can leave the card read-only: the kernel remounts FAT read-only after an error and MinUI then sticks on the colour bar. Repair steps: [SD_RESTORE.md](SD_RESTORE.md). Prefer a clean shutdown.
- Not verified on hardware: auto-sleep/wake on battery (adb keeps the device in "charging"), cores other than GBA/PICO-8, the ZXS core (no m21 build yet).
- Kernel structs of the vendor kernel differ from mainline 5.4.61: check offsets before adding any new out-of-tree module (NOTES.md has the method and a table).

## Layout

```
firmware/sjgam-m22pro/                     stock firmware dump + analysis
skeleton/SYSTEM/res/lang/                  shared UI catalogs (zh_CN.txt)
skeleton/EXTRAS/Tools/m21/<tool>.pak/lang/ per-tool catalogs
skeleton/EXTRAS/Tools/m21/USB Mode.pak/    USB Mode pak (prebuilt)
workspace/all/language/                    Language picker source
workspace/m21/other/usbmode/               USB Mode source, kernel modules, tomato patch, NOTES.md
workspace/m21/other/dockerbuild/           Docker build, package.sh, verify.sh, build_fsckfat.sh
workspace/m21/other/cjkfont/               CJK font replacements
README.upstream.md                         upstream README (verbatim)
```

## Credits and license

- Upstream: [shauninman/MinUI](https://github.com/shauninman/MinUI), [Turro75/MyMinUI](https://github.com/Turro75/MyMinUI) and everyone listed there.
  This fork keeps the upstream license terms (see the upstream repositories).
- Firmware images under `firmware/` are unmodified dumps; copyright belongs to Allwinner and the device vendor. They are provided for research, backup
  and compatibility work only. **Do not flash the SPI NOR.** Use at your own risk.
- Not affiliated with SJGAM, Allwinner or the MinUI authors.
