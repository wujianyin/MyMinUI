# USB Mode.pak (m21) — New Features and Issues Summary

> 中文版: [FEATURES_AND_ISSUES.zh-CN.md](FEATURES_AND_ISSUES.zh-CN.md)

Scope: development round 2026-10-19 ~ 2026-10-20 (USB Host, ext4, serial/network Windows support, internet-sharing scripts, diagnostics, two-level menus).
Details and debugging notes: `NOTES.md` (§4–§7); usage: `README.md`.

## 0. Feature status overview (user testing, 2026-10-20)

Legend: ✅ works · ⚠ partly works / recoverable · ❌ **unusable**

### ❌ Unusable
| Feature | Symptom | Cause / notes |
|---|---|---|
| On-device `curl` | Neither http nor https works | Stock curl was built with no protocols (`Protocols:` empty); device tool limitation, not a USB network issue |
| On-device `wget` for https | https fails (http works) | Stock wget has no TLS |
| Windows + ECM network type | Unusable (expected) | Windows has no inbox ECM driver; use NCM (Win11) or RNDIS on Windows; not specifically tested |
| macOS + RNDIS network type | Unusable (expected) | macOS has no RNDIS driver; only enumeration verified; use ECM or NCM on Mac |

### ⚠ Partly works / recoverable
| Feature | Symptom | Mitigation |
|---|---|---|
| ADB toggle | Occasional: adb stops working after toggling USB Network, USB Host, or other switches | Turn ADB off then on again in the menu (user confirmed); root cause not investigated |
| Windows internet sharing (NCM / RNDIS) | Link comes up, `ping 8.8.8.8` works, but `ping www.baidu.com` fails (DNS) | Unresolved; see §4 item 1 |
| USB Host non-storage devices | Enumeration only; function not verified | Keyboard/gamepad etc. untested |
| USB Host power | Type-C port does not supply 5 V | Must use a self-powered hub |

### ✅ Works
| Feature | Platform / scope |
|---|---|
| USB Storage (RW/RO) | Mount and read on Windows and macOS |
| USB Serial (CDC-ACM) | Windows sees a COM port and shell works; boot default with serial also works |
| USB Network ECM / NCM | Works on macOS; `ping www.baidu.com` OK |
| USB Network NCM / RNDIS | Windows enumerates and gets 192.168.7.2 |
| USB Host (powered hub) | FAT32, EXT4, NTFS USB sticks mount and read; other USB devices enumerate |
| Menu (submenus, help pages, Boot Defaults) | Displays correctly on device screen |

## 1. New features

### 1.1 USB Host (OTG host) sub page
| Feature | Description |
|---|---|
| Host mode persistence | Host mode stays on after leaving the pak (background daemon `usbmode.elf --host-daemon`); re-entry shows the real role (reads `otg_role`) |
| Device list + auto-mount storage | Enumerates USB devices; USB stick partitions mount under `/mnt/SDCARD/USB/<sdXn>` (visible in the file manager) |
| Storage row A-button action menu | Eject / Mount-retry / Details / Cancel |
| Non-destructive eject | After eject the device node remains (no SCSI delete); UI shows "ejected (safe to unplug)"; remount from menu without replugging; also releases system auto-mount `/mnt/exUDISK` |
| Details page for any device | Type, VID:PID, port, speed, USB version, manufacturer, power, serial, interfaces/drivers, mount point |
| ext2/3/4 USB sticks | Mount via stock `fuse2fs` (rw first, ro fallback); vfat/exfat/ntfs (read-only) via kernel `mount()` |
| Unknown filesystem diagnostics | Shows magic bytes when unrecognized; writes `/mnt/SDCARD/usbprobe.txt` |
| Verified | With powered hub: FAT32, EXT4, NTFS sticks mount and read; other USB devices enumerate |
| Note | Type-C port **does not supply 5 V**; must use a self-powered USB hub |

### 1.3 Menu refactor (two-level menus; display and operation confirmed on device screen)
| Feature | Description |
|---|---|
| Main menu 7 items | USB Storage, ADB, USB Serial, USB Network (A → submenu), USB Host, Boot Defaults (A → submenu), Return to MinUI; row count adapts to screen height (1280x720@2x = 8 rows), scroll when needed |
| USB Network submenu | Mode (OFF / +Internet / Local only), Network Type (ECM/NCM/RNDIS), How to connect (scrollable Windows/macOS/Linux help: which type to pick, host IP, script location, commands, how to stop) |
| Boot Defaults submenu | Checkboxes: ADB, USB Storage (Off/RW/RO), USB Serial, USB Network + Network Type; serial and network mutually exclusive; changes save immediately. Replaces old Boot Default / Boot USB Network rows |
| Boot config 3-line format | Line 1 `adb_only|adb_ums_rw|adb_ums_ro|ums_rw|ums_ro|off`, line 2 `net|-`, line 3 `serial|-`; old 1–2 line format still supported; `tomato.m21` supports boot without ADB and with serial |
| USB Storage default read-only | Cycle order OFF → **RO** → RW → OFF; one Right press only reaches RO first; RW shows `ON (Read-Write, risky)` and status-bar warning that simultaneous PC/device writes to the SD card can corrupt the filesystem—prefer RO and use a card reader to copy files. Same in Boot Defaults USB Storage |
| Removed On Exit to MinUI | Leaving USB Mode always turns USB Storage off (avoids PC and device writing the same SD card) |
| Script location | Internet-sharing/diagnostic scripts live only under `Tools/m21/USB Mode.pak/host-tools/`, no longer copied to SD root; help pages show the path |

### 1.2 USB serial and USB network (device mode) — Windows support
| Feature | Description |
|---|---|
| USB Serial switched to standard CDC-ACM | New `f_acm_x.ko`; Windows inbox `usbser`, Mac/Linux driverless; device side `ttyGS0` (old `gser` was vendor class, no Windows driver) |
| Network Type selector | Choose `ECM / NCM / RNDIS` in USB Network / Boot Defaults submenus; saved in `.userdata/m21/usb_nettype`, applies at boot |
| ECM / NCM / RNDIS | New `f_ncm_x.ko`, `f_rndis_x.ko` (ECM existed); ECM=Mac/Linux, NCM=Mac/Linux/Win11, RNDIS=Windows 10/11 |
| Serial vs network mutex | UDC has one interrupt endpoint; enabling one auto-disables the other with a prompt; ADB + storage + network can still coexist |
| Device class EF/02/01 | When serial or network is on, set IAD composite class so Windows binds inbox drivers per function |
| Boot default network/serial | `usb_boot_mode.cfg` line 2 `net`, line 3 `serial` + `usb_nettype` (now via Boot Defaults submenu) |
| RNDIS Windows compatibility | RNDIS first in config + MS OS compatible ID descriptors + `bDataInterface` fix (see §2) |

### 1.3 Internet sharing (host NAT) scripts — three platforms
Device side sets default route via 192.168.7.2 and DNS automatically; the host must NAT:
| Platform | Script | Notes |
|---|---|---|
| macOS | `share-internet-macos.sh on/off/status` | pf anchor NAT, sudo required |
| Linux | `share-internet-linux.sh on/off` | iptables MASQUERADE, sudo required |
| Windows | `share-internet-windows.bat` (calls `.ps1`) `on/off/status` | Try `New-NetNat` first; if unavailable (common on Home, "invalid class" error) fall back to ICS (registry tweaks ICS internal address to 192.168.7.2) |

All four scripts are in the pak `host-tools/`; a copy was also placed at **SD card root** (easy to copy on a PC).

### 1.4 Diagnostics
- `host-tools/usb-diag.sh` (device) + `usb-diag-windows.bat`: gateway, ping each DNS, `nslookup`, large packets, system resolver; saves `usb-diag.txt` and copies to clipboard (on device under `Tools/m21/USB Mode.pak/host-tools/`).
- `dhcpd_mini` now logs DHCP packets sent/received to `/tmp/dhcpd.log`.
- Device UDC debug switches: write 1 to `/sys/devices/platform/soc@3000000/4100000.udc-controller/{queue_debug,irq_debug}`.

## 2. Issues, root causes, and fixes

| # | Symptom | Root cause | Fix / conclusion |
|---|---|---|---|
| 1 | Host mode reverts to device after leaving pak | State lived only in UI process | Background daemon + state file `/tmp/usbhost.state`; read `otg_role` on entry |
| 2 | After eject, stick stuck on "reading", must replug | Eject did SCSI `device/delete` | Eject is umount+sync+mark `ejected`, no device delete; remount from menu |
| 3 | ext4 shows unknown / magic `55534243` | That 58.5 GB three-partition stick returns corrupted reads on EHCI (CBW "USBC" in data); 4 KB request cap did not help | Hardware/transport, not ext4; ext4 TF card OK. Documented in `NOTES.md`; **do not write to that stick** |
| 4 | Windows "no driver" for serial/network | `gser` vendor class; no Windows ECM; device class not EF/02/01 | CDC-ACM; NCM/RNDIS; device class EF/02/01 |
| 5 | NCM/RNDIS kernel oops (`strlen` in `if_nlmsg_size`) | Vendor `struct net_device` larger than mainline (`netdev_priv` offset 1600 vs 1344); `struct eth_dev` overlapped tail of net_device; ECM OK (`wrap/unwrap` NULL) | Add `char sunxi_tail_pad[256]` at end of `net_device` in build-tree header (`kmod/netdevice_h_pad.patch`); **rebuild all network modules** |
| 6 | RNDIS class attributes read back 0 | configfs parsed with `%02hhx` | Write `e0`/`01`/`03`, not `0xe0` |
| 7 | After type switch, NIC becomes usb1/usb2 | Each registered network function owns a `usbN` | `usbnet.sh up` reads `functions/<type>/ifname`, cleans old `usbN` |
| 8 | Gadget changes from `adb shell` kill script mid-run | Unbinding UDC drops adb; stock `sleep` killed too | Script runs as `(trap "" HUP; sh script &)` in background, uses `usleep` |
| 9 | **Windows RNDIS code 10** (hardest) | Device `rndis_setup` never called once (Windows sent no requests after config). Fixes: ① RNDIS must be **first** function in config (ifaces 0/1); after ADB (ifaces 1/2) always fails; ② `f_rndis.c` hardcoded Call Management `bDataInterface` to 1; ③ E0/01/03 "Internet sharing device" driver failed here; switched to MS OS compatible ID path | On `usbnet.sh on rndis` / `tomato` RNDIS link, drop ffs/acm links first, restore after; `kmod/f_rndis.patch`; `os_desc` (`MSFT100`, `0xcd`, `RNDIS`/`5162001`) + `bcdDevice` 0x0511 (Windows caches MS OS presence by VID/PID/REV) |
| 10 | Windows gets 169.254 | Running `usbnet.sh up` directly from `adb shell` → `dhcpd_mini.elf` (glibc) "not found" on stock rootfs | Menu/boot run inside chroot, unaffected; test with `chroot /overlay /tmp/dhcpd_mini.elf ...` |
| 11 | Windows sharing script `New-NetNat` "invalid class" | Windows missing NetNat WMI class | Auto fallback to ICS (registry `ScopeAddress`=192.168.7.2) |
| 12 | Device: 8.8.8.8 OK, `www.baidu.com` hangs (Windows sharing) | **Not root-caused**. Mac path DNS OK; Windows path diag log interrupted by Ctrl+C (only gateway tested) | See §4 todos |

## 3. Compatibility / verification matrix
| Item | Mac | Windows | Notes |
|---|---|---|---|
| USB Storage | ✅ mount/read OK | ✅ mount/read OK | User tested |
| ADB | ✅ | ✅ (toggle OK) | ⚠ Occasional: after toggling USB Network / USB Host etc., adb fails until ADB off/on (see §4 item 9) |
| USB Host (stick/TF/ext4/hub) | — | — | ✅ Powered hub: FAT32, EXT4, NTFS sticks mount/read; other USB devices enumerate (user tested); Type-C no 5 V, self-powered hub required |
| USB network ECM | ✅ `ping www.baidu.com` OK | ❌ No inbox ECM on Windows (expected, untested) | Mac/Linux only |
| USB network NCM | ✅ DHCP + large ping; `ping www.baidu.com` OK | ✅ Enumerates (Win11); after sharing, `ping www.baidu.com` still problematic | |
| USB network RNDIS | ❌ (no Mac driver; enumeration/control only) | ✅ Enumerates (gets 192.168.7.2); after sharing, `ping www.baidu.com` still problematic | RNDIS must be first |
| USB serial CDC-ACM | ✅ bidirectional | ✅ Correct COM port, shell-like session (user tested) | Boot `adb_only`+`serial` reboot verified |
| Internet sharing | ✅ (pf) | 🟡 8.8.8.8 OK, DNS hangs, TBD | |
| Menu UI (submenus, fullscreen, help, mutex hints) | — | — | ✅ User confirmed display and submenus on device; runtime toggle/bind-failure fallback branches not all tested |

## 4. Open / todo
1. **DNS hang after Windows sharing**: With RNDIS/NCM + Windows sharing already `on`, run `usb-diag-windows.bat` to completion (no Ctrl+C, ~1 minute).
   **After switching network type Windows gets a new NIC; ICS binds the old one** — rerun `share-internet-windows.bat on`.
2. Mac path: `ping -s 1400 8.8.8.8` 100% loss (small packets OK), seen in `ECM` mode; may be Mac-side NAT/MTU or large-frame regression; TBD.
3. On-device https: `wget` http only; stock `curl` http/https both fail (no protocols at build time). Device tool limit, not USB network; ship TLS-enabled wget/curl in pak if needed.
4. Menu: display and submenus confirmed; not fully tested: boot with ADB off combos, boot mode `off`, auto-rollback on bind failure.
5. Windows ICS fallback vs `New-NetNat` paths: scripts not fully verified on real hardware (ICS path reaches 8.8.8.8).
6. 58.5 GB three-partition stick transfer errors (may need VID:PID quirks; not tried).
7. `uh_tune_disk` capping `max_sectors_kb` to 4 ineffective and slower on that stick; can remove.
8. Current device state: `usb_boot_mode.cfg`=`adb_only`+`net`, `usb_nettype`=`ncm`. Debug files and share/diag copies at SD root cleaned (scripts only in pak `host-tools`).
9. **ADB occasional failure**: After toggling USB Network, USB Host, etc., adb sometimes disconnects; fix by ADB off/on in menu. Not debugged; suspect `apply_usb_config()` UDC unbind/rebind does not reset adbd FunctionFS endpoint state (hypothesis, unverified). On repro: `cat /sys/kernel/config/usb_gadget/g1/UDC`, `ls configs/c.1`, `ps | grep adbd`, `dmesg | tail` on device.
10. USB Host other devices (keyboard/gamepad): enumeration only; function not verified.

## 5. Related commits (`fork/main`)
- USB Host: `4f39fb3` persistence · `f8d379e` A-button menu · `519994a` non-destructive eject · `74ef8f7` device details · `33573a8` ext2/3/4 · `3f31bb9` unknown FS diagnostics · `791087a` read tuning
- Windows support: `a7d9a65` ACM/NCM/RNDIS + net_device padding + device class · `123fcb9` RNDIS first + MS OS + descriptor fixes
- Sharing & diagnostics: `d2ed7ce` Windows sharing script · `00da38b` ICS fallback · `b470884` diagnostics · `9e753c6` this document
- Menu refactor: `48ff1b8` submenus/help/boot 3-line cfg · `4dfe4c8` rows vs screen height · `3faa59d` remove On Exit · `12bf099` scripts only in pak `host-tools`


## Appendix: 2026-10-21 TF card damage analysis

- Card: Samsung (manfid `0x1b`), model EB2MW, 2017-06, 29.8 GiB, capacity matches genuine 32 GB; this boot kernel log had no MMC/I/O errors but `FAT-fs: Volume was not properly unmounted`. **No full-card read/write test done.**
- Inferred cause (**unconfirmed**): Device had SD mounted RW while USB Storage exported the same block device to the PC with no mutual exclusion; both could write the same FAT. Before damage the card had Windows/macOS `.Spotlight-V100`, `.fseventsd`, `System Volume Information`, so the PC did write during export. Power is a slide switch; abrupt power-off while SD still mounted RW.
- Mitigations: USB Storage defaults RO with RW warning (this change); use a card reader for file transfer; after "not properly unmounted" run `fsck_msdos` on Mac; back up saves/config regularly; card ~9 years old—back up and replace.
