# Chinese fonts (MinUI, USB Mode and every other screen)

> 中文版: [README.zh-CN.md](README.zh-CN.md) | Back to [README](../../../../README.md)

All MinUI programs (minui, minarch, every pak) use one font file, `.system/res/BPreplayBold-unhinted.otf`. The original has Latin glyphs only, so Chinese shows up as blanks.
`BPreplayBold-unhinted.otf` here is a **same-name replacement** (the extension stays, the content is TrueType; FreeType detects it by content), so **nothing has to be recompiled**.

**The release zip built by `workspace/m21/other/dockerbuild/package.sh` already contains both replacement files**; install them by hand only on a card that does not use that zip.

## Result

- Latin glyphs are still the original BPreplay Bold (same look); Han characters, kana and full-width punctuation come from Noto Sans CJK SC Bold.
- Coverage: all of GB2312 (about 7.4 thousand characters and symbols), kana, CJK punctuation, full-width forms, common arrows and geometric symbols. Not included: Hangul, rare GBK characters outside GB2312, Traditional-only characters.
- The vertical metrics (ascent/descent/lineGap ...) are **forced to the original font's values** (1089/-285/31), so button height, line spacing and text position are unchanged.
- Size 2.2 MB (original 169 KB). Preview: `preview.png`.

![preview](preview.png)

## Install by hand

```sh
# device online (adb)
adb push BPreplayBold-unhinted.otf /mnt/SDCARD/.system/res/BPreplayBold-unhinted.otf
# then reboot the device (or leave and re-enter the program) so the font reloads
```

Without adb, copy the file to `.system/res/` on the card and overwrite the file with the same name.

## Restore

The original font is the same as `skeleton/SYSTEM/res/BPreplayBold-unhinted.otf` in the repository; copy it back.

## Files.pak (file manager MyCommander)

`Files.pak` uses its own font, `Tools/m21/Files.pak/res/FreeSans.ttf` (1.5 MB, also without Chinese). The replacement here is FreeSans (Latin/Greek/Cyrillic unchanged) plus Noto Sans CJK SC **Regular**
(Han, kana, punctuation; the weight matches FreeSans), again with the vertical metrics fixed to the original values; size 3.7 MB. MyCommander uses `TTF_RenderUTF8`, so Chinese file names display directly.

![Files.pak preview](Files.pak/preview.png)

```sh
adb push Files.pak/res/FreeSans.ttf "/mnt/SDCARD/Tools/m21/Files.pak/res/FreeSans.ttf"
```

Without adb copy it to `Tools/m21/Files.pak/res/` on the card. The original can be taken from the release zip (`Tools/m21/Files.pak/res/FreeSans.ttf`).
Generation scripts: `1b_subset_noto_regular.py` (needs `NotoSansCJKsc-Regular.otf`, same download as the Bold one with `Bold` replaced by `Regular`) and `3_merge_freesans.py` (depends on `to_ttf()` in `2_merge.py`).

## Languages

The font only provides the glyphs. Which language is shown is chosen by the UI language setting, see [I18N.md](../../../../I18N.md).
