# SD card recovery (SJGAM M22Pro / MyMinUI)

> 中文版: [SD_RESTORE.zh-CN.md](SD_RESTORE.zh-CN.md) | Back to [README.md](README.md)

Two situations are covered: **A.** the card is damaged or was reformatted (rebuild it), and **B.** the card is intact but the kernel remounted it **read-only**
(MinUI sticks on the colour bar, repair it with `fsck.fat`).

## A. Rebuilding a damaged card

On 2026-10-21 the card was corrupted and reformatted. Everything below comes from files in this repository and can be repeated.

### A.1 Base system

1. Format the card as FAT32.
2. Either extract the zip you built with `workspace/m21/other/dockerbuild/package.sh` to the card root (recommended: it already contains the changes listed in A.2),
   or extract the last upstream m21 release (`MyMinUI-20260208b-0-m21.zip`, <https://github.com/Turro75/MyMinUI/releases/tag/MyMinUI-20260208>), copy `m21/emulationstation` and `m21/tomato` to the root (see the SJGAM section of `INSTALL.md`) and then apply A.2.
3. Restore your ROMs (`Roms/`) and BIOS files (`Bios/`). The `m21/thisism22` marker is created by `tomato` on the M22Pro.

### A.2 What this fork changed

| What | Source (repository) | Location on the card |
|---|---|---|
| Boot script with USB boot defaults | `workspace/m21/other/usbmode/tomato.m21` | `/tomato` (overwrite; 11749 bytes) |
| USB Mode tool | `skeleton/EXTRAS/Tools/m21/USB Mode.pak/` | `/Tools/m21/USB Mode.pak/` |
| Language tool and first-start picker | built by `dockerbuild/` (`language.elf`), `skeleton/EXTRAS/Tools/m21/Language.pak/` | `/.system/m21/bin/language.elf`, `/Tools/m21/Language.pak/` |
| UI language catalogs | `skeleton/SYSTEM/res/lang/`, `skeleton/EXTRAS/Tools/m21/*/lang/` | `/.system/res/lang/`, `/Tools/m21/<tool>.pak/lang/` |
| Current language | chosen on the first start | `/.userdata/shared/language` (`zh_CN` or `en`) |
| System defaults (`minarch_prevent_tearing = Lenient`) | `skeleton/SYSTEM/m21/system.cfg` | `/.system/m21/system.cfg` |
| USB boot defaults | see below | `/.userdata/m21/usb_boot_mode.cfg`, `usb_nettype` |
| CJK font (same-name replacement) | `workspace/m21/other/cjkfont/BPreplayBold-unhinted.otf` | `/.system/res/BPreplayBold-unhinted.otf` |
| Files.pak CJK font (same-name replacement) | `workspace/m21/other/cjkfont/Files.pak/res/FreeSans.ttf` | `/Tools/m21/Files.pak/res/FreeSans.ttf` |
| Test ROM (optional) | `skeleton/EXTRAS/Roms/Pico-8 (P8)/` | `/Roms/Pico-8 (P8)/` |

```sh
printf 'adb_only\nnet\n-\n' > /.userdata/m21/usb_boot_mode.cfg   # adb on, USB network on, serial off
printf 'ncm\n'             > /.userdata/m21/usb_nettype
```

The per-core `minarch.cfg` files (key bindings ...) live in `.userdata/m21/<system>-<core>/` and are **not** in the repository; only your own backups restore them.
A core you have never played uses the `system.cfg` defaults (tearing `Lenient`, sync `Auto`).

### A.3 Not on the SD card

- The **60 Hz patch** is in the device's internal SPI flash, so card damage does not affect it. See `workspace/m21/other/dtb_60fps/`.
- Recovering a broken SPI: `reboot efex` to enter FEL and write the full backup back with the patched `sunxi-fel`, or use a CH341 programmer with the same backup.

### A.4 Not recoverable

Saves (`Saves/`), save states, the recently played list, brightness/volume/sleep settings. Make a full image of the card from time to time
(with a card reader: `sudo dd if=/dev/rdiskN of=tf.img bs=4m`, check N with `diskutil list` first).

### A.5 Why it got damaged (inferred)

See the end of `workspace/m21/other/usbmode/FEATURES_AND_ISSUES.md`: the device mounts the card read-write while USB Storage in read-write mode lets the computer write to the same FAT.
USB Mode now defaults to read-only; do not copy files to the card in read-write mode, use a card reader.

### A.6 Checks after restoring (adb)

`ref_fps:60`; `/mnt/SDCARD/tomato` is 11749 bytes; `Tools/m21/USB Mode.pak` exists; `usb_boot_mode.cfg` is `adb_only/net/-`; `system.cfg` has `Lenient`;
`/sys/kernel/config/usb_gadget/g1/functions` has `ffs.adb` and `ncm.usb0`; `usb0` is `192.168.7.1`; `/.system/res/lang/zh_CN.txt` exists.

## B. The card became read-only (MinUI stuck on the colour bar)

### Symptom and cause

After a hard power cut, a reboot or a bad FAT write, the kernel found a FAT error and remounted the card with `errors=remount-ro`.
`launch.sh` starts `minui.elf > $LOGS_PATH/minui.txt`; on a read-only card that redirection fails, `minui.elf` never starts and the loop keeps retrying,
so the screen stays on the boot colour bar. adb still works.

Check it:

```sh
adb shell 'mount | grep "on /mnt/SDCARD "'        # ro,... means read-only
adb shell 'dmesg | grep -i -E "fat|mmcblk0"'      # FAT-fs error lines
```

The stock firmware has no `fsck.vfat`, so a static ARM `fsck.fat` (dosfstools 4.2) is built for it.

### Repair (about a minute)

```sh
# 1. host: build the tool once (needs Docker and the kbuild-arm2 image, see workspace/m21/other/dockerbuild/README.md)
OUT=/tmp/fsckb sh workspace/m21/other/dockerbuild/build_fsckfat.sh

# 2. dry run, nothing is written (the card may stay mounted read-only)
adb push /tmp/fsckb/fsck.fat /tmp/fsck.fat && adb shell chmod +x /tmp/fsck.fat
adb shell '/tmp/fsck.fat -n -v /dev/mmcblk0p1'

# 3. repair, check again, reboot so the kernel forgets the old error state
adb shell '/tmp/fsck.fat -a -v /dev/mmcblk0p1; sync'
adb shell '/tmp/fsck.fat -n /dev/mmcblk0p1'
adb shell 'sync; reboot'
```

Take a copy of the user data (`.userdata`, `Saves`, `Roms`) first if you can. `adb exec-out` does not work on this device, so use `adb pull` for files
(or a card reader on the computer: `diskutil` / `dd` the card, then `fsck_msdos` on macOS).
`fsck.fat -a` only touches metadata and removes what it cannot reconcile (it truncates broken files and frees orphaned clusters).

### What the 2026-10-25 repair found

On the card of the M22Pro (FAT32, 7.3 GB, `/dev/mmcblk0p1`) the damage was small:

- `/.userdata/shared/.minui/recent.txt`: size 211 bytes but an empty cluster chain (the chain pointed at a free cluster). Truncated to 0 bytes, then MinUI rewrote it.
- 7 directories (`Bios`, `Cheats`, `Emus`, `M21`, `Roms`, `Tools`, `.Trashes/...`) had an invalid `..` entry. Fixed.
- 25 unreferenced clusters (100 KB) reclaimed; the dirty bit and the free-cluster counter corrected.

After the repair and a reboot: the card was mounted `rw`, `minui.elf` ran, adb came up automatically, and the language, fonts and `tomato` were untouched.

### Avoiding it

Shut down from the menu instead of cutting power; do not kill the device while MinUI is writing (list changes, save states); do not write to the card from the computer in USB Storage read-write mode while the device is running.
