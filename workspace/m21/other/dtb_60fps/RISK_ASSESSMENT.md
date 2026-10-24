# M22Pro Built-in Display 60 fps (Device-Tree Patch) Risk Assessment

> 中文版: [RISK_ASSESSMENT.zh-CN.md](RISK_ASSESSMENT.zh-CN.md)

Date: 2026-10-20. **This document is assessment only; nothing has been written to the device.**
For the upstream approach see [Readme.md](Readme.md) in this directory (author Turro75; valid only for M22Pro; will brick M21).

## 1. Problem Being Addressed

The M22Pro built-in panel (hx8399, 1080×1920 MIPI-DSI) runs at **37.6 Hz** in the factory device tree (`ref_fps:38`). Consequences [author + code `platform.c:1292`]:
- The built-in display cannot enable vsync (doing so caps refresh at 38 fps); only double buffering mitigates tearing, which still occurs;
- Games are limited to ~38 fps refresh (60 fps games drop frames on display).

The patch changes LCD timing to ~59.5 Hz. `launch.sh` checks whether `fps:38` is still present in `sys` to decide if the patch is applied and sets `NEWDTB`; `platform.c` then enables vsync on the built-in panel accordingly—**no application changes required**.

## 2. Facts We Verified (Offline, Based on `firmware/sjgam-m22pro/images/mtd0_uboot.bin`)

| Fact | Conclusion |
|---|---|
| Our device’s hx8399 DTB `lcd0@5461000` node | **Identical** to upstream `orig.dts` (same firmware build); patch values apply directly |
| Device-tree location | mtd0 offset `0xd6000`, 60608 bytes; mtd0 also holds a st7703s (640×480) DTB at `0x862b8`, **left unchanged** |
| mtd0 layout | `0x0` boot0 SPL (28 KiB, eGON.BT0); `0x10000` `sunxi-package` (u-boot, optee, dtb entries; effective length `0xd8000`) |
| **Package header has additive checksum** | Header `add_sum=0x8bb7e81c`; we recomputed with the algorithm (that field counted as stamp `0x5F0A6C39` during summation) and got an **exact match**, indicating the checksum covers the whole package (including the DTB) |
| Timing math | Factory 90 MHz / (1230×1945) = **37.62 Hz**; patch 162 MHz / (1350×2016) = **59.52 Hz** |
| DSI rate (4 lane, RGB888) | Factory 540 Mbit/s/lane → patch **972 Mbit/s/lane** (nearly doubled) |
| Flash erase granularity | 4 KiB (`/proc/mtd` erasesize=0x1000) |

### Issues With the Upstream `dd` Approach (Our Judgment)
Upstream flow: recompile the full DTS with `dtc` (58503 bytes, 2105 bytes smaller than the original 60608), `dd seek=876544 conv=notrunc` write-back, then rewrite the **entire 1 MiB** mtdblock0. That implies:
1. The DTB is shorter; 2105 bytes of old data remain after it;
2. The package header checksum is **not updated** (package contents changed, checksum no longer matches);
3. All 256 sectors are rewritten, including boot0 and U-Boot, unnecessarily widening exposure.

It booted on the author’s device, so boot0/U-Boot **does not actually enforce** this additive checksum [inferred, because the author could boot]; there is no source evidence, so it cannot be treated as a guarantee.

## 3. Our More Conservative Patch Method

Script: [`patch_mtd0_60fps.py`](patch_mtd0_60fps.py) (read-only dump, writes new files only, **does not flash**).

- **In-place patch of seven 4-byte timing properties** in the original DTB binary (`lcd_dclk_freq`, `lcd_hbp`, `lcd_ht`, `lcd_hspw`, `lcd_vbp`, `lcd_vt`, `lcd_vspw`), without `dtc`; DTB size and layout unchanged;
- Write a **compensation word** in the zero-padded gap after the DTB so the package checksum stays the same; **no byte in the package header sector needs changing**;
- Offline verification: only 10 bytes differ file-wide; **boot0 region (0–0x7000) unchanged**; checksum recomputation passes; `--restore` round-trip is **byte-identical** to the original dump;
- **Only two 4 KiB sectors need rewriting**: `0xde000` (timing) and `0xe4000` (compensation word). Upstream rewrites 256 sectors.
- Optional `--fix-header`: update the header checksum field instead; requires one extra sector at `0x10000` (power loss while writing the header sector is the worst case, so not the default).

## 4. Risk Register

| # | Risk | Impact | Likelihood / notes | Mitigation |
|---|---|---|---|---|
| 1 | Power loss / crash while writing flash | Corrupted DTB sector → U-Boot may not boot normally; **device may not start** | Short window (two 4 KiB sectors); upstream writes the full 1 MiB, much larger window | Full battery + charger; write only 2 sectors; read back and compare after write; confirm backup before writing |
| 2 | Panel rejects 972 Mbit/s DSI rate or timing out of spec | **Black / garbled screen** after boot (system may still boot) | **Not verified** hx8399 max lane rate; upstream author lit panel and measured 60.2 Hz on his M22Pro, but only one sample; panel batches may differ | If screen fails, **adb recovery** (see §5); keep backup |
| 3 | Display engine underrun / errors at higher refresh | Occasional artifacts, tearing, stutter | Upstream `sys` showed `err:277` (60 Hz sample) vs our factory `err:83`; runtimes differ, **needs measurement** | After flash, watch `err`/`skip` growth in `cat /sys/class/disp/disp/attr/sys` |
| 4 | Power / thermals | Shorter battery life | Pixel clock doubled; panel and DSI power rise; **no data** | Observe drain rate after flash |
| 5 | Compensation word in package gap if boot0/U-Boot uses that region | Could affect boot in theory | Region is zero-filled, after DTB, outside dtb entry length [inferred]; upstream also changed post-DTB area | Can use `--fix-header` (writes header sector) |
| 6 | Flashing M21 by mistake | **Brick** | Upstream warns explicitly | Script only patches DTB containing `hx8399`; exits if not found; confirm hx8399 on device (`lcd_driver_name`) |
| 7 | No recovery after flash | Hardware recovery needed | If boot0/U-Boot cannot start, Allwinner BROM usually enters **FEL (USB flash) mode** [inferred, **not verified on this device**; physical FEL entry unknown] | Until FEL entry is confirmed on device, treat “won’t boot” as non-recoverable |

## 5. Recovery Paths

1. **Boots but display abnormal** (risks 2, 3): System still runs; `tomato` starts adbd; **adb should still connect**. Restore original bytes to sectors `0xde000` and `0xe4000` with the recovery image (`--restore` output is byte-identical to original dump); reboot.
2. **Won’t boot** (risks 1, 5): FEL or programmer only. Unverified—**do not rely on it**.
3. Factory dump backup: `firmware/sjgam-m22pro/images/` (with `SHA256SUMS`) and Desktop backup.

## 6. Pre-Flash Checks (If You Proceed)

1. Read current `/dev/mtdblock0` on device; SHA256 **must match** `mtd0_uboot.bin` (proves backup matches live flash).
2. Confirm hx8399 in device tree (`cat /proc/device-tree/soc@3000000/lcd0@5461000/lcd_driver_name`).
3. Charge > 50% and plug in power; stable adb.
4. Generate patch image first; check script output (“package checksum verified”, “header sector changed: False”, “boot0 area changed: False”).
5. Write only sectors `0xde000` and `0xe4000`; read back and `cmp` immediately; do not `dd` the full 1 MiB.
6. Copy restore sectors (original values) to SD card and PC; reboot only after adb reconnects.
7. After reboot: `cat /sys/class/disp/disp/attr/sys` should show `ref_fps:60`; `launch.sh` sets `NEWDTB=1`.

## 7. Assessment Conclusion (For Decision-Making)

- **Benefit**: Built-in panel 38 Hz → ~60 Hz; vsync possible, most tearing gone, 60 fps games can display at full rate. No app changes.
- **Improvements vs upstream**: In-place 7-field patch, 2 sectors written, header and boot0 untouched, round-trip restore; verified offline.
- **Remaining risks**: (1) power-loss while writing flash; (2) whether panel accepts 972 Mbit/s (no data, one author success); (3) no verified hardware recovery if flash is bad.
- **No known way to try without flashing flash**: Sunxi display driver LCD timing comes from device tree; we found no runtime tuning path [inferred, device was disconnected, not yet checked under `/sys/class/disp/`].
- Recommendation: **Run read-only checks §6 items 1–2 on device** and confirm FEL entry before deciding to flash.


## Addendum 1: Upstream Releases Include No Flash Tools (Verified 2026-10-20)

Downloaded and inspected m21 release packages `20260118b-0`, `20260126b-0`, `20260208b-0`, and full git history:

- No dtb/mtd/flash files or scripts in packages; `tomato`/`emulationstation` only have commented `#export NEWDTB=0`.
- Upstream provides only: **manual steps** in `dtb_60fps/Readme.md`, and **detection logic** in commit `1ec9f1b` (`launch.sh` reads `/sys/class/disp/disp/attr/sys`, sets `NEWDTB=1` when `fps:38` absent).
- Readme line “modified tomato script ... can work even as a tool” is the author’s private build, not shipped.

So there is no ready-made, validated SPI flash tool; you must build your own—`patch_mtd0_60fps.py` in this directory is for that.

## Addendum 2: Device Read-Only Verification (2026-10-20, No Writes)

| Item | Result |
|---|---|
| `/dev/mtdblock0` SHA256 | `852d4d22…c90aa7`, **identical** to `firmware/sjgam-m22pro/images/mtd0_uboot.bin` |
| Partitions | mtd0 `uboot` 1 MiB, erase 4 KiB; mtd1 env 128 KiB; mtd2 boot 4 MiB; mtd4 rootfs |
| Panel driver | dmesg: `hx8399 lcd_panel_init is ok`, hx8399 confirmed |
| Current refresh | `ref_fps:38`, measured `fps:37.8`, DE clock 300 MHz, `err:53` |
| Runtime DT `lcd0@5461000` | dclk=0x5a (**90 MHz**), ht=1230, vt=1945, 1080×1920, 4 lane, hbp=32, hspw=8, vbp=7, vspw=6; matches flash DTB and patch script assumptions |
| Runtime timing API | `/sys/class/disp/disp/attr/` has `sys xres yres colorbar enhance_* runtime_enable ...`, **no LCD timing knobs**, `runtime_enable=0`. Must change DTB in flash; cannot trial at runtime |

Conclusion: Patch script premises (model, raw data, timing) verified on hardware. **Still unverified**: FEL recovery entry, and whether hx8399 tolerates 972 Mbit/s lane rate—only flashing will tell; still advise against writing without a FEL path.


## Addendum 3: FEL Measured (2026-10-20, No Flash Write)

- Device `/sbin/reboot` supports `efex`: `adb shell reboot efex` (ignore `can not access .../rtc/flag`) enters FEL; on Mac `sunxi-fel version` returns `AWUSBFEX soc=00001859(R528)`. **FEL works; Mac can talk to device.**
- Power is a **slide switch** (no power button): off then on for cold boot recovery; tested OK (MyMinUI-20260208b-0, `ref_fps:38`, adb OK).
- `sunxi-tools` not in brew; build: `brew install dtc pkgconf`, `git clone https://github.com/linux-sunxi/sunxi-tools`, `make sunxi-fel`.
- This `sunxi-fel` **has no R528 SPI support** (`spiflash-info`/`spiflash-read`: `SPI support not implemented yet for 1859 (R528)`), so SPI NOR read/write over FEL is not direct. Real brick recovery would need uploading a custom SPI helper via FEL—**not implemented or verified**.
- Still unverified: After corrupt flash, does power-on **automatically** enter FEL; no power button for “hold while power on”; manual entry may need SPI NOR pin strap or TF boot—both uninvestigated.
- Conclusion: FEL channel works, but “bricked flash is always recoverable” is **still not guaranteed**. Do not write flash until a usable FEL recovery flow exists.


## Addendum 4: SPI NOR Read via FEL Working (2026-10-20, Read-Only)

- Patched `sunxi-tools` (baseline `4ab7b32`) `fel-spiflash.c` for R528/D1/T113 (`0x1859`) SPI0: base `0x04025000`, CCU `0x02001000`, GPIO `0x02000000` (port stride 0x30), pins PC2–PC7 function 2; pin mux mask 4 bits not 3 (BROM leaves PC at `0xf`, 3-bit mask wrote invalid `0xa`). Patch: [sunxi-fel-r528-spi.patch](sunxi-fel-r528-spi.patch); `git apply` then `make sunxi-fel`.
- Measured `sunxi-fel spiflash-info`: Manufacturer `85h` (Puya), model `20h`, **16 MiB capacity**. mtd0’s 1 MiB is only the first partition of the whole SPI NOR.
- `sunxi-fel spiflash-read 0 0x1000000 <out>` read full 16 MiB; **byte-identical** to five partition images in repo `firmware/sjgam-m22pro/images/` (uboot/env/boot/boot-resource/rootfs); tail all `0xff`. Full backup `~/Desktop/mtd_dump/spi_full_16M_fel.bin`, SHA256 `76aa79b7…ec42a`.
- So: **If Linux won’t boot but FEL is reachable, entire SPI NOR can be read/compared.** Write path (`spiflash-write`, erase + page program) **not tested**; Puya not in tool’s chip table—defaults to 4 KiB erase `0x20` and 256 B page program `0x02`; validate on a harmless region (e.g. file tail `0xff` area) before trusting.


## Addendum 5: FEL Write Path Measured (2026-10-20)

- Test region: SPI offset `0xfb0000`, blank tail after rootfs (filesystem ends `0xfa7800`, then `0xff`); no system data affected.
- After `sunxi-fel spiflash-write 0xfb0000 <8 KiB random>`, readback **byte-identical**: erase (4 KiB, `0x20`) + page program (256 B, `0x02`) works on Puya; no write protection.
- One mistake during restore: `tr` for “all 0xff” produced UTF-8 `c3 bf`, writing `c3bf` from `0xfb0000` for 16 KiB. Fixed with Python-generated 16 KiB `0xff`, full 16 MiB readback **SHA256 identical to pre-flash backup (`76aa79b7…ec42a`)**—fully restored. Lesson: generate test data with Python or `xxd`; always full-chip readback compare after writes.
- Conclusion: **In FEL, `sunxi-fel` can read, write, and verify the whole SPI NOR.** With FEL entry (so far only verified via `reboot efex` while Linux runs), bricked devices have a recovery path: `spiflash-write 0 ~/Desktop/mtd_dump/spi_full_16M_fel.bin`.
- Still unverified: When flash is corrupt and Linux won’t boot, automatic/manual FEL entry. That is the remaining gap.


## Addendum 6: Flashed and Verified (2026-10-20)

- Method: `patch_mtd0_60fps.py` patch image (10 bytes changed in sectors `0xde000`, `0xe4000`); device `reboot efex` to FEL; R528-patched `sunxi-fel spiflash-write` **only those two sectors**; full 16 MiB readback: first 1 MiB matches patch image, remaining 15 MiB matches pre-flash backup. Patch image at `~/Desktop/mtd_dump/mtd0_60hz_patched.bin`.
- Result: Slide-switch reboot **normal boot**, adb OK, `ref_fps:60`, measured `fps:60.2`, `mgr0: 1080x1920`; dmesg `hx8399 lcd_panel_init is ok`; runtime DT `lcd_dclk_freq=0xa2` (162 MHz), `lcd_ht=0x546`, `lcd_vt=0x7e0`.
- DE `err` count stable at 81 after boot, no growth in 15 s; SoC ~37.6 °C idle.
- `/sys/class/disp/disp/attr/sys` no longer has `fps:38`; next `launch.sh` boot sets `NEWDTB=1`; `platform.c` enables vsync on internal panel.
- Rollback: `reboot efex`, `sunxi-fel spiflash-write 0 ~/Desktop/mtd_dump/spi_full_16M_fel.bin` (full chip), or only original `0xde000`/`0xe4000`; if FEL unreachable, CH341 programmer with same backup.
- To watch: long-run heat, battery, flicker; in-game vsync effect (tearing gone?, `minarch_prevent_tearing` tuning).


## Addendum 7: Frame Submit Timing at 60 Hz (2026-10-20)

Tool: [pantrace](pantrace/README.md) (passive timing of `minarch.elf` display ioctls). Game: GBA *Dragon Ball Advanced Adventure*, continuous scroll, ~100–120 s per run, **one run per configuration**. Raw output: [measurements/](measurements/).

| | A: Prevent Tearing=Off, Core Sync=Core | B: Prevent Tearing=Lenient, Core Sync=Auto |
|---|---|---|
| Submit interval p50 | **16.80 ms** (59.5 Hz) | **16.80 ms** (59.5 Hz) |
| p95 | 17.22 ms | 17.22 ms |
| p99 | 22.4 ms | **18.3 ms** |
| Intervals >20 ms | 76 (1.3%) | **48 (0.7%)** |
| Intervals <12 ms (catch-up bursts) | 12 | **4** |
| `FBIOPAN_DISPLAY` avg duration | 6.06 ms | 6.05 ms |
| `>30 ms` count (menu/pause) | 7 | 7 |

Conclusions (data-supported only):

1. **Both configs lock submit cadence at 16.80 ms (59.5 Hz), the new panel refresh**, matching calculated 59.52 Hz. `FBIOPAN_DISPLAY` blocks ~6 ms per frame waiting for vblank.
2. **M22Pro portrait uses the rotation path; `PLAT_flip` always calls `pan_display` there** (`sync && isnewdtb` applies only on the non-rotated path), so vsync wait does not depend on Prevent Tearing or `NEWDTB`. Explains why toggling that setting feels similar.
3. On the old 38 Hz panel, waiting for vblank each frame **capped submits at ~38/s** while GBA core outputs ~59.7/s, so frames were dropped. Now ~59.5 submits/s. Mechanism inference—**no pantrace from 38 Hz era** (tool did not exist).
4. At 60 Hz, B (Lenient+Auto) slightly better than A: >20 ms 1.3%→0.7%, bursts 12→4. Direction consistent but **one ~100 s run per group with menu pauses—not statistically significant**.
5. **Tearing not measured**; tool shows submit timing only. Tearing needs high-speed video (e.g. phone 240 fps on `0testtearing.p8`).


## Addendum 8: Fixed Wrong system.cfg Default (2026-10-20)

- `minarch` option values match via **case-sensitive** `strcmp`; mismatch falls back to index 0. `skeleton/SYSTEM/m21/system.cfg` had `minarch_prevent_tearing = lenient` (lowercase) but valid values are `Off/Lenient/Strict`, so factory default was **`Off`** (logs show `SET Prevent Tearing TO Off`).
- Changed to `Lenient` (repo `skeleton/SYSTEM/m21/system.cfg`; device `/mnt/SDCARD/.system/m21/system.cfg` synced; backup `~/Desktop/mtd_dump/system.cfg.orig`).
- Saved `minarch.cfg` on device for `GBA-gpsp`, `FC-fceumm`, `P8-fake08` override system defaults; all set to `Prevent Tearing=Lenient`, `Core Sync=Auto` (backups `~/Desktop/mtd_dump/*_minarch.cfg.orig`). New cores get new defaults.
- Tracking tool `pantrace` removed from device (`/overlay/etc/ld.so.preload` and `/overlay/lib/pantrace.so` deleted).
