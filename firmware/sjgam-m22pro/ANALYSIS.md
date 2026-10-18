# SJGAM M22Pro 原厂固件分析

对象:SJGAM M22Pro(上游 MyMinUI 中归入 `m21` 平台),固件为我从设备 SPI NOR 导出的原厂镜像。
所有结论来自对镜像和运行中设备(`adb shell`)的检查;**标注"推断"的是没有被直接证实的判断**。

镜像文件在 [`images/`](images/),提取出的配置/脚本在 [`extracted/`](extracted/),校验和见 [`images/SHA256SUMS`](images/SHA256SUMS)。

---

## 1. 硬件概览

| 项目 | 内容 | 来源 |
|---|---|---|
| SoC | Allwinner **H133**(内核/DTS 里称 `sun8iw20p1`,与 T113 同系列),双核 Cortex-A7,ARMv7,NEON/VFPv3 | DTS `compatible = "allwinner,h133", "arm,sun8iw20p1"`,`/proc/cpuinfo` |
| CPU 频率 | 480 / 720 / 912 / 1008 / 1104 / **1200 MHz** | cpufreq `scaling_available_frequencies` |
| 内存 | **256 MB** DRAM(可用约 234 MB,CMA 8 MB) | `androidboot.dramsize=256`,`/proc/meminfo`。注意 DTS 里 memory 节点写的是 512 MB(0x20000000),实际由 U-Boot 传参覆盖 |
| 板载存储 | **16 MiB SPI NOR**(存放 U-Boot、内核、原厂 rootfs) | `/proc/mtd`,分区总和恰好 0x1000000 |
| 外部存储 | microSD(本机 32 GB FAT32),存放 MinUI/游戏/`tomato` | `/dev/mmcblk0` |
| 屏幕 | MIPI-DSI 面板 **hx8399**,1080×1920 竖屏面板;帧缓冲 720×1280(`fb0` 虚拟 720×2560 双缓冲),显示引擎缩放到 1080×1920 | 运行时 `/proc/device-tree/.../lcd_driver_name` 与 `/sys/class/disp` |
| USB | 1 个 OTG 口(`sunxi_usb_udc`,UDC 名 `4100000.udc-controller`),设备/主机可切换 | sysfs `usbc0` 的 `usb_device/usb_host/usb_null` |
| 音频 | `audiocodec`(ALSA),线路输出/MIC 参数在 `/etc/rc.local` | rootfs |
| 输入 | `event0-3`、`js0-2`(游戏手柄节点) | `/dev/input` |
| 串口 | `ttyS0` 115200(uart0,基址 0x02500000) | U-Boot env / bootargs |

### 1.1 两份 DTB(重要)
`mtd0` 里能找到两份设备树:

| 文件 | 偏移 | 面板驱动 | 分辨率 | 说明 |
|---|---|---|---|---|
| `extracted/dts/mtd0_dtb_0_0x862b8.dts` | 0x862b8 | `st7703s` | 640×480 (lcd_x=0x280, lcd_y=0x1e0) | **推断**:同系列横屏 4:3 机型(M21)的配置,和 MinUI `m21` 平台的 `FIXED_WIDTH/HEIGHT = 640×480` 吻合 |
| `extracted/dts/mtd0_dtb_1_0xd6000.dts` | 0xd6000 | `hx8399` | 1080×1920 | 与 U-Boot 包内的 `2_dtb.bin`(60608 字节)同一份;**本机运行中使用的就是 hx8399** |

即同一套固件框架支持多种屏幕/机型,按 DTB 区分。MyMinUI 的 `m21` 平台代码要同时适配两者。

---

## 2. 分区与镜像格式

`/proc/mtd`(同 `images/mtd_info.txt`):

| 分区 | 大小 | 内容 | 镜像文件 | 格式 |
|---|---|---|---|---|
| mtd0 `uboot` | 0x100000 (1 MiB) | boot0 SPL + boot package(U-Boot、OP-TEE、DTB) | `mtd0_uboot.bin` | Allwinner `eGON.BT0` + `sunxi-package` |
| mtd1 `env` | 0x20000 (128 KiB) | U-Boot 环境变量 | `mtd1_env.bin` | U-Boot env(CRC + 文本) |
| mtd2 `boot` | 0x400000 (4 MiB) | 内核 | `mtd2_boot.bin` | Android boot image v0(页大小 2048) |
| mtd3 `boot-resource` | 0xed800 (≈950 KiB) | 开机 logo 等资源 | `mtd3_boot-resource.bin` | FAT16 |
| mtd4 `rootfs` | 0x9f2800 (≈10.2 MB) | 原厂系统 | `mtd4_rootfs.squashfs` | SquashFS 4.0,xz,128 KiB 块 |
| mtd5 `UDISK` | 0 | 未使用(空) | (未提供) | — |

> `mtd4_rootfs.squashfs` 在运行中的设备上是 `/dev/root`,**已用 100%(9.6M)只读**,所以任何改动都不能写进去 ——
> 这也是 MyMinUI 另外在 SD 卡上放一个 ext2 `rootfs.ext2` 并 `chroot` 的原因。

### 2.1 mtd0:SPL 与 boot package
- `0x0`:`eGON.BT0` boot0 SPL(提取为 `boot0_spl.bin`,64 KiB,开头 `f0 00 00 ea`(ARM 跳转)+ `eGON.BT0` 魔数)。
- `0x10000`:`sunxi-package` boot package,内含
  - `u-boot.bin`(507,904 字节,U-Boot **2018.05-g89c9751-dirty,编译于 2022-08-10**,取自内核启动参数 `uboot_message`)
  - `optee.bin`(300,344 字节,OP-TEE;DTS 中有 `linaro,optee-tz` 节点)
  - `dtb.bin`(60,608 字节,即上面的 hx8399 DTB)
- 提取 DTB:在 `mtd0.bin` 里搜索 FDT 魔数 `d0 0d fe ed`,从偏移处读到 `totalsize`,再 `dtc -I dtb -O dts`。

### 2.2 mtd1:U-Boot 环境
见 [`extracted/bootloader/mtd1_uboot_env.txt`](extracted/bootloader/mtd1_uboot_env.txt),要点:
```
bootdelay=0
bootcmd=run setargs_nor boot_normal
boot_normal=sunxi_flash read 43000000 ${boot_partition};bootm 43000000
setargs_nor=... console=ttyS0,115200 loglevel=8 root=/dev/mtdblock4 rootfstype=squashfs init=/sbin/init cma=8M ... gpt=1
```
- `mac` / `wifi_mac` / `bt_mac` / `snum` 都是空的(没有写入设备唯一信息,可放心公开)。
- 实际内核命令行(运行时 `/proc/cmdline`)还包含 `partitions=env@mtdblock1:boot@mtdblock2:boot-resource@mtdblock3:rootfs@mtdblock4:UDISK@mtdblock5`、`androidboot.dramsize=256`、`disp_reserve=3686400,0x4bf32bc0` 等。

### 2.3 mtd2:内核
- Android boot image:kernel 3,901,168 字节,加载地址 `0x40008000`;ramdisk 仅 12 字节(占位);内核镜像是 **gzip 压缩的 zImage**,gzip 流位于 zImage 内偏移 `0x6893`(zImage 本身位于 `mtd2` 偏移 2048),解压后得到 10,805,168 字节的原始 `vmlinux.bin`(基址 `0xc0008000`)。
- 内核:**Linux 5.4.61 #664 SMP PREEMPT,2024-10-25 06:03:41 UTC**,工具链 `arm-openwrt-linux-muslgnueabi-gcc 6.4.1`(Linaro 6.4-2017.11)。
- `CONFIG_ARM_APPENDED_DTB=y`,`CONFIG_KERNEL_GZIP=y`,`CONFIG_KALLSYMS=y`,**`CONFIG_MODVERSIONS` 未开启**(外置模块不需要版本 CRC,只需 vermagic 匹配)。
- 配置全文:[`extracted/kernel/kernel_config.txt`](extracted/kernel/kernel_config.txt);符号表:[`extracted/kernel/kallsyms.txt`](extracted/kernel/kallsyms.txt)(对照反汇编 `vmlinux.bin` 用,见第 6 节)。

### 2.4 mtd3:boot-resource
FAT16 镜像,内含:`BOOTLOGO.BMP`(480×480×24bpp,已转存 `BOOTLOGO.png`)、`MAGIC.BIN`(512 字节)。

### 2.5 mtd4:rootfs
- SquashFS 4.0 / xz / 10,064,856 字节 / 891 个 inode / 2024-10-26 生成。
- `unsquashfs -d mtd4_rootfs images/mtd4_rootfs.squashfs` 即可展开(完整展开约 37 MB,其中约 14 MB 是原厂附带的 NES 游戏,本仓库没有收录展开后的整棵树,只收录了脚本/配置,以及完整文件清单 `extracted/rootfs_filelist.txt`)。

---

## 3. 原厂系统(Tina Linux)

- 基于 OpenWrt 的 **Tina Linux 4.0.0**,代号 *Neptune*;构建号 `tina.root.20241025.060322`,目标 `h133-p2nor/generic v3.5`。
- `/etc/os_version`:`2024.10.26-v1.1.1`;`/etc/model`:`M22`。
- C 库 **musl**(`/lib/ld-musl-armhf.so.1`),busybox,procd/ubus/uci;**没有 glibc**。
- 内核模块仅有 `fuse.ko` 与一组 netfilter/iptables(含 `nf_nat`、`iptable_nat`、`xt_REDIRECT`)——所以将来做"共享上网/NAT"在原厂内核里是可行的。
- 原厂 rootfs 里的 root 密码哈希是厂商默认值(`/etc/shadow`);串口/adb 都直接给 root shell,没有设防。

### 3.1 启动流程
```
BROM → boot0 SPL(SPI NOR 0x0)
     → U-Boot 2018.05 + OP-TEE(boot package)
     → 读 mtd2 `boot`,bootm 0x43000000(Android boot image 里的 zImage + appended DTB)
     → Linux 5.4.61,root=/dev/mtdblock4(squashfs)
     → /sbin/init(procd) → /etc/init.d/rcS
         · adbd   (START=80)
         · S99run (START=99)
```
`/etc/init.d/S99run` 的要点:
1. `mount /dev/mmcblk0p1 /mnt/SDCARD`(SD 卡第 1 分区),并创建 `/sdcard`、`/data` 软链。
2. 后台运行 `/usr/bin/switchev`(用途**推断**为按键/输入事件处理,未反编译确认)。
3. 循环:`while [ -f /usr/guimenu ]`:读 `/sys/class/gpadc/data`、`charge` 做低电量检测(`/usr/bin/lowpower`),然后执行 `/usr/guimenu`。

`/usr/guimenu`(脚本)决定实际启动哪个前端:
```sh
export LD_LIBRARY_PATH=/mnt/SDCARD/lib
export HOME=/mnt/SDCARD
CMD=/usr/bin/smallui                      # 内置兜底 UI
if [ -f /usr/tomato ]; then MISCBIN=/mnt/SDCARD/tomato
else MISCBIN=/mnt/SDCARD/emulationstation; fi
[ -f $MISCBIN ] && CMD=$MISCBIN           # SD 卡上存在就用它
if $CMD; then [ -f /tmp/.next ] && sh /tmp/.next ...; fi
```
> **关键发现**:rootfs 里放了一个 1 字节的 `/usr/tomato` 文件,所以 `guimenu` 会去执行 **SD 卡上的 `/mnt/SDCARD/tomato`**。
> 这就是 MinUI 第三方系统的入口(厂商为自定义系统预留的钩子);没有它则退回 `emulationstation`,再没有就用内置 `smallui`。
> `tomato` 本身是个 shell 脚本:它用 fuse2fs 把 SD 上的 `rootfs.ext2` 挂到 `/overlay`,bind 挂载 dev/proc/sys/configfs/SDCARD/tmp,然后 `chroot /overlay` 启动 MinUI。
> 本仓库对 `tomato` 做的 USB 相关修改见 `workspace/m21/other/usbmode/tomato.m21`。

### 3.2 原厂应用与库
- 前端:`smallui`、`switchev`、`lowpower`、`testmode`(`/tmp/.testmode` 存在时进入测试模式)。
- 图形:DirectFB 1.7.7(`/usr/lib/directfb-1.7-7`,`dfb*` 工具)、tslib、freetype;音频:ALSA、`libsamplerate`。
- 模拟器:`/usr/bin/fceumm_libretro.so`(NES 核心),`/game/` 下约 14 MB 的 NetChip NES 游戏(厂商内置,**未收录**)。
- 网络:`wpa_supplicant`、`hostapd`、`dnsmasq`、`ntpd`、`curl`/`wget`;蓝牙辅助脚本 `bt_as_on/off/play`、`sbcenc/dec`。
- `/www/luci-static` 只有静态资源,rootfs 的 `usr/sbin` 里没有看到 web 服务器。
- 注意:原厂 rootfs(musl)与 MinUI 的 chroot(glibc 2.26)是**两套用户空间**,二者共用同一个内核和 `/dev`、`/sys`。

### 3.3 USB / ADB(原厂实现)
- `/etc/init.d/adbd`:调用 `/bin/setusbconfig adb`,创建 gadget `g1`:VID `0x18d1` / PID `0xD002`,厂商 "Allwinner",产品 "Tina ADB",序列号默认 `20080411`;`adbd -D` 经 FunctionFS(`/dev/usb-ffs/adb`)工作。
- `/bin/setusbconfig` 声称支持 `mtp / adb / rndis / adb,mtp`(及 `uac1`、`hid` 等),但**当前内核只内置了 `gser` 和 `ffs`**:`rndis/ecm/ncm` 的内核功能块没有编译,脚本里的 rndis 选项在这台机器上实际用不了 ——
  这就是为什么 USB 网络需要自己编外置模块(见 `workspace/m21/other/usbmode/NOTES.md`)。
- UDC 驱动 `sunxi_udc` 设置了 `quirk_altset_not_supp`,并且 `struct usb_gadget`、`struct module`、`struct sk_buff` 等与主线 5.4.61 布局不同(详见 NOTES)。
- 端点资源:ep1/ep2/ep5 各一对 bulk,ep4 interrupt,ep3 iso。

---

## 4. 对 MyMinUI 移植有用的结论
1. 入口是 `/usr/tomato` + `/mnt/SDCARD/tomato`,不需要改写 SPI NOR 里的任何东西 —— **装坏了只要换/改 SD 卡内容即可恢复,不会变砖**。
2. 原厂 rootfs 满且只读,新用户空间只能放在 SD 卡(ext2 镜像 + `chroot`)。
3. 内核没有 `CONFIG_MODVERSIONS`,厂商 `.config` 可用于外置模块编译;需要的头文件结构体偏移要自己核对(NOTES 里有完整方法和一张补丁表)。
4. 屏幕随机型不同(DTB 不同),显示分辨率和旋转要分别处理。
5. `panic=0` 且无持久化 ramoops:内核在中断里崩溃会等看门狗复位,不留日志。RTC 的 GP 寄存器(物理 `0x07090100`)能跨复位保存,我用它做过"崩溃黑匣子"(`crashlog.ko`,见 NOTES)。

---

## 5. 如何自行复现提取

```sh
# 在设备上读出各分区(大小见 /proc/mtd)
adb shell cat /dev/mtdblock0 > mtd0.bin   # 同理 1..4
adb shell cat /proc/mtd

# rootfs
unsquashfs -d mtd4_rootfs mtd4_rootfs.squashfs

# 设备树
dtc -I dtb -O dts -o x.dts x.dtb          # DTB 位置见 §1.1 / §2.1

# 内核(已验证:结果与导出的 vmlinux.bin 逐字节一致)
python3 - <<'EOF'
import zlib, struct
d = open('mtd2_boot.bin','rb').read()
ksize = struct.unpack('<I', d[8:12])[0]            # Android boot image 头:kernel_size = 3901168
zimage = d[2048:2048+ksize]                        # 内核从第 1 页(页大小 2048)开始 = zImage
open('zImage','wb').write(zimage)
z = zimage.find(b'\x1f\x8b\x08')                  # zImage 内的 gzip 流,偏移 0x6893
open('vmlinux.bin','wb').write(zlib.decompressobj(31).decompress(zimage[z:]))   # 10805168 字节
EOF
# 反汇编(Mac 没有 arm objdump,可以在 docker 里跑)
arm-linux-gnueabihf-objdump -D -b binary -m arm --adjust-vma=0xc0008000 vmlinux.bin
```

---

## 6. 已知未解决 / 待确认
- `hx8399` 与 `st7703s` 两套 DTB 分别对应哪些具体机型,没有证据,上面是推断。
- U-Boot 里 DRAM 容量如何覆盖 DTS 的 512 MB(只确认了实际 256 MB)。
- `MAGIC.BIN`、`optee.bin` 的内容没有分析。
- `/etc/config/smartlinkd`、`/etc/miracast.cfg` 暗示原厂有投屏/配网功能,是否可用未测。
- 内核 `CONFIG_USB_*` 里是否还有可以直接打开的功能(只确认 ECM/NCM/RNDIS 没编译)。

## 7. 版权与风险提示
- 这些镜像是**设备原厂固件的原样导出**,版权归 Allwinner 及设备厂商所有,仅为研究/兼容性开发/备份用途放在这里;不保证与你的设备批次一致。
- **刷写 SPI NOR 有变砖风险**,本仓库不提供也不建议刷写;MyMinUI 只需要 SD 卡即可使用。
- 镜像里没有 MAC、序列号等设备唯一信息(U-Boot env 中为空),但 `/etc/shadow` 中是厂商默认 root 密码哈希。
