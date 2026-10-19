# USB Mode.pak 工作记录与后续交接文档

设备:Allwinner H133/T113(sun8iw20,256MB RAM)掌机,Tina Linux 5.4.61(armv7l),SPI NOR 上的 musl 原厂 rootfs;
MyMinUI(`m21` 平台)跑在 32GB FAT SD 卡上的 `chroot /overlay`(glibc 2.26 的 ext2 镜像,fuse2fs 挂载)里。
宿主机:Mac。本仓库:`/Users/wujianyin/work/gitwork/MyMinUI`。

> 本文档记录:最终状态 → 整体架构 → 一步步怎么做到的 → 踩过的坑与解法 → 遗留问题 / 下一步 → 环境复现与备份位置。

---------------------------------------------------------------------

## 1. 最终状态(截至本次结束)

`/mnt/SDCARD/Tools/m21/USB Mode.pak` 是一个交互式 USB 管理工具,菜单项:

| 菜单项 | 取值 | 状态 |
|---|---|---|
| USB Storage (SD Card) | OFF / RW / RO | 已测(早期会话) |
| ADB Debugging | ON / OFF | 已测 |
| USB Serial (ttyGS0) | ON / OFF | 已测(早期会话) |
| **USB Network (192.168.7.x)** | OFF / ECM | 本次新增;功能经 shell 流程在设备上实测,**菜单 UI 本身未在设备屏幕上点过** |
| USB Port Role | Device / OTG Host | 已测(早期会话) |
| On Exit to MinUI | Keep / Auto eject | 已有 |
| Boot Default | ADB only / ADB+Storage RW / ADB+Storage RO / USB Off | 开机钩子本次修复并实测(见下) |
| **Boot USB Network** | ON / OFF | 本次新增,开机钩子实测通过 |
| Return to MinUI | | |

开机实测通过的组合(重启后检查):
- `adb_only` + `net`:ADB 正常,Mac 上出现 "Tina ADB" 网卡,DHCP 得到 192.168.7.2,互 ping ~1ms。
- `adb_ums_ro` + `net`:ADB + 只读 SD(Mac 上挂出 SAMSUNGDISK)+ USB 网络 三者同时可用。
- 设备上当前开机默认已恢复为 `adb_only`(不带网络)。

**未测**:`off` 开机模式(会关闭 USB,adb 也没了,需拔 SD 卡改回配置);`adb_ums_rw` 已重启实测通过(ffs.adb+ffs.ums、umsd 运行、Mac 上出现 SD 卡);菜单里手动切换网络(逻辑用 `t_net.sh` 在 chroot 中模拟过 off/on/up 流程,可用);
Windows 主机(RNDIS/NCM);Linux 主机(理论可行,没试)。

---------------------------------------------------------------------

## 2. 文件清单与位置

### 仓库内(未提交,`git status` 显示 untracked)
- `skeleton/EXTRAS/Tools/m21/USB Mode.pak/`
  - `launch.sh`(导出 `USBMODE_PAK_DIR`,把 umsd/usbmode 拷到 /tmp 运行)
  - `usbmode.elf`、`umsd.elf`、`dhcpd_mini.elf`、`usbnet.sh`
  - `modules/u_ether_x.ko`、`modules/f_ecm_x.ko`
- `workspace/m21/other/usbmode/`
  - `usbmode.c`(UI)、`umsd.c`(FunctionFS 存储守护进程)、`dhcpd_mini.c`、`usbnet.sh`、`crt_glibc.c`
  - `tomato.m21`(设备上 `/mnt/SDCARD/tomato` 的当前版本)
  - `kmod/`:`f_ecm.c`、`u_ether.c`、`Kbuild`、`build.sh`、`Dockerfile`、各 `*_pad.*`(头文件填充补丁)
  - `README.md`(简版说明)、本文件

注意:`build/FULL/m21/tomato`(构建产物)被 `.gitignore` 忽略,**tomato 的真正源文件在仓库里没找到**,
需要把 `tomato.m21` 里 USB 相关改动合并进真实来源(见第 6 节)。

### 设备(SD 卡)
- `/mnt/SDCARD/tomato`(开机钩子,已更新;旧版备份:Desktop 的 `USB Mode.pak/tomato.before_net`)
- `/mnt/SDCARD/Tools/m21/USB Mode.pak/*`(同仓库 skeleton 内容)
- `/mnt/SDCARD/.userdata/m21/usb_boot_mode.cfg`
  - 第 1 行:`adb_only | adb_ums_rw | adb_ums_ro | off`
  - 可选第 2 行:`net`(开机启用 USB 网络)
- `/mnt/SDCARD/usb_adb_debug.log`(开机 USB 初始化日志,排查首选)

### 备份(Desktop,/tmp 会被清空!)
`/Users/wujianyin/Desktop/mtd_dump/USB Mode.pak/`
- 所有源码与二进制副本、`tomato.before_net`、`tomato.with_net`
- `kmod_src/`(旧版内核模块源码与头文件补丁)
- `build_env/`:交叉编译环境备份(`m21_glibc`(设备 glibc 2.26 库)、`sdl2_inc`、`api.o/platform.o/utils.o`、
  `crt_glibc.c`、`device.config`(厂商 .config)、两个 Dockerfile、`tools/` 里的调试工具与脚本)
- 另有 `/Users/wujianyin/Desktop/mtd_dump/unpacked/`:原厂 flash 解包(`vmlinux.bin`、`kallsyms.txt`、`kernel_config.txt`、`mtd4_rootfs`)

---------------------------------------------------------------------

## 3. 架构要点(必须先理解)

1. **双 rootfs**:原厂 musl 根跑 `/bin/adbd -D`;`tomato` 用 fuse2fs 把 `rootfs.ext2` 挂到 `/overlay`,
   再 bind 挂载 dev/proc/sys/configfs/SDCARD/tmp 后 `chroot /overlay` 运行 MinUI。
   - 在宿主命名空间里**后挂载**的东西(如 `/dev/usb-ffs/ums` 的 functionfs)在 chroot 里看不到,
     必须 `mount -o bind /dev/usb-ffs/ums /overlay/dev/usb-ffs/ums`。
2. **USB gadget 用 configfs**:`/sys/kernel/config/usb_gadget/g1`,UDC 名 `4100000.udc-controller`。
   厂商内核只内置 `gser`、`ffs`,没有 ECM/NCM/RNDIS,需要外置模块。
3. **UDC 必须先解绑再增删 function 链接**(`echo "" > UDC`),否则新 function 不会被 bind
   (现象:`usb0` 不出现 / `(unnamed net_device)`)。
4. **ECM 的 netdev(`usb0`)在 UDC 绑定后才注册**,所以 IP 配置和 DHCP 必须在绑定之后做(`usbnet.sh up`)。
5. **ADB 的 adbd 在宿主侧**(`/bin/adbd -D`),`tomato` 里有个循环保活。

---------------------------------------------------------------------

## 4. 本次(以及前几次会话)的实现步骤与问题解决

### 4.1 USB 网络为什么不能用 FunctionFS 实现
最初想用用户态 FunctionFS 写一个 `netd.elf` 实现 ECM。写描述符时报 `Invalid argument`:
厂商 `f_fs.c` 只接受 HID/CCID(0x21)类特定描述符,拒绝 CDC 的 0x24。
结论:FFS 做不了 ECM,必须用内核自带的 `f_ecm`,于是改为**交叉编译外置模块**。
(`netd.c/.elf` 作废,仅留在 Desktop 备份中。)

### 4.2 外置内核模块的构建环境
- 下载主线 5.4.61 源码(`https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.4.61.tar.xz`),用厂商 `.config`
  并打开 `USB_CONFIGFS_ECM/NCM/RNDIS=m`,`make modules_prepare`(见 `kmod/build.sh`)。
- Docker 镜像 `kbuild-arm`(debian bookworm + gcc-arm-linux-gnueabihf,见 `kmod/Dockerfile`),工作目录挂载为 `/work`。
- 编译:`make KCFLAGS="-march=armv7-a" M=/work/<dir> modules`,然后 `arm-linux-gnueabihf-strip --strip-debug`。
- 模块名加 `_x` 后缀(`u_ether_x.ko`、`f_ecm_x.ko`)。

### 4.3 结构体布局与厂商内核不一致(核心难点)
厂商内核的若干结构体比主线多了字段,外置模块若用主线布局会读错偏移。用法:
反汇编 `vmlinux.bin`(原始镜像,基址 `0xc0008000`):
`arm-linux-gnueabihf-objdump -D -b binary -m arm --adjust-vma=0xc0008000 --start-address=... --stop-address=...`,
和探针模块(`tools/offs2.c`、`skbo.c`、`gp.c`,用 `offsetof` 打印后 `objdump -s -j .rodata` 读出)对比,
然后在 5.4.61 的头文件里加填充:

| 结构体 | 头文件 | 填充 | 发现过程 / 症状 |
|---|---|---|---|
| `struct module` | `linux/module.h` | `char sunxi_pad[36]` 放在 `syms` 之前 | insmod 失败/异常 |
| `struct usb_function_instance` | `linux/usb/composite.h` | `int sunxi_pad` 放在 `fd` 之后 | mkdir function 失败 |
| `struct sk_buff` | `linux/skbuff.h` | `char sunxi_pad[16]` 放在 `headers_end[0]` 之后 | 厂商偏移 tail=168 end=172 head=176 data=180 len=84 data_len=88 mac_header=150;症状:UDC 打印 `_req is invalid`、`buf=NULL`,收发包刷屏 |
| `struct usb_gadget` | `linux/usb/gadget.h` | `char sunxi_pad[16]` 放在 `udc` 之后;`char sunxi_pad2[184]` 放在 `dev` 之后 | **本次最后一个**:`gadget->ep0` 在厂商内核偏移 40(主线 24)→ 读到 NULL → `usb_ep_queue(NULL)` 在中断里崩溃;`mA`/标志位在厂商里是 716/725(+184) |

已确认与主线一致:`net_device`、`usb_function`、`usb_configuration`、`usb_ep`、`usb_request`。
*新增使用其它内核结构体的模块前,务必先用同样方法核对偏移*。

### 4.4 `can_support_ecm()` 失败
厂商 UDC(`sunxi_udc`)设置了 `quirk_altset_not_supp=1`,`ecm_bind` 返回 -EINVAL。
在 `f_ecm.c` 中把 `if (!can_support_ecm(...))` 改成 `if (0 && ...)` 绕过(实测 ECM 正常工作)。

### 4.5 “一启用 ECM 设备就重启”——如何定位
症状:只要 ECM 的中断(INT)通知端点在描述符里,Mac 一枚举设备就复位(无任何崩溃文本)。
- `panic=0`,中断上下文 oops 会一直挂到硬件看门狗复位;DRAM 重启后不保留(测过),`dmesg` 里没有崩溃信息。
- **解法:用 RTC 通用寄存器当“黑匣子”**。RTC GP 寄存器(物理 `0x07090100 + 4*n`)的 gp0-5、gp7
  在 `reboot -f`/看门狗复位后保留(gp6 会被清)。写了 `crashlog.ko`(`tools/crashlog.c`):
  注册 `register_die_notifier` + panic notifier,崩溃时把 `0xdead0000|val`(含 in_interrupt 标志)、
  `regs->ARM_pc`、`ARM_lr`、`ARM_sp`、`thread.address`、`err`、`r0` 写进 gp0-gp7;重启后 `cat /proc/crashlog`。
  (`/proc/crashlog` 写入任意内容 = 清零;`pmem.c` 是更早的手动读写版本。)
- 一次崩溃得到:PC=`c04403d0` = `usb_ep_queue` 第一条指令 `ldrb r3,[r0,#25]`,r0=NULL;
  LR=`bf0a5788`(模块地址,落在 `f_ecm_x` 的 `.text.unlikely` 偏移 0x17c)= `ecm_setup` 里的
  `usb_ep_queue(cdev->gadget->ep0,...)`。→ 追到 `gadget->ep0` 偏移不对(见 4.3 最后一行)。
- 补上 gadget 填充后,**主线原样的 ECM(INT 通知端点 ep4、通知开启)完全稳定**。之前怀疑的“INT 端点本身有问题”“notify 使用后释放”都是误判。
- 经验:这套 `crashlog.ko` 对以后任何“中断里崩溃、无日志”的问题都有用(从 `kallsyms.txt` 反查 PC;模块内地址用 `.ko` 的 `objdump -dr` 加上各 section 大小推算)。

### 4.6 收不到包 / 刷屏问题
- `rx_fill` 失败时会无限 `defer_kevent` 重试并刷爆 dmesg ring buffer(丢掉早期日志)。
  调试期给 `rx_fill` 加了重试上限;**最终干净版用的是主线 u_ether.c,没有这个上限**,
  因为 sk_buff 填充修好后不会再失败。
- 一次测试里 Mac 发出的包设备收不到(RX=1),重启后同样的模块收发都正常 → 当时属于脚本没有按预期启动
  (见 4.9)/状态残留,未发现代码缺陷。

### 4.7 DHCP 与 IP
- 设备 `usb0=192.168.7.1/24`。chroot 内 busybox 没有 `udhcpd`,原厂 `dnsmasq` 依赖原厂 musl 库在 chroot 里跑不了,
  所以写了 `dhcpd_mini.c`(静态极小 DHCP 服务端:对任何客户端发 192.168.7.2,租期 3600s,只下发掩码和服务器地址;
  不给网关/DNS)。
- Mac 上若 VPN(utun)抢路由,ping 169.254.x.x 要用 `ping -b enNN`;有 DHCP 之后用 192.168.7.x 直接可达。

### 4.8 开机钩子(tomato)的两个 bug(需求 B)
1. 先 `setusbconfig adb` 已绑定 UDC,之后再 `ln -sf` 新 function 不会生效 → 链接前先 `echo "" > $G/UDC`
   (UMS 与 ECM 两处都加了)。
2. chroot 里的 `umsd.elf` 报 `open ep0: No such file or directory` → 在 chroot 目录补
   `mount -o bind /dev/usb-ffs/ums ${ROOTFS_MOUNTPOINT}/dev/usb-ffs/ums`。
3. ECM 绑定失败的兜底:检查 `UDC` 为空时删除 `ecm.usb0` 链接再重绑;成功则等 `usb0` 出现→配 IP→chroot 内起 `dhcpd_mini.elf`。
4. `sh -n` 检查语法后再推到 SD;推荐每次改 tomato 都先 `adb shell sh -n /mnt/SDCARD/tomato`。

### 4.9 设备端脚本/调试环境的坑(省得再踩)
- 设备没有 `nohup`/`setsid`。后台脚本用:
  `start-stop-daemon -b -S -m -p /tmp/x.pid -x /bin/sh -- /tmp/script.sh`。
  它**不稳定**:同一时刻第二个 `/bin/sh` 守护通常是空操作,且 pid 文件残留时不启动(先 `rm -f /tmp/x.pid`)。
  脚本里自己再启动日志器,且脚本里不要依赖 adb(解绑 UDC 会断 adb)。
- 改 gadget 配置会断 adb;脚本里要有失败回滚(解绑→删新链接→重绑)。
- **SD 卡被硬重启弄坏会变只读**(`fat_free_clusters: deleting FAT entry beyond EOF` → 文件系统只读,脚本重定向到 SD 失败,日志全丢)。
  修复:通过 USB 把 SD 卡暴露给 Mac(`umsd` 跑 `/dev/mmcblk0`,见 `tools/umsrun.sh`),
  Mac 上 `diskutil repairVolume /dev/diskN`(不需要 sudo),再重启。
  调试日志写 SD 时用预分配文件 + `dd conv=notrunc`(`tools/dmlog.sh`)。
- 设备 `/tmp` 是 tmpfs,重启即清空;Mac 的 `/tmp` 同样可能被清,**已把关键构建资产备份到 Desktop/build_env**。
- Mac 上 `sed -i` 需要 `sed -i ''`。
- Docker 镜像:`kbuild-arm`(内核模块)+ `kbuild-arm2`(在其上加 `libc6-dev-armhf-cross`,用于编译用户态程序;见 `build_env/Dockerfile.arm2`)。

### 4.10 用户态程序(usbmode / umsd / dhcpd_mini)的编译方式
(早前的编译命令没有保存,这次重新摸索出来,**按这个就能复现**)
```
docker run --rm -v /tmp:/t -v <MyMinUI>/workspace:/w kbuild-arm2 bash -c '
cd /t && arm-linux-gnueabihf-gcc -O2 -marm -march=armv7-a -mfpu=neon -mfloat-abi=hard \
  -fomit-frame-pointer -fno-pie -no-pie -nostartfiles -DPLATFORM=\"m21\" -DUSE_SDL2 \
  -I/w/all/common -I/w/m21/platform -I/w/m21/libmsettings -I/t/sdl2_inc -I/t/sdl2_inc/SDL2 \
  usbmode.c api.o platform.o utils.o crt_glibc.c -o usbmode.elf \
  -L/t/m21_glibc -lSDL2 -lSDL2_image -lSDL2_ttf -lmsettings -lsamplerate -lpthread -ldl -lm -lz \
  -l:libc.so.6 -Wl,--dynamic-linker=/lib/ld-linux-armhf.so.3 -Wl,--allow-shlib-undefined
arm-linux-gnueabihf-strip usbmode.elf'
```
要点:
- 必须 `-no-pie -fno-pie -marm`(Debian 默认 PIE;`crt_glibc.c` 的自定义 `_start` 汇编要 ARM 模式)。
- 链接对象是设备 glibc 2.26 的库(`m21_glibc/`),但头文件来自 Debian 的 `libc6-dev-armhf-cross`;`crt_glibc.c` 提供 `_start` 和 `__errno` 适配,避免依赖新版 glibc 符号。
- `api.o/platform.o/utils.o` 是之前编好的 MyMinUI 公共对象(备份在 `build_env`);若要重编,从
  `workspace/all/common/{api,utils}.c` 和 `workspace/m21/platform/platform.c` 编译(参考 `workspace/all/clock/makefile` 的 CFLAGS)。
- 也可以改用仓库自带的 `toolchains/m21-toolchain`(`make PLATFORM=m21 toolchain` 一类流程,Docker 镜像需要先构建;本次没走这条路)。
- `dhcpd_mini` 用相同方式编译,只需 `crt_glibc.c` 和 `-l:libc.so.6`。

---------------------------------------------------------------------

## 5. 部署与回归测试清单(以后改动后照此验证)

1. 编译 → `adb push` 到 `/mnt/SDCARD/Tools/m21/USB Mode.pak/`(`chmod +x`)。
2. 开机钩子:写 `usb_boot_mode.cfg`,`sync`,`adb reboot`,等待 ~60s:
   - `ls /sys/kernel/config/usb_gadget/g1/configs/c.1`(应见 `ffs.adb`、可选 `ffs.ums`、`ecm.usb0`)
   - `cat /sys/kernel/config/usb_gadget/g1/UDC`(应为 `4100000.udc-controller`)
   - `ifconfig usb0`、`ps | grep -E "umsd|dhcpd_mini"`、`cat /tmp/umsd.log`、`/mnt/SDCARD/usb_adb_debug.log`
   - Mac:`networksetup -listallhardwareports | grep -A1 Tina`、`ifconfig enNN`(应有 192.168.7.2)、`ping 192.168.7.1`、`ls /Volumes`
3. 模拟菜单切换(不用点屏幕):见 `tools/ecmtest.sh` 的写法;或 `chroot /overlay sh "<pak>/usbnet.sh" off|on|up` 配合先解绑/后绑定 UDC。
4. 改内核模块前先用探针确认结构体偏移(4.3);崩溃无日志时用 `crashlog.ko`(4.5)。

---------------------------------------------------------------------

## 6. 遗留问题 / 下一步(按优先级)

1. **提交到仓库**:`skeleton/.../USB Mode.pak/` 和 `workspace/m21/other/usbmode/` 目前是 untracked。
   并且把 `tomato.m21` 里的 USB 改动合并到 tomato 的真实源文件(仓库内只找到被忽略的 `build/FULL/m21/tomato`,
   需要确认它由哪个文件/脚本生成:可从 `makefile` 里的 `m21` 目标和 `skeleton/BASE` 追查)。
2. **在设备屏幕上实测菜单**:新增的 USB Network / Boot USB Network 两项、状态栏文案、与 Storage/ADB/Serial 的组合切换;
   以及“绑定失败自动回退”分支(`usbmode.c` 里 `cur_net` 失败路径)。
3. **补测开机模式** `off`(`adb_ums_rw` 已测通过);以及 `adb_ums_ro/rw` + `net` 之外的组合(例如 serial + net)。
4. **端点资源上限**:UDC 只有 ep1/ep2/ep5 三对 bulk + ep4 interrupt + ep3 iso。
   ADB(1 对)+ UMS(1 对)+ ECM 数据(1 对)+ ECM 通知(ep4)刚好用满;再加 `gser`(需要 1 对 bulk + 1 个 INT)大概率放不下,
   需要在菜单里做互斥/提示,或实测。
5. **Windows / Linux 主机**:模块 `f_rndis_x.ko`、`f_ncm_x.ko` 已能用同样方法编译(`kmod/Kbuild` 里已有),
   但没有在设备上加载测过,也没放进 pak、菜单没有入口。RNDIS 还会用到 `rndis.c`,
   上机前要重新核对相关结构体偏移。Linux 主机的 `cdc_ether` 理论可用。
6. **共享上网(NAT)**:目前只有点对点局域网,不下发网关/DNS,也没有转发。需要的话:`dhcpd_mini` 增加 router/DNS 选项 +
   设备侧 `iptables` MASQUERADE(设备上有 iptables 模块)。
7. **DHCP 服务**:只有一个固定租约(192.168.7.2),不管 xid 之外的状态,没有 ARP 检查;多主机场景不适用。
8. **清理**:设备 SD 根目录若有遗留的调试文件(`dm.bin`、`nettest.log` 已删),`/mnt/SDCARD/usb_adb_debug.log` 每次开机会覆盖,无需处理;
   仓库里 `kmod/*_pad.txt` 是完整头文件拷贝(体积大),可改成精简 diff。
9. **构建可复现性**:5.4.61 源码树现在在 `/tmp/kbuild/linux-5.4.61`(会丢)。重建步骤:
   下载源码 → 拷厂商 `device.config` 为 `.config` → 打开三个 `USB_CONFIGFS_*=m` → `make olddefconfig modules_prepare`
   → 按 4.3 表手动加填充(或套用 `kmod/*_pad.*`)→ 编译 `kmod/`。建议后续把填充做成真正的 `.patch` 文件放进仓库。
10. **安全提示**:SD 卡以 RW 暴露给电脑的同时设备自己也在用,有损坏风险;菜单默认退出时会自动弹出存储。硬重启后 SD 若变只读,按 4.9 修复。

---------------------------------------------------------------------

## 6.1 共享上网(后续补充)
- 设备**没有 WiFi 硬件/驱动**(没有 `wlan0`、`/sys/class/ieee80211`、`rfkill`),只有 `usb0`;原厂 `curl 7.54.1` 编译时没有任何协议
  (`curl --version` 的 `Protocols:` 为空),所以 `curl http(s)://…` 一律报 `Protocol not supported`,与网络是否通无关;
  busybox `wget` 在设备上会段错误。测试用 `ping`、`nslookup`、`nc`(adb shell 里有 `nc`,chroot 里没有)。
- 已实现(设备端):`usbnet.sh up` 与 `tomato` 在配好 `usb0` 后 `ip route replace default via 192.168.7.2 dev usb0`,
  并在 `/tmp/resolv.conf.auto`(`/etc/resolv.conf` 的最终目标,chroot 与宿主共用)没有 nameserver 时写入
  223.5.5.5 / 119.29.29.29 / 8.8.8.8。`usbnet.sh off` 删除该默认路由。
- 宿主端(`skeleton/.../USB Mode.pak/host-tools/`):
  - `share-internet-macos.sh on|off|status`:`sysctl net.inet.ip.forwarding=1` + 在 pf anchor `com.apple/250.usbshare` 里加
    `nat on <出口> from 192.168.7.0/24 to any -> (<出口>)`(系统默认 `/etc/pf.conf` 有 `nat-anchor "com.apple/*"`,不会替换主规则集)。
    出口接口自动取 `route -n get 8.8.8.8`;开着 VPN 时是 `utunN`,不通就断开 VPN 或手动指定 Wi-Fi 接口。需要 sudo。
  - `share-internet-linux.sh on|off`:iptables MASQUERADE + FORWARD。
- **验证状态(已在真机跑通)**:Mac 端 `share-internet-macos.sh on` 后,设备 `ping 8.8.8.8`、`ping www.baidu.com` 通;
  `chroot /overlay /bin/busybox wget -qO- http://www.baidu.com` 能取回页面(chroot 里的 busybox `wget` 可用,原厂的会段错误);
  1MB 文件下载 md5 一致。设备自带 `nc` 过于简陋(不发数据),不要用它测 HTTP。
- 已做:菜单里 *USB Network* 一行现在是三态 `OFF / ON (+Internet via PC) / ON (Local only)`(不新增行,避免屏幕放不下)。
  “Local only” 在 `/mnt/SDCARD/.userdata/m21/usb_noshare` 放一个标志文件,`usbnet.sh up` 和 `tomato` 看到它就不设默认路由/DNS(默认无标志=共享)。
  已实测:开机带标志无默认路由、无标志有默认路由;`usbmode.elf` 已重编但菜单屏幕操作未实测。
- 未做:Windows 请用系统自带"Internet 连接共享";
  `dhcpd_mini` **有意**不下发网关/DNS:它服务的对象是宿主机,而设备自己没有外网,下发网关会让宿主机把设备当成默认出口、劫持宿主机的网络。
  设备方向的网关/DNS(指向宿主机)由 `usbnet.sh up` 直接设置,这一项不需要做。

---------------------------------------------------------------------

## 6.2 大帧(>512 字节)收发损坏——已修复(`u_ether.c`)
- **现象**:ping/小 HTTP 正常,但大文件下载超时、`ping -s 600` 以上 100% 丢包;`usb0` 的 `RX dropped`/`errors` 一直涨;
  把 MTU 调到 ≤400 能下载,480 左右出现 md5 不一致,540 以上失败。
- **定位**:给 `rx_complete` 加调试日志(见下),收到的帧目的 MAC 变成 `03:74:ab:40:ce:34`(正确应为 `d2:59:03:74:ab:40`),
  即**内容整体前移了 2 字节**,`eth_type_trans` 得到未知协议被当作 `rx_dropped`。小包走 PIO 没事,大包走 DMA 才出问题。
- **根因**:sunxi UDC 的 DMA 要求 4 字节对齐的缓冲区,而 `u_ether` 默认 `skb_reserve(NET_IP_ALIGN=2)`(RX)以及协议栈给的
  skb->data 偏移 2(TX)都使缓冲区地址不对齐,DMA 从对齐边界开始读写,数据偏移 2 字节。
- **修复**(`kmod/u_ether.c`):RX 强制 `dev->no_skb_reserve = true`;TX 在 `usb_ep_queue` 前若 `skb->data & 3` 则拷贝到对齐的新 skb。
  修复后 1MB 下载 md5 与源一致(两次),`ping -s 1400` 双向通,`RX/TX errors/dropped = 0`。
- **调试方法**:在 `rx_complete` 里对 ethertype 不是 0800/0806/86dd 的帧 `pr_info` 出长度和 MAC;模块放进 pak 的 `modules/` 后重启
  (用 `usb_boot_mode.cfg` 第二行 `net` 开机即带网络)。**不要在 `adb shell` 里直接解绑 UDC**(adb 自己就跑在这个 gadget 上,脚本会被杀掉,
  设备会一直处于未绑定状态需要重启);需要运行时切换就用 `(trap "" HUP; script &)` 放到后台。设备上没有 `setsid`/`nohup`。
  也不要在运行中 `rmmod f_ecm_x`/`u_ether_x`——会导致无日志的硬复位(`crashlog.ko` 也记不到),直接替换 `.ko` 后重启。

---------------------------------------------------------------------

## 6.3 OTG 主机模式 / U 盘(后续补充)
- **Type-C 口不输出 5V(硬件/设备树限制)**:`usbc0` 节点 `usb_port_type=0`(纯设备口)、没有 `usb_id_gpio`、`usb_det_vbus_gpio`,
  整个设备树里没有 `usb_drv_vbus_gpio`;`ehci0/ohci0` 的 `drvvbus-supply` 指向 `usb1-vbus`,但它是没有 `gpio` 属性的
  `regulator-fixed`(空壳)。实测切到主机模式只是该稳压器 users 计数 +1,没有任何引脚动作,转接器指示灯不亮。
  (板上是否有未在设备树里声明的升压电路未排除;要确认需读 GPIO 寄存器或量 VBUS。)
- **带外部供电的 hub 可用**:`usbhost_test.sh`(本目录,拷到 SD 卡后后台运行,会切 host→等待→切回 device)实测:hub(2148:7022)、
  SanDisk U 盘(0781:5591)被识别为 `sda/sda1`(FAT32,28.6GiB);hub 上的 Realtek RTL8152 USB 网卡(0bda:8152)也被枚举,
  但内核没有对应驱动(`CONFIG_USB_NET_DRIVERS` 未开,可考虑补编 `r8152` 做有线网络)。内核有 usb-storage、SCSI 磁盘、vfat、exfat、ntfs(只读驱动),没有 ext4。
- **原厂 rootfs 会自动挂载 U 盘**:`/etc/hotplug.d/block/10-mount` + `/etc/config/fstab` 把 `/dev/sda`、`/dev/sda1` 以 `rw,async`
  挂到 `/mnt/exUDISK`(在 chroot 之外)。所以手动 `mount -o ro` 会得到 `Resource busy`(同一设备已 rw 挂载);必须同样 rw 挂载才能共享。
- **USB Host 二级页面(`usbhost.h` + `usbmode.c: run_host_page`)**:主菜单的 *USB Host (OTG)* 一行按 A 进入二级页:
  - 第一行 `Host mode: < ON/OFF >`(左右/A 切换)。ON = 解绑 UDC、停 `umsd`、读 `usb_null`/`usb_host` 切 OTG 角色(**同一个 Type-C 口,
    ADB/USB gadget 在 Host 开着时不可用**);OFF = 先弹出所有 U 盘,再恢复 device 角色并重绑 gadget(ADB 回来)。
  - Host ON 后每秒枚举 `/sys/bus/usb/devices`,列出所有 USB 设备:`[Storage]/[Hub]/[Input]/[Network]/[Audio]/[Internal]/[Other] 名称`,
    存储设备后面显示 `reading... / mounting... / mounted (xxx MB) / mount failed`。机内手柄(ehci1/ohci1 上的 Twin USB JoysticK)标 `[Internal]`。
  - 光标移到设备行,底部显示详情(总线路径、VID:PID、速度、`sdXn:fs -> 挂载点` 或错误)。
  - 存储设备自动挂载:对该盘每个分区(无分区表则整盘)识别文件系统(读首扇区:FAT/exFAT/NTFS;ext2/3/4 提示不支持),
    发现 3 秒后(等原厂 hotplug 先挂)以 rw 挂到 `/mnt/SDCARD/USB/<sdXn>`(chroot 内外都可见;**必须 rw**,原因见上),失败每 2 秒重试,最多 5 次;
    NTFS 内核驱动只读,rw 失败时退回只读。
  - 在存储设备行按 A:已挂载 → 安全弹出(`sync`、`umount`、`rmdir`、`echo 1 > /sys/block/sdX/device/delete`);未挂载 → 重试挂载。
  - 拔盘:块设备消失后自动 `umount -l` 并清理挂载点。离开 Host(切 OFF、或在主菜单改 Storage/ADB/Serial/Network)都会先弹出所有盘。
  - **退出 pak 后 Host 仍保持**:Host ON 时会起一个后台守护进程 `usbmode.elf --host-daemon`(pid 文件 `/tmp/usbhost.pid`,状态文件 `/tmp/usbhost.state`,
    UI→守护进程命令文件 `/tmp/usbhost.cmd`)。守护进程每秒枚举设备并自动挂载/清理,**只要端口还在 host 角色就一直运行**(角色变回 device 时自行退出并弹出所有盘);
    UI 页面只读状态、发 `eject/retry` 命令。重新进入 pak 时 `detect_current_state()` 读 `otg_role` 还原 Host 状态
    (旧版本漏了这一步,所以退出重进总显示 device,实际角色并没有被改)。
  - 命令行自测(无需屏幕):`usbhost_selftest.sh <秒数>`(在 pak 目录,后台运行)→ 切 host、每 2 秒列设备/自动挂载并打印前 12 行文件列表 →
    弹出并恢复 device,日志 `/mnt/SDCARD/usbhost_selftest.log`。**无 hub 的空跑已通过**(枚举到机内手柄、切换/恢复正常);
    带 hub + U 盘的挂载、弹出、屏幕页面**尚未实测**。
- 注意:`/mnt/exUDISK`(原厂挂载点)在 chroot 外;弹出后原厂那个挂载会残留成失效挂载(chroot 里清不掉),不影响我们的 `/mnt/SDCARD/USB/*`。
  默认 `rw,async`,不弹出直接拔盘有损坏风险。
- 不支持/待做:ext4 U 盘(可考虑用已有的 fuse2fs)、NTFS 读写(原厂 `ntfs-3g` 是 musl 版,在 glibc chroot 里跑不了)、RTL8152 有线网卡驱动、
  磁盘卷标显示、多 hub 层级缩进显示。

---------------------------------------------------------------------

## 7. 速查

- 进设备:`adb shell`;设备 `usb0=192.168.7.1`;Mac 端网卡名通常是 `en16`("Tina ADB"),随接口变化。
- 读崩溃黑匣子:`insmod /tmp/crashlog.ko; cat /proc/crashlog`(PC 反查:`tr -d '\r' < kallsyms.txt | sort`,找 ≤PC 的最近符号)。
- 反汇编厂商内核:见 4.3 的 objdump 命令(在 `kbuild-arm` 容器里跑,Mac 没有 arm objdump)。
- 重新给 gadget 加链接的标准顺序:`echo "" > UDC` → 增删 `configs/c.1/*` 链接 → `echo 4100000.udc-controller > UDC` → 校验 UDC 非空 → 配 IP。


### ext4 U 盘(fuse2fs)
- 内核没有 ext4(`CONFIG_EXT4_FS` 未开),但原厂侧 SD 卡上自带 musl 版 `fuse2fs`(1.43.7)和库(`/mnt/SDCARD/m21/{binmusl,libmusl}`),
  /overlay 本身就是用它挂的 `rootfs.ext2`,`fuse.ko` 已加载。
- chroot 里的 glibc 库与 musl 冲突,所以守护进程通过 init 的根目录起 stock loader:
  `/proc/1/root/lib/ld-musl-armhf.so.1 --library-path /mnt/SDCARD/m21/libmusl:/proc/1/root/usr/lib:/proc/1/root/lib /mnt/SDCARD/m21/binmusl/fuse2fs <dev> <mnt> -o rw,noatime`(失败回退 ro)。
  用 64MB ext4 镜像在设备 chroot 内实测读写正常。
- 限制:fuse2fs 1.43 不写日志(journal);需要日志恢复的盘会挂载失败(界面显示 fuse2fs 的错误行),需在电脑上 fsck。

### 某些 U 盘读取报错 / 读到乱数据(hostbyte=0x07)
- 现象:58.5GB 三分区"Mass Storage Device"(FAT/ext?/NTFS)经 hub 接入,`dmesg` 出现大量 `UNKNOWN(0x2003) hostbyte=0x07`、
  `usb 3-1.1: reset high-speed USB device ... using sunxi-ehci`,失败的 READ(10) 都是 16KB/32KB(phys_seg 3~8,多段 scatter-gather);
  探测 `/dev/sda2` 偶尔读到 `USBC` 开头的 CBW 数据(缓冲区被 CBW 污染)。小块读(分区表、FAT 引导)正常。
- 推测:sunxi-ehci 对多段批量传输处理有问题。对策:守护进程在发现磁盘时把 `/sys/block/<sd>/queue/max_sectors_kb` 压到 4,
  让每个请求只含一个物理段,并且重试挂载时 `sync; echo 3 > /proc/sys/vm/drop_caches` 丢弃被污染的缓存页。
- 探测失败时会把分区头 2KB 十六进制写到 `/mnt/SDCARD/usbprobe.txt`,便于离线分析。


## 7. Windows 支持:CDC-ACM 串口 + NCM/RNDIS 网络(本次)
- **为什么 Windows 显示没驱动**:`gser` 是厂商自定义类(0xFF),Windows 只对标准 CDC-ACM 自动用 `usbser`;ECM Windows 没有内置驱动。
  内核没开 ACM/NCM/RNDIS,按 4.2 的方法交叉编译 `f_acm_x.ko`(`f_acm.c` 主线原样)、`f_ncm_x.ko`(同样绕过 `can_support_ecm`,见 `kmod/f_ncm.patch`)、
  `f_rndis_x.ko`(`f_rndis.c`+`rndis.c` 原样)。重建 5.4.61 源码树:下载 → 套 `kmod/*_pad.*` 与新的 `netdevice_h_pad.patch` → `kmod/build.sh`
  → `make M=<含 kmod 源码与 f_acm/f_ncm/f_rndis/rndis.c 的目录>`。
- **新发现的结构体问题(`netdev_priv` 偏移)**:厂商 `alloc_netdev_mqs` 里 `ALIGN(sizeof(struct net_device))=1600`,主线是 1344,而模块里的 `netdev_priv()` 是内联常量
  → `struct eth_dev` 实际叠在厂商 net_device 的尾部字段上(`rtnl_link_ops`在 1412 等)。ECM 一直"碰巧能用"(叠到的位置写的是 0),NCM/RNDIS 会把 `wrap/unwrap` 函数指针写到 `rtnl_link_ops`,
  `ifconfig usb0 up` 时在 `if_nlmsg_size → strlen` 里 oops(`strlen(e59f337c)`,r4 指向 `ncm_unwrap_ntb`)。修法:给 `struct net_device` 末尾加 256 字节填充,让 priv 偏移=1600。
  **所有网络模块(含 ECM、u_ether)都已用新头文件重编**。
- **RNDIS 要点**:`f_rndis` 默认 IAD 类是 02/ACM/vendor,Windows 不认;要设 `functions/rndis.usb0/{class,subclass,protocol}` = `e0/01/03`。注意这几个 configfs 属性用 `sscanf("%02hhx")` 解析,
  必须写 `e0`,写 `0xe0` 会得到 0。设备描述符类设为 `EF/02/01`(IAD 组合设备)。
- **接口名**:每个注册过的网络功能占一个 `usbN`(ecm→usb0,之后切 ncm→usb1…),`usbnet.sh up` 读 `functions/<type>.usb0/ifname` 并把别的 `usbN` 清 IP 后 down,DHCP 绑定到正确的接口。
- **端点限制**:ACM/ECM/NCM/RNDIS 都需要 1 个中断端点(ep4),UDC 只有一个,所以串口和网络互斥;菜单里开其中一个会自动关另一个。ADB+存储+网络仍可共存。
- **已验证(Mac)**:`f_acm_x` 出现 `/dev/cu.usbmodemXXXX`,双向收发正常;NCM 自动获取 192.168.7.2、ping 1400 字节、8000 字节分片正常;ECM 回归正常;
  开机路径(`usb_nettype`=ncm / rndis)均已重启验证。RNDIS 在 Mac 上只能验证枚举和描述符(类 e0/01/03、设备类 EF/02/01),**真正的 Windows 驱动绑定待用户验证**。
- **测试小技巧**:从 adb 里改 gadget 要先 `echo "" > UDC`,脚本必须后台运行且**用 `usleep` 而不是 `sleep`**(解绑后 stock 的 `sleep` 会被连带杀掉,脚本中途消失);用 `(trap "" HUP; sh script &)` 启动。

## 7.1 RNDIS 在 Windows(11)上的真正条件——已验证
排查过程:设备管理器先后出现“基于远程 NDIS 的 Internet 共享设备”(class E0/01/03 路线)和“远程 NDIS 兼容设备”(MS OS 兼容 ID 路线),都是**代码 10**,
设备端 `rndis_setup` 一次都没被调用(Windows 在 SET_CONFIGURATION 之后对设备完全没有请求)。最终有效的修改:
1. **RNDIS 必须是配置里的第一个功能(接口 0/1)**:`ffs.adb` 在前(RNDIS 在 1/2)必代码 10;RNDIS 在前立刻正常(Android 的 rndis,adb 也是这个顺序)。
   configfs 的接口号按 `configs/c.1/` 里**创建软链接的顺序**分配,所以 `usbnet.sh on rndis` 和 `tomato` 在建 `rndis.usb0` 链接前先删掉 ffs/acm 链接,建完 RNDIS 再按原样补回。
2. `f_rndis.c` 主线把 CDC Call Management 描述符的 `bDataInterface` 写死为 1,RNDIS 不在接口 0/1 时与 Union 描述符不一致,已改为实际数据接口号(`kmod/f_rndis.patch`)。
3. 走 MS OS 兼容 ID 路线(`os_desc`: `use=1`、`b_vendor_code=0xcd`、`qw_sign=MSFT100`、`rndis.usb0/os_desc/interface.rndis/compatible_id=RNDIS`、`sub_compatible_id=5162001`,
   并 `ln -s configs/c.1 os_desc/c.1`),函数 class/subclass/protocol 保持默认 02/02/ff;`bcdDevice` 改为 0x0511(Windows 按 VID/PID/REV 缓存“有无 MS OS 描述符”)。其它模式 `osdesc_off` 恢复 0x0504。
- 注意:`dhcpd_mini.elf` 是 glibc 程序,只能在 `chroot /overlay` 里运行。从 `adb shell` 直接跑 `usbnet.sh up` 会报 `/tmp/dhcpd_mini.elf: not found`(Windows 会得到 169.254);菜单里的操作在 chroot 内,没问题。
  测试时用 `chroot /overlay /tmp/dhcpd_mini.elf usb0 192.168.7.1 192.168.7.2`(先把文件拷到 `/overlay/tmp`)。`dhcpd_mini` 现在把收发的 DHCP 报文写到 stderr(即 `/tmp/dhcpd.log`)。
- 调试手段:在 `f_rndis.c` 的 `rndis_setup`/`rndis_command_complete`/`rndis_set_alt` 里加 `pr_info`;UDC 的 `queue_debug`、`irq_debug`(`/sys/devices/platform/soc@3000000/4100000.udc-controller/`)写 1 可看 ep 队列和中断;
  Mac 上用 libusb 可以读配置描述符、MS OS 字符串/兼容 ID,并手动发 RNDIS INIT(`SEND/GET_ENCAPSULATED_*`)验证设备端控制通路。

## 7.2 Windows 共享上网
- 现象:Windows 上 RNDIS 能拿到 192.168.7.2,`ping 192.168.7.2` 通,但设备 `ping 8.8.8.8`/`www.baidu.com` 不通——与 Mac 上没开 NAT 时一样,宿主机必须做 NAT。
- 不用 Windows 自带 ICS:它会把适配器强行改成 192.168.137.1 并自带 DHCP,与设备 192.168.7.x(设备默认路由指向 192.168.7.2)冲突。
- `host-tools/share-internet-windows.ps1 on|off|status`(`.bat` 双击会自动要管理员权限):自动找 192.168.7.x 的适配器和有默认路由的出口,
  两者 `Set-NetIPInterface -Forwarding Enabled`,再 `New-NetNat -Name USBShare -InternalIPInterfaceAddressPrefix 192.168.7.0/24`。
  可能的失败:与 WSL/Docker/Hyper-V 的 NAT 网段重叠、`winnat` 服务未运行、出口是 VPN 虚拟网卡(用 `-Uplink` 指定)。**该脚本未在真机 Windows 上验证。**
- **实测(Windows 家庭版)**:`New-NetNat` 报“无效类”(缺少 NetNat 的 WMI 类)。脚本因此自动回退到 ICS:
  先写注册表 `HKLM\SYSTEM\CurrentControlSet\Services\SharedAccess\Parameters` 的 `ScopeAddress`/`ScopeAddressBackup`/`StandaloneDhcpAddress`=192.168.7.2(默认 192.168.137.1),
  停 `SharedAccess` 服务后用 `HNetCfg.HNetShare` COM 接口给上网网卡 `EnableSharing(0)`、给 USB 网卡 `EnableSharing(1)`,USB 网卡会被设成静态 192.168.7.2,与设备默认网关一致。
  `off` 会关闭共享、删掉这三个注册表值、把 USB 网卡改回 DHCP。**ICS 回退方式尚未在真机验证。**
