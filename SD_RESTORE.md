# TF 卡损坏后的恢复清单(SJGAM M22Pro / MyMinUI)

2026-10-21 TF 卡损坏并格式化后,按下面步骤恢复到"最终确认"的状态。全部是仓库里已有的文件,可重复执行。

## 1. 基础系统

1. 卡格式化为 FAT32。
2. 解压最后一个 m21 发布包(`MyMinUI-20260208b-0-m21.zip`,<https://github.com/Turro75/MyMinUI/releases/tag/MyMinUI-20260208>),把整个包内容放到卡根目录;把 `m21/emulationstation` 和 `m21/tomato` 复制到根目录(见 `INSTALL.md` 的 SJGAM 一节)。
3. 自己恢复 ROM(`Roms/`)、BIOS(`Bios/`)。`m21/thisism22` 标记由 `tomato` 在 M22Pro 上自动创建。

## 2. 我们改过的部分

| 内容 | 来源(仓库) | 卡上位置 |
|---|---|---|
| 带 USB 启动默认值的启动脚本 | `workspace/m21/other/usbmode/tomato.m21` | `/tomato`(覆盖) |
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

见 `workspace/m21/other/usbmode/FEATURES_AND_ISSUES.md` 末尾:设备读写挂载 SD 的同时,USB Storage 读写模式让电脑也能写同一份 FAT。USB Mode 现在默认只读;不要在读写模式下往卡里拷文件,用读卡器。

## 6. 恢复后核对(adb)

`ref_fps:60`、`/mnt/SDCARD/tomato` 为 11605 字节、`Tools/m21/USB Mode.pak` 存在、`usb_boot_mode.cfg` 为 `adb_only/net/-`、`system.cfg` 里是 `Lenient`、`/sys/kernel/config/usb_gadget/g1/functions` 有 `ffs.adb` 和 `ncm.usb0`、`usb0` 为 `192.168.7.1`。
