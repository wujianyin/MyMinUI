# MyMinUI on the SJGAM M21 / M22Pro (`m21` platform): status and known problems

> 中文版 (the Chinese original, same section numbers): [M21_STATUS.zh-CN.md](M21_STATUS.zh-CN.md) | Back to [README.md](README.md)

Written 2026-10-20, updated 2026-10-25. Purpose: before improving anything, find out what upstream did, what it did not do, and where the traps are.

## 0. Sources and how far to trust them

| Source | Content | Remark |
|---|---|---|
| `README.upstream.md` (987 lines) | All release notes by the author Turro75 (2024-10 to 2026-08) | Same as the GitHub Releases page; the author's word only, not verified by us |
| GitHub Releases (40) | Which releases carried an m21 package | Attachments checked one by one |
| Commit history | About 246 non-fork commits touching `workspace/m21` | Read in order |
| GitHub issues / PRs | m21/m22pro related issues #28 #38 #48 #51 #63 #71 #74 #75 #77 #79 #80, PRs #111-#116 | Bodies and comments read |
| Repository code and docs | `workspace/m21/*`, `skeleton/SYSTEM/m21`, `INSTALL.md`, `workspace/m21/other/dtb_60fps/Readme.md`, `firmware/sjgam-m22pro/` | Read |
| Our own device (M22Pro) | Versions and display parameters read on the device | Marked "measured" |

Tags: **[author]** the author's statement in release notes/issues; **[measured]** seen on our M22Pro; **[code]** derived from the repository code; **[inferred]** my judgement, not directly proven.

## 1. Platform overview

`m21` is the platform directory shared by two SJGAM handhelds: the **SJGAM M21** (3.5" 640x480 landscape) and the **SJGAM M22Pro** (1080x1920 portrait panel, used sideways). A third device, the LOONG MAY-M21 (H133), belongs to a PR author.

- SoC [author]: Allwinner H133 (sun8iw20), dual Cortex-A7 up to 1.2 GHz, **no GPU**; the "N909" of the marketing is this chip. 256 MB RAM [our firmware analysis].
- System [author][measured]: the stock Tina Linux (musl) is read-only and the 16 MiB SPI NOR is full. The author's approach: the stock hook `/usr/tomato` (= `tomato` in the SD card root) mounts `rootfs.ext2` (glibc) from the card to `/overlay` with fuse2fs and `chroot`s into it to run MinUI. All MinUI binaries are built for glibc.
- Display [code][measured]:
  - The panel accepts RGB8888 only, and the top byte of every pixel must be 0xff to be visible [author]; SDL flip/render is unusable, so the author wrote a framebuffer + display-layer renderer (`/dev/disp` + ION, `sunxi_display2.h`) in `workspace/m21/platform/platform.c`.
  - The M22Pro panel is physically 1080x1920. The program draws 1280x720 (`DEVICE_WIDTH/HEIGHT`), rotates by 90 degrees and writes 720x1280 into the layer; the display engine scales it to the panel. Measured: `fb[1280,1280] crop[0,0,720,1280] frame[0,0,1080,1920]`.
  - M21 and M22 are told apart by the file `/mnt/SDCARD/m21/thisism22` [code]; it exists (2 bytes) on our device.
  - Internal panel refresh: the stock M22Pro device tree gives only **38 fps**, the M21 about 50 fps [author]; HDMI reaches 60 fps.
- Input [author]:
  - The M22Pro has no Menu key; **Select+Start** emulates it. There is no power key, only a physical power switch; holding Select+Start for 2 s performs the "shutdown" (software can only shut down to the point where you may cut power).
  - The face buttons are BAYX; the code swaps A/B and X/Y to ABYX like other Miyoo/Anbernic devices. Edit `.system/m21/custombuttonmapping.env` to remap [author][code].
  - The stick is **digital, wired to buttons**: no analog values, cannot be remapped (issue #77).
- Time/power [author]: no RTC; the system time is saved at "soft shutdown" and restored at boot (not saved when you use the switch). Sleep = black screen, Select wakes, "shutdown" after 2 minutes (you still flip the switch). CPU steps 480/720/912/1008/1104/1200 MHz [code].
- Install [code]: SJGAM section of `INSTALL.md`: FAT32 card, copy the `m21/` directory of the release, copy `m21/emulationstation` and `m21/tomato` to the root, keep the zip in the root; first boot installs automatically (it creates swap, **be patient**).

### 1.1 Hardware and display chain (measured with adb and the firmware device tree, 2026-10-22)

| Item | Result | Evidence |
|---|---|---|
| SoC | Allwinner H133 (sun8iw20, same family as R528/T113) | `/proc/device-tree/model` = `sun8iw20`, compatible `allwinner,h133`, `arm,sun8iw20p1` |
| CPU | Dual Cortex-A7, NEON, VFPv4; 480/720/912/1008/1104/1200 MHz | `/proc/cpuinfo`, `cpufreq/scaling_available_frequencies` |
| Memory | About 234 MB usable (`MemTotal` 239908 kB) | `/proc/meminfo` |
| GPU | **None** | No gpu/mali/powervr/sgx node in the firmware dts or the running device tree; no dri/mali/pvr/kgsl in `/dev`; no drm or devfreq in `/sys/class`; no GPU in dmesg; only libdrm in the overlay, no Mali/EGL/GLES libraries |
| Graphics related hardware present | G2D (`g2d@5410000`, `/dev/g2d`, 2D acceleration), display engine DE (`disp@5000000`, `/dev/disp`), video codec Cedar (`ve@1c0e000`), deinterlace, CE crypto, camera input vind | Device tree nodes and `/dev` |

Display chain [code][inferred]: the core or MinUI draws a frame with the CPU (NEON) into ION physically contiguous memory, the CPU rotates it by 90 degrees, `pan_display` on `/dev/disp` lets the DE read that layer, the DE scales to 1080x1920 and sends it over MIPI-DSI.
The DE is a fixed-function scan-out and composition unit, not a GPU; what is drawn is entirely up to the CPU. `pan_display` blocks about 6 ms waiting for vblank, so the 60 Hz patch (panel timing and DSI rate) and Prevent Tearing both act here. The `colorbar` attribute is a DE test pattern that does not involve the CPU.
Consequence: cores that need 3D (PSP, DC ...) are out of reach; the available accelerations are NEON and, not yet evaluated, G2D for rotate/scale (see section 6).

## 2. m21 timeline

| Date | Event |
|---|---|
| 2024-10-08 | First M21 support. **Not working** then: NDS, Amiga, retroarch, external gamepad, HDMI. Special handling: double-press Menu to sleep, hold Menu for 3 s to power off |
| 2024-11-04/05 | External gamepad (SJGAM private protocol only, one pad), HDMI output with audio, mute at minimum volume, button layout changes, CORE_THREAD_VIDEO (core thread split, PS1 BR2 about 56 fps), 32bpp pixel format |
| 2024-12-01 | Auto-rotate core output (vertical arcade games), m21 first |
| 2024-12-15 | The author's M21 broke, **releases stopped** |
| 2025-01-27 | m21 build re-enabled, M22Pro added (HDMI + one external pad) |
| 2025-04-02 | Global double buffering + vsync rework; m21 fell behind again (device broken) |
| 2025-04-13 / 04-30 | `sjgam_m21_m22` and `SJGAM-M21-M22p` releases; tearing and rotation fixed with user testing |
| 2025-05-25 | **m21/m22 fully supported again**: display-layer double buffering, Select+Start menu/power-off, sleep logic, default 1280x720, `custom_hdmi_settings.txt`; vsync disabled because of the fixed 50/38 fps |
| 2025-08-03 to 2025-12-28 | Unified render path, flicker fixed (#71, fixed in the 12-23 release); 2025-12-29 core git versions pinned |
| 2026-01-09 | Display driver polish, boot colour bar, custom button mapping (#74), hardware vsync on HDMI |
| 2026-01-18 | Device tree changes for 60 fps on the M22Pro documented (`dtb_60fps`) |
| 2026-01-26 | Install/update improvements, install screen rotated with the screen |
| 2026-02-04 / 02-08 | The 02-04 release gives a black screen on the M21 (#79, the driver was tuned for M22, wrong init values for M21); 02-08 fixes it. **The last official release with an m21 package** |
| 2026-05-25 to 2026-08-05 | Five official releases, **none with an m21 package** (see 4.1) |
| 2026-06-06 | The author announces the M22Pro broke too (no display after a drop): **m21 is no longer supported or released** |
| 2026-08-07 to 08-08 | Community user WladmirDotNet (owns a LOONG MAY-M21) merges: PR #114 fixes the m21 build (bit-rot), #116 enables sharp scaling on m21 with an ARGB8888 layer, #115 red channel fix, #111 out-of-bounds write |
| 2026-08-10 | Author commit `72ed4f3` puts `m21` back in the default platform list. **No new official release up to today** (latest is 2026-08-05, without m21) |

## 3. Feature status

Version running on our device: **MyMinUI-20260208b-0** (`.system/version.txt`, commit `2c7a860`) [measured]; since 2026-10-24/25 we run our own build of the current upstream HEAD (see 6.1/6.2).

### 3.1 "Works" according to the author

| Feature | Status | Remark |
|---|---|---|
| Launcher / in-game menu / save-state preview / box art | Works [author] | The UI comes up on our device [measured] |
| 31 systems (Emus paks) | Works [code][author] | A2600/5200/7800, DOOM, DOS, FBN, FC, GB/GBA/GBC, GG, MAME, MD, NG, NGCD, NGP/NGPC, PCE, PKM, PS, PUAE (Amiga), QUAKE, SFC, SGB, SMS, SUPA, VB, WS, ZXS, P8/P8N (native Pico-8) |
| Rendering | Display-layer double buffering [author] | No vsync on the internal panel (see 4.2) |
| Scaling | nearest; sharp (PR #116, **after 02-08, unreleased**) | We removed sharp on m21 (see 6.1) |
| HDMI | Works, default 1280x720p60 [author] | Edit `.system/m21/custom_hdmi_settings.txt` (854x480p60 is faster) |
| External gamepad | SJGAM private pad only, one [author] | #63 (two pads on M22) unresolved |
| Auto-rotate vertical arcade games | Works [author] | |
| Sleep / power off | Works but special [author] | See section 1 |
| Debug HUD (CPU, fps, temperature, battery) | Works [author] | |
| Cheats | Works [author] | Since 2025-12-16 |
| Button remap | Edit `custombuttonmapping.env` [author] | No UI |

### 3.2 Explicitly not working or not done

| Feature | Status | Source |
|---|---|---|
| NDS emulation | Not supported | [author] first release notes; NDS only for miyoomini/a30 |
| retroarch | Does not work | [author] first release notes |
| External pad for player 2 | Not supported | issue #63, an old `testmultiplayer` branch exists but was never merged |
| Analog stick | Hardware limit | #77 |
| vsync / 60 fps on the internal panel | Stock device tree limit | See 4.2 |
| Software power on/off | Hardware limit (physical switch only) | [author] |
| Menu key on M22 | None; Select+Start | [author] |

## 4. Known problems and risks

### 4.1 Maintenance state (the biggest risk)

1. **Nobody holds an M21/M22Pro for main testing.** The author's M21 and M22Pro broke one after the other [author]. The only community tester owns a LOONG MAY-M21 (not an M22Pro).
2. **The last m21 official package is 20260208b-0.** Upstream then changed much global code (new audio engine, new scaler, sleep mode, resetting brightness/volume on update, the `PLAT_getVsyncInterval` interface ...), none of it ever verified on m21 by the author:
   - PR #114 (2026-08) shows m21 once did not even **compile** (`SHARPNESS_SOFT` removed, `PLAT_getVsyncInterval` missing);
   - the 2026-05-25 note "drop nextui audio fix, use the new audio engine" has no statement about m21 [inferred: needs verification].
3. **The toolchain source is gone.** `makefile.toolchain` clones `shauninman/union-m21-toolchain`, which no longer exists (PR #114 author). Our fork builds without it: `workspace/m21/other/dockerbuild/`.
4. The m21 build flow (`make m21` = `clean setup common package`) differs from h700/r36s (`build system cores package`): **core `.so` files are not in git** (`**/cores/output` is git-ignored); our local `workspace/m21/cores/output` has 24 `.so` of unverified origin [code]. A clean clone cannot produce a complete release by `make m21`; cores must be built separately or taken from a release [inferred]. Our `package.sh` takes them from the 20260208b release.

### 4.2 Display and tearing

- The stock M22Pro device tree sets the refresh rate to **38 fps** (M21 about 50 fps). With vsync on, the frame rate is dragged down to that value, so **vsync is forced off on the internal panel** [author]; only double buffering reduces tearing, and **tearing may still be visible**.
- Our device [measured]: `ref_fps:38` originally, i.e. the stock device tree.
- The author documented how to reach 60 fps by editing the device tree (`workspace/m21/other/dtb_60fps/Readme.md`): extract `mtdblock0`, change the timing of the `lcd0` node, recompile and `dd` it back to `/dev/mtdblock0`. **Only valid for the M22Pro; flashing it to an M21 bricks it**; writing the SPI NOR is risky in itself. The boot script detects the patch with `fps:38` (`NEWDTB` environment variable). **The release packages contain no flashing tool**; only the manual steps and the detection logic exist.
  On 2026-10-20 we verified read-only on the device: `mtdblock0` has the same SHA256 as the repository image, the panel is hx8399, the running device tree timing matches the assumption (see [RISK_ASSESSMENT.md](workspace/m21/other/dtb_60fps/RISK_ASSESSMENT.md)). FEL works: `reboot efex`, `sunxi-fel version` reports R528; stock `sunxi-fel` cannot do R528 SPI, our patched one (`sunxi-fel-r528-spi.patch`) reads and writes the whole 16 MiB SPI NOR.
  **On 2026-10-20 the 60 Hz patch was flashed on our own device and verified: `ref_fps:60`, `fps:60.2`, normal boot.**
- Issue #80 (M22Pro, NES tearing/sound): the author suggests turning off the tearing option on the internal panel and choosing `Core` or `NoFix` for the sync reference; users report `Core` is best [author].
- Issue #71: flashes when pressing action buttons, related to cheat initialisation, fixed in the 2025-12-23 release.

### 4.3 Device differences (M21 vs M22)

- The render driver was tuned for **M22 parameters**; M21 had a black screen (#79, fixed 2026-02-08) and mirrored menus/misplaced buttons [author]. With only M22 information at hand the **regression risk on the M21 is higher**.
- They are told apart by the `thisism22` file: it must be placed correctly or rotation/resolution will be wrong.
- HDMI is not rotated, the internal panel is rotated by 90 degrees; the code handles them separately (also native Pico-8) [author].

### 4.4 Input and power

- Digital stick: no analog support (#77).
- On the M22, Select+Start is both the stock "switch 1P/2P" (stock firmware) and MyMinUI's menu shortcut, which conflicts with a second pad (#63).
- No software power on/off; flip the switch after "shutdown". The system time is saved only on a "soft shutdown".

### 4.5 Install and upgrade

- The release must keep its `MyMinUI-*-m21.zip` name; do not rename it to `MinUI.zip` (#51, several users stumbled); SD card compatibility problems also occurred.
- Upgrades overwrite most files, but `custombuttonmapping.env` is no longer overwritten (since 2026-02-07) [author].
- Since 2026-06-14 every update resets brightness/volume [author] (not verified for m21; our `boot.sh` merge handles it, see 6.2).

### 4.6 Build

- Check that m21 still builds in `PLATFORMS`/the toolchain; PR #114 needs `-lsamplerate` (added to `makefile.env`).
- The fbneo and mame32 cores need much memory; with small Docker memory `lto1` gets killed (#75); the author needs 6 GB RAM and 4 GB swap on a Mac mini and sometimes reruns.
- glibc binaries only run in the chroot; to run them in the stock system you need the musl toolchain (#48).

## 5. Problems we found that are unrelated to upstream

Found while doing the USB work (details in `workspace/m21/other/usbmode/FEATURES_AND_ISSUES.md`):

- USB Mode.pak (storage, ADB, serial, network, host, boot defaults) is ours; upstream has nothing like it.
- `curl` has no protocols and `wget` has no https: stock tool limits.
- ADB occasionally stops after toggling other USB switches; turn it off and on in USB Mode (root cause not found).
- After sharing the Internet from Windows, name resolution hangs (IP works; `ping www.baidu.com` has a problem).
- A hard power cut can leave the SD card with FAT errors and the kernel remounts it read-only; MinUI then sticks on the colour bar. Repair: [SD_RESTORE.md](SD_RESTORE.md) section B.

## 6. Candidate improvements (suggestions, not evaluated)

Whether to do them and in which order is your decision:

| Direction | Value | Difficulty/risk | Basis |
|---|---|---|---|
| Merge the upstream commits after 20260208 (new audio engine, sharp scaling, sleep improvements, PRs #111-#116) and verify each on the M22Pro | High: our version was far behind and m21 once did not build | Medium: audio, scaling and sleep need verification | 4.1 (audited in 6.1, mostly done) |
| Verify and fix the new scaler's sharp mode on the M22Pro | Medium: picture quality | Medium: PR #116 was tested on a LOONG MAY-M21 | 2 |
| 60 fps on the internal panel by device tree patch | High: solves tearing/frame cap, vsync possible | **High**: writes the SPI NOR (brick risk), M22Pro only; offline assessment and a more conservative patch script (writes 2 sectors) exist, see [RISK_ASSESSMENT.md](workspace/m21/other/dtb_60fps/RISK_ASSESSMENT.md) | 4.2 (done on our device) |
| External pad for player 2 (#63) | Medium | Medium: an old branch exists, the Select+Start conflict needs solving | 4.4 |
| Button mapping UI (today only the env file) | Low to medium | Low | 3.1 |
| Fix the build chain: toolchain source, README, `make m21` flow | Medium: reproducibility | Low | 4.1 (done by `dockerbuild/`) |
| Ship our USB Mode.pak with the release package | Medium | Low | 5 (done) |
| Investigate the ADB flake and the Windows DNS problem | Medium (our own feature) | Low to medium | 5 |

## 6.1 Audit of the upstream commits after 20260208 (2c7a860..HEAD, about 160), 2026-10-24

Tested on the SJGAM M22Pro with adb on GBA, and minarch/api/utils/platform/minui were compiled with strict warnings (`-Wall -Warray-bounds -Wrestrict -Wformat-overflow` ...).

**Found and fixed (all committed):**

| Problem | Origin | Symptom/risk | Fix |
|---|---|---|---|
| Display layer changed to ARGB8888 with 32-bit writes in every mode | `c5d07f5` (PR #116, tested on a LOONG MAY-M21) | GBA at only 30 fps with crackling audio on the M22Pro | Layer always RGB565, Sharp option removed (`NO_SHARP_SCALER`) |
| HUD frequency always 0 | `a[9]` overflow in `getCPUusage()` | With gcc 12 it zeroes `cur_cpu_freq` | `a[10]` |
| HUD font bigger and blurry | `e114217` picks the font by the largest picture side | The 720x1280 picture is also scaled 1.5x by the display engine | `HUD_FONT_MID_MAX 1280` keeps 10x16 |
| Brightness/volume not saved | Old m21 `SaveSettings()` bug | Defaults at every boot | `O_CREAT` + write the struct + validate on load |
| `sprintf` with overlapping source and destination in `getCPUusage()` | Upstream | Undefined behaviour | `snprintf` appending |
| Missing `CPU_SPEED_SLEEP` environment variable gives a null pointer | m21 platform code (added with the sleep improvements) | An old `launch.sh` with a new binary crashes | Fall back to 480000 |
| `SND_init(0,0)` on audio that was never initialised after wake | `PWR_exitSleep()` | After MinUI's main-screen auto sleep, on all platforms | Re-init only if `sample_rate_bak>0` |
| `PLAT_shouldWake()` has no return when the input device cannot be opened | m21 platform code | Undefined behaviour | `return 0` |
| `OptionQuicksave_onConfirm()` has no return | Upstream | Undefined behaviour (in practice `PWR_powerOff()` does not return) | return added |

**Checked, no change needed:**
- Saves renamed from `.sav` to `.srm` (`292ee24`): old `.sav` files are renamed when a game opens. Note that going back to an old minarch after the rename will not find the saves.
- `msettings.bin` deleted on update (`66d5cce`): already in the m21 `boot.sh`; safe together with our version validation.
- Auto sleep after 30 s and shutdown 120 s after sleep: not triggered while charging (including USB power / adb attached), only on battery; older versions behaved similarly.
- `5f85ed8` (shutdown crash) only touches the h700/miyoomini/my282/r36s platform files; the m21 `PLAT_quitVideo` has no such problem.
- libsamplerate: `6ed7394` claims to remove it, but m21 still links `-lsamplerate` and the device has the library, so nothing changes.

**Not verified, still a risk:**
- **Auto sleep/wake on battery** cannot be tested over adb (adb attached means charging).
- **Other cores:** the card only has GBA and PICO-8 games; other cores (FC/SFC/MD/PS ...) never ran the new minarch; the core `.so` files and Commander still come from the old package.
- **ZXS (fuse core)** has no m21 build yet and is not in the release package.

**Latent upstream bugs (also fixed afterwards):**
- `Core_updateAVInfo()` did `memcpy(&av_info,&av_infonew,...)` with the address of the pointer when `av_infonew!=NULL`; now `*av_infonew` (all callers pass NULL today).
- The rotation-3 branch of `rotateIMGScalar()` lacked the `y` loop and used an uninitialised `y` (only the scalar fallback path); loop added.
- The 8-byte buffer of `putInt()` cannot hold an 11-digit int; now 16.
- Many `sprintf` calls write into 256/512 byte path buffers and can overflow with long paths (64 places in the strict warnings). The m21 `platform.h` defines `sprintf` as an `snprintf` bounded by the destination array size (unbounded when the destination is a pointer of unknown size, as before). Such warnings are now 0. The macro only affects the m21 build.

## 6.2 Our changes on 2026-10-25

All committed and deployed on our M22Pro; details in the linked documents.

| Change | What | Document |
|---|---|---|
| CJK fonts in the package | `package.sh` overlays `BPreplayBold-unhinted.otf` and `Files.pak/res/FreeSans.ttf` | [cjkfont README](workspace/m21/other/cjkfont/README.md) |
| `tomato` with USB boot defaults, merged onto the current `boot.sh` | Update no longer overwrites the running `tomato`, msettings reset on update, `-x tomato emulationstation` in the unzip; a wrong `tomato` on the card had disabled adb at boot | [README.md](README.md) (3b), [SD_RESTORE.md](SD_RESTORE.md) |
| Switchable UI language | `TR()` + catalogs, first-start picker, `Tools/Language.pak`, per-pak catalogs, zh_CN for everything; the four `Toggle*` tools show their value | [I18N.md](I18N.md) |
| Read-only SD card repair | Static `fsck.fat` build script, steps, what was found | [SD_RESTORE.md](SD_RESTORE.md) section B |

Still unverified on the device: the minarch in-game menu in Chinese on every core, and every USB Mode page in Chinese (the main pages were seen).

## 7. References

- Upstream: <https://github.com/Turro75/MyMinUI>; releases: <https://github.com/Turro75/MyMinUI/releases>
- Issues: [#63](https://github.com/Turro75/MyMinUI/issues/63) [#71](https://github.com/Turro75/MyMinUI/issues/71) [#74](https://github.com/Turro75/MyMinUI/issues/74) [#75](https://github.com/Turro75/MyMinUI/issues/75) [#77](https://github.com/Turro75/MyMinUI/issues/77) [#79](https://github.com/Turro75/MyMinUI/issues/79) [#80](https://github.com/Turro75/MyMinUI/issues/80) [#48](https://github.com/Turro75/MyMinUI/issues/48) [#51](https://github.com/Turro75/MyMinUI/issues/51) [#38](https://github.com/Turro75/MyMinUI/issues/38)
- PRs: [#114](https://github.com/Turro75/MyMinUI/pull/114) [#115](https://github.com/Turro75/MyMinUI/pull/115) [#116](https://github.com/Turro75/MyMinUI/pull/116) [#111](https://github.com/Turro75/MyMinUI/pull/111)
- Local material: `INSTALL.md` (SJGAM section), `workspace/m21/`, `workspace/m21/other/dtb_60fps/Readme.md`, `firmware/sjgam-m22pro/`, `workspace/m21/other/usbmode/`
