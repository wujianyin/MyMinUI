# TF 卡损坏后的恢复清单(SJGAM M22Pro / MyMinUI)

> English (主文档): [SD_RESTORE.md](SD_RESTORE.md) | 返回 [README.zh-CN.md](README.zh-CN.md)

本文分两部分:第 1–6 节:卡损坏或被格式化后重建;第 7 节:卡没坏但被内核改成**只读**(MinUI 卡在色条界面),用 `fsck.fat` 修复。

2026-10-21 TF 卡损坏并格式化后,按下面步骤恢复到"最终确认"的状态。全部是仓库里已有的文件,可重复执行。

## 1. 基础系统

1. 卡格式化为 FAT32。
2. 解压最后一个 m21 发布包(`MyMinUI-20260208b-0-m21.zip`,<https://github.com/Turro75/MyMinUI/releases/tag/MyMinUI-20260208>),把整个包内容放到卡根目录;把 `m21/emulationstation` 和 `m21/tomato` 复制到根目录(见 `INSTALL.md` 的 SJGAM 一节)。
3. 自己恢复 ROM(`Roms/`)、BIOS(`Bios/`)。`m21/thisism22` 标记由 `tomato` 在 M22Pro 上自动创建。

## 2. 我们改过的部分

| 内容 | 来源(仓库) | 卡上位置 |
|---|---|---|
| 带 USB 启动默认值的启动脚本 | `workspace/m21/other/usbmode/tomato.m21` | `/tomato`(覆盖,11749 字节) |
| 语言选择器和首次选择语言 | `dockerbuild/` 编出的 `language.elf`、`skeleton/EXTRAS/Tools/m21/Language.pak/` | `/.system/m21/bin/language.elf`、`/Tools/m21/Language.pak/` |
| 界面语言包 | `skeleton/SYSTEM/res/lang/`、`skeleton/EXTRAS/Tools/m21/*/lang/` | `/.system/res/lang/`、`/Tools/m21/<工具>.pak/lang/` |
| 当前语言 | 首次进入时选择 | `/.userdata/shared/language`(`zh_CN` 或 `en`) |
| USB Mode 工具 | `skeleton/EXTRAS/Tools/m21/USB Mode.pak/` | `/Tools/m21/USB Mode.pak/` |
| 系统默认值(`minarch_prevent_tearing = Lenient`) | `skeleton/SYSTEM/m21/system.cfg` | `/.system/m21/system.cfg` |
| USB 开机默认 | 见下 | `/.userdata/m21/usb_boot_mode.cfg`、`usb_nettype` |
| 中文字体(覆盖同名文件) | `workspace/m21/other/cjkfont/BPreplayBold-unhinted.otf` | `/.system/res/BPreplayBold-unhinted.otf` |
| Files.pak 中文字体(覆盖同名文件) | `workspace/m21/other/cjkfont/Files.pak/res/FreeSans.ttf` | `/Tools/m21/Files.pak/res/FreeSans.ttf` |
| 测试 ROM(可选) | `skeleton/EXTRAS/Roms/Pico-8 (P8)/` | `/Roms/Pico-8 (P8)/` |

```sh
printf 'adb_only\nnet\n-\n' > /.userdata/m21/usb_boot_mode.cfg   # adb 开、USB 网络开、串口关
printf 'ncm\n'             > /.userdata/m21/usb_nettype
```

各核心保存的 `minarch.cfg`(按键绑定等)在 `.userdata/m21/<系统>-<核心>/` 下,**不在仓库里**,损坏后只能靠自己的备份;新玩的核心会直接使用 `system.cfg` 的默认值(防撕裂 `Lenient`、同步 `Auto`)。

## 3. 不在 TF 卡上的内容

- **60Hz 补丁**在设备内部 SPI Flash 里,TF 卡损坏不影响。详见 `workspace/m21/other/dtb_60fps/`。
- 刷坏 SPI 的恢复:`reboot efex` 进 FEL,用打了补丁的 `sunxi-fel` 写回完整备份;或用 CH341 编程器写入同一备份。

## 4. 恢复不了的

存档(`Saves/`)、即时存档、最近游玩列表、亮度/音量/睡眠设置。建议定期把整张卡做成镜像备份(例如读卡器接入后 `sudo dd if=/dev/rdiskN of=tf.img bs=4m`,先用 `diskutil list` 确认 N)。

## 5. 为什么会损坏(推断)

见 `workspace/m21/other/usbmode/FEATURES_AND_ISSUES.zh-CN.md` 末尾:设备读写挂载 SD 的同时,USB Storage 读写模式让电脑也能写同一份 FAT。USB Mode 现在默认只读;不要在读写模式下往卡里拷文件,用读卡器。

## 6. 恢复后核对(adb)

`ref_fps:60`、`/mnt/SDCARD/tomato` 为 11749 字节、`Tools/m21/USB Mode.pak` 存在、`usb_boot_mode.cfg` 为 `adb_only/net/-`、`system.cfg` 里是 `Lenient`、`/sys/kernel/config/usb_gadget/g1/functions` 有 `ffs.adb` 和 `ncm.usb0`、`usb0` 为 `192.168.7.1`。

`/.system/res/lang/zh_CN.txt` 也应存在。

## 7. 卡被改成只读(MinUI 卡在色条界面)

**现象和原因**:硬断电、重启或写入出错后,内核发现 FAT 错误,按 `errors=remount-ro` 把卡改成只读。`launch.sh` 用 `minui.elf > $LOGS_PATH/minui.txt` 启动桌面,
只读卡上这个重定向失败,`minui.elf` 起不来,循环不停重试,屏幕就一直停在开机色条。adb 仍然可用。

确认:

```sh
adb shell 'mount | grep "on /mnt/SDCARD "'        # 带 ro 就是只读
adb shell 'dmesg | grep -i -E "fat|mmcblk0"'      # FAT-fs error
```

原厂系统没有 `fsck.vfat`,所以用 Docker 交叉编译一个静态 ARM 的 `fsck.fat`(dosfstools 4.2)。

**修复(约一分钟)**:

```sh
# 1. 宿主机:编译一次(需要 Docker 和 kbuild-arm2 镜像,见 workspace/m21/other/dockerbuild/README.zh-CN.md)
OUT=/tmp/fsckb sh workspace/m21/other/dockerbuild/build_fsckfat.sh

# 2. 只检查不写入(卡保持只读挂载也可以)
adb push /tmp/fsckb/fsck.fat /tmp/fsck.fat && adb shell chmod +x /tmp/fsck.fat
adb shell '/tmp/fsck.fat -n -v /dev/mmcblk0p1'

# 3. 修复,复查,重启让内核忘掉旧的错误状态
adb shell '/tmp/fsck.fat -a -v /dev/mmcblk0p1; sync'
adb shell '/tmp/fsck.fat -n /dev/mmcblk0p1'
adb shell 'sync; reboot'
```

能的话先备份用户数据(`.userdata`、`Saves`、`Roms`)。这台设备上 `adb exec-out` 不可用,文件用 `adb pull`(或者用读卡器在电脑上整卡 `dd`,再用 macOS 的 `fsck_msdos`)。
`fsck.fat -a` 只改元数据:截断损坏的文件、释放孤立的簇。

**2026-10-25 这次修复发现的问题**(M22Pro 的卡,FAT32、7.3 GB、`/dev/mmcblk0p1`,损坏很小):
- `/.userdata/shared/.minui/recent.txt`:大小记录 211 字节,但簇链为空(指向一个空闲簇)。已截断为 0 字节,之后 MinUI 重新写入。
- 7 个目录(`Bios`、`Cheats`、`Emus`、`M21`、`Roms`、`Tools`、`.Trashes/...`)的 `..` 条目无效,已修复。
- 回收 25 个无引用的簇(100 KB),并修正了脏标记和空闲簇计数。

修复并重启后:卡为 `rw` 挂载,`minui.elf` 正常运行,adb 自动连上,语言、字体、`tomato` 都没受影响。

**避免再出现**:从菜单关机,不要直接断电;MinUI 正在写入(列表变化、存档)时不要强断电;设备运行时不要在 USB Storage 读写模式下从电脑往卡里写文件。
