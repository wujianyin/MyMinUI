# MyMinUI-M22Pro

> English summary: [README.en.md](README.en.md)

这是 [Turro75/MyMinUI](https://github.com/Turro75/MyMinUI) 的 fork,后者又是 [shauninman/MinUI](https://github.com/shauninman/MinUI) 的 fork。
上游的完整说明保留在 [README.upstream.md](README.upstream.md)(支持设备列表、功能、安装方法都在里面),这里只记录**本 fork 自己的目标、改动和进展**。

## 目标

后续开发会**专注于 SJGAM M22Pro**(Allwinner H133 / `sun8iw20`,256 MB 内存,上游里归在 `m21` 平台)这一台设备:
- 把它当作一台可以随时调试、可以扩展的掌机来用(USB 网络、存储、调试口等);
- 沉淀这台设备的底层资料(原厂固件、分区、启动流程、内核/USB 栈的差异),方便后来者;
- 和上游保持同步,通用改进尽量回馈上游,设备相关的改动留在本 fork。

其它平台的代码保持上游原样,本 fork 没有对它们做过测试。

## 设备资料

- 上游对 m21 平台的适配现状、时间线和已知问题总结:**[M21_STATUS.md](M21_STATUS.md)**(依据上游 README、Releases、提交、Issues/PR)。

- TF 卡损坏后的恢复清单(哪些文件从仓库恢复、哪些恢复不了):**[SD_RESTORE.md](SD_RESTORE.md)**。
- 内屏 60Hz 补丁(已在 M22Pro 上验证)和 FEL 救砖流程:**[workspace/m21/other/dtb_60fps/](workspace/m21/other/dtb_60fps/RISK_ASSESSMENT.md)**。

- 原厂固件镜像(SPI NOR 全部分区)和分析文档:**[firmware/sjgam-m22pro/](firmware/sjgam-m22pro/)**,
  分析见 [ANALYSIS.md](firmware/sjgam-m22pro/ANALYSIS.md)。
- 要点:Linux 5.4.61 + musl 的 Tina Linux,16 MiB SPI NOR,系统只读且已满;MinUI 通过 `/usr/tomato` 钩子从 SD 卡启动,
  装坏了只要恢复 SD 卡内容即可,不会变砖。

## 本 fork 做了什么

### 1. USB Mode.pak(`Tools/m21/USB Mode.pak`)
一个交互式 USB 管理工具,可以在设备上直接切换 USB 功能(无需重启):

| 功能 | 说明 | 状态 |
|---|---|---|
| USB Storage | 把 SD 卡作为 U 盘暴露给电脑,可选读写 / 只读(用户态 FunctionFS 实现 `umsd`) | ✅ Windows、Mac 均可挂载读取 |
| ADB | 开关 adb | ✅ 已测;⚠ 偶发:切换 USB Network/Host 等开关后 adb 失效,需关再开 |
| USB Serial | CDC-ACM 串口(`ttyGS0` + 登录 shell),Windows/macOS/Linux 免驱 | ✅ 已测(Windows 识别为 COM 口并可进入 shell;含开机勾选串口) |
| **USB Network(二级菜单)** | ECM / NCM / RNDIS 以太网,设备 `192.168.7.1`,电脑通过内置 DHCP 得到 `192.168.7.2`;菜单里带 Windows/macOS/Linux 的“如何连接”说明页 | ✅ macOS(ECM/NCM)含 ping www.baidu.com 正常;Windows(NCM、RNDIS)可识别,但共享后 ping www.baidu.com 仍有问题;✅ 设备屏幕显示和二级菜单已确认 |
| **共享宿主机上网** | 设备端:USB 网络启动后自动加默认路由 `192.168.7.2` 和 DNS;宿主端用 pak 里 `host-tools/` 的 `share-internet-macos.sh` / `share-internet-linux.sh` / `share-internet-windows.bat` 开 NAT | ✅ macOS 实测可上外网;🟡 Windows(`New-NetNat`,不可用时自动回退 ICS)8.8.8.8 通,域名解析卡住待查;Linux 脚本未真机验证 |
| OTG 角色 | Device(连电脑)/ Host(接手柄、U 盘) | ✅ 已测 |
| **USB Network 三态** | OFF / ON(设备经 PC 上外网) / ON(仅本地,不设默认路由/DNS) | ✅ 脚本逻辑已实测;✅ 屏幕菜单显示已确认 |
| **Boot Defaults(二级菜单)** | 勾选开机默认开启的 USB 功能:ADB、存储(关/RW/RO)、串口、网络(及网络类型);串口与网络互斥 | ✅ `adb_only`、`adb_ums_ro`、`adb_ums_rw`、`adb_only`+`net`、`adb_only`+`serial` 已重启实测;🟡 `off` 和“开机关闭 ADB”未测(会关闭 USB,需拔 SD 卡恢复配置) |

实现位置:
- pak 与二进制:`skeleton/EXTRAS/Tools/m21/USB Mode.pak/`
- 源码、内核模块、`tomato` 补丁、详细工作记录:`workspace/m21/other/usbmode/`
  - [README.md](workspace/m21/other/usbmode/README.md) 简介
  - [NOTES.md](workspace/m21/other/usbmode/NOTES.md) **完整工作记录**:踩过的坑、解决步骤、剩余问题
- 厂商内核没有编译 ECM/NCM/RNDIS,所以 USB 网络用的是**外置内核模块**(`u_ether_x.ko`、`f_ecm_x.ko`),
  对 5.4.61 头文件加了若干结构体填充来匹配厂商内核布局。

### 2. 开机钩子 `tomato` 的修复
`workspace/m21/other/usbmode/tomato.m21` 是设备上 `/mnt/SDCARD/tomato` 的当前版本,相对旧版修了几个 USB 相关问题:
链接 function 前必须先解绑 UDC;chroot 里要 bind 挂载 `/dev/usb-ffs/ums`;ECM 绑定失败时自动回退。
开机配置文件 `/.userdata/m21/usb_boot_mode.cfg`:第 1 行 `adb_only | adb_ums_rw | adb_ums_ro | ums_rw | ums_ro | off`,第 2 行 `net` 或 `-`,第 3 行 `serial` 或 `-`(旧的 1~2 行格式仍兼容)。

## 使用方法(简版)

1. 按上游 [INSTALL.md](INSTALL.md) 准备好 `m21` 的 MinUI。
2. 把 `skeleton/EXTRAS/Tools/m21/USB Mode.pak/` 整个复制到 SD 卡 `Tools/m21/` 下。
3. 如需"开机默认 USB 模式",用 `workspace/m21/other/usbmode/tomato.m21` 里的 USB 部分更新 SD 卡根目录的 `tomato`
   (先对照你现有的 `tomato`,**不要直接覆盖**,并用 `sh -n tomato` 检查语法)。
4. 在 MinUI 的 Tools 里打开 *USB Mode*。USB 网络:电脑端出现一块 "Tina ADB" 以太网卡,自动获得 `192.168.7.2`,`ping 192.168.7.1`。
5. 想让设备上外网:先把 pak 里的 `host-tools/` 脚本拷到电脑(打开 USB Storage 后在 SD 卡 `Tools/m21/USB Mode.pak/host-tools/` 里复制,或 `adb pull`),再运行:macOS `sudo sh share-internet-macos.sh on`,Linux `sudo sh share-internet-linux.sh on`,Windows 双击 `share-internet-windows.bat`(管理员;优先 `New-NetNat`,系统不支持时自动回退到 ICS 并把内网地址改成 192.168.7.2)。
   然后在设备 `adb shell` 里 `ping -c3 8.8.8.8`、`ping -c3 www.baidu.com`。用完把 `on` 换成 `off` 关闭。切换 Network Type 后 Windows 会出现新网卡,需要重新运行 `on`。USB Mode → USB Network → How to connect 里有三个系统的逐步说明。

## 已知问题 / 待办

**完全不可用(依据实测):** 设备自带 `curl`(http/https 都不行,编译时无协议);`wget` 访问 https;Windows 下的 ECM 网络类型(无驱动,请用 NCM/RNDIS);macOS 下的 RNDIS(无驱动,请用 ECM/NCM)。
**可恢复:** ADB 偶发在切换其他开关后失效,在菜单里把 ADB 关掉再打开即可。
**部分可用:** Windows 共享网络后能 `ping 8.8.8.8` 但域名解析有问题(待查)。完整状态表见 `workspace/m21/other/usbmode/FEATURES_AND_ISSUES.md` §0。

- ⚠️ **Type-C 口本身不对外供 5V**(设备树无 VBUS 控制,转接器灯不亮);OTG 主机模式必须用**带外部供电的 hub**。实测 hub + FAT32 U 盘可被识别(`sda1`),系统会自动把它挂到 `/mnt/exUDISK`;USB Mode 里新增 *USB Host (OTG)* 二级页:开关 Host、列出所有 USB 设备、存储设备自动挂载到 `/mnt/SDCARD/USB/<sdXn>` 并可安全弹出(带供电 hub 实测:FAT32、EXT4、NTFS 的 U 盘可挂载读取,其他 USB 设备可识别)。详见 NOTES.md §6.3。
- 🟡 菜单显示和二级菜单已在设备屏幕确认;`off` 开机模式和“开机关闭 ADB”组合未测。
- 🟡 USB 网络:macOS(ECM/NCM)和 Windows(NCM、RNDIS)已实测;Windows 共享上网后域名解析卡住、Mac ECM 大包丢包待查,详见 `workspace/m21/other/usbmode/FEATURES_AND_ISSUES.md`。
- ℹ️ USB 端点有限(3 对 bulk + 1 个 interrupt):串口与网络互斥,菜单(主菜单和 Boot Defaults)会自动关掉另一个并提示;ADB + 存储 + 网络可共存。
- 🟡 设备本身**没有 WiFi 硬件/驱动**,上外网只能靠宿主机共享(Mac 上已实测可上外网);设备端自带的 `curl` 编译时没有任何协议(`Protocols:` 为空),
  `https`/`http` 都会报 `Protocol not supported`,自带 `nc` 也不发数据;`wget` 只能访问 http,https 不行(用户实测);测试请用 `ping` 或 `wget` 访问 http(见 NOTES.md)。
- ✅ 已修复:USB 网络大帧(>512 字节)收发损坏——原因是 sunxi UDC 的 DMA 要求 4 字节对齐,`u_ether` 的 `NET_IP_ALIGN` 让数据偏移了 2 字节(NOTES.md §6.2)。
- ℹ️ `dhcpd_mini` 只发一个固定租约(192.168.7.2),**有意**不下发网关/DNS(它服务的是宿主机,下发会劫持宿主机默认路由);设备端的默认路由/DNS 由 `usbnet.sh up`(和 `tomato`)直接设置。
- 🟡 仓库里 `tomato` 的真实来源文件还没找到(构建目录 `build/FULL/m21/tomato` 被 `.gitignore` 忽略),
  目前 `tomato.m21` 只是设备上那一份的拷贝,需要合并回真正的来源。
- 🟡 pak 还没有接入 `workspace` 的 makefile,`.elf`/`.ko` 是预编译后直接提交的;编译方法见 NOTES.md。
- ⚠️ SD 卡以读写方式暴露给电脑的同时设备自己也在用它,有损坏风险;硬重启可能让 SD 变成只读(修复办法见 NOTES.md)。
- ⚠️ 厂商内核的若干结构体与主线 5.4.61 不一致,新增外置模块前必须先核对偏移(NOTES.md 有方法和对照表)。

## 目录速览

```
firmware/sjgam-m22pro/            原厂固件镜像 + 分析文档
skeleton/EXTRAS/Tools/m21/USB Mode.pak/   USB Mode pak(预编译)
workspace/m21/other/usbmode/      USB Mode 的源码、内核模块、tomato 补丁、NOTES.md
README.upstream.md                上游 README(原文)
```

## 致谢与许可

- 上游:[shauninman/MinUI](https://github.com/shauninman/MinUI)、[Turro75/MyMinUI](https://github.com/Turro75/MyMinUI),以及上游列出的所有贡献者。
  本 fork 沿用上游的许可条款(见上游仓库)。
- `firmware/` 下的原厂固件版权归 Allwinner 及设备厂商所有,仅供研究、备份和兼容性开发;使用风险自负,**不要刷写 SPI NOR**。
- 本项目与 SJGAM、Allwinner、MinUI 原作者无任何隶属关系。
