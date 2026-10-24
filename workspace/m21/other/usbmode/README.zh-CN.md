# USB Mode.pak (m21 / Allwinner H133, Tina 5.4.61 厂商内核)

> English (主文档): [README.md](README.md)

功能项：USB Storage (OFF/RW/RO)、ADB、USB Serial (CDC-ACM)、USB Network (OFF / +Internet / local)，带 Network Type 选择（ECM = Mac/Linux，NCM = Mac/Linux/Win11，RNDIS = Windows 10/11；RNDIS 在配置中排第一位并使用 MS OS compatible-ID 描述符）、USB Host (OTG) 子页面、Boot Defaults 子页面（复选框：ADB / Storage / Serial / Network + 类型）；USB Network 子页面（模式、类型、各 OS 的「How to connect」说明）。

Serial 与 Network 互斥：UDC 只有一个中断端点。开启任一功能时，设备类设为 EF/02/01 (IAD)，Windows 才会绑定内置驱动（ACM 用 usbser，RNDIS 用 usb8023）。

功能、问题/根因与待办项汇总见 `FEATURES_AND_ISSUES.zh-CN.md`。

## 文件
- `usbmode.c`   UI（链接 MinUI api/platform/utils，glibc-2.26 启动桩见 `crt_glibc.c`）
- `umsd.c`      FunctionFS 大容量存储守护进程
- `dhcpd_mini.c` `usb0` 单租约 DHCP 服务（192.168.7.1 -> 192.168.7.2）
- `usbnet.sh`   usbmode 调用的 `on [ecm|ncm|rndis]|off|up|serial on|off` 辅助脚本（insmod、function、ifconfig、DHCP）
- `tomato.m21`  开机钩子；读取 `.userdata/m21/usb_boot_mode.cfg`
  （第 1 行：`adb_only|adb_ums_rw|adb_ums_ro|off`，可选第 2 行：`net`）；网络类型在 `.userdata/m21/usb_nettype`
- `kmod/`       树外模块 `u_ether_x.ko` / `f_ecm_x.ko` / `f_ncm_x.ko` / `f_rndis_x.ko` / `f_acm_x.ko`（主线 5.4.61；
  改动：sunxi UDC 设置 `quirk_altset_not_supp`，ECM/NCM 中绕过 `can_support_ecm()`（`f_ncm.patch`），
  `u_ether.c` DMA 对齐修复）

## 编译模块
厂商内核未配置 ECM/NCM/RNDIS，因此用厂商 `.config` 配置的主线 5.4.61 树树外编译（`kmod/build.sh`）。厂商 struct 布局与主线不同，在该构建树头文件中增加了 padding 字段：

| struct                      | header               | pad                                        |
|-----------------------------|----------------------|--------------------------------------------|
| `struct module`             | `linux/module.h`     | `syms` 前 `char sunxi_pad[36]`         |
| `struct usb_function_instance` | `linux/usb/composite.h` | `fd` 后 `int sunxi_pad`          |
| `struct sk_buff`            | `linux/skbuff.h`     | `headers_end[0]` 后 `char sunxi_pad[16]`|
| `struct usb_gadget`         | `linux/usb/gadget.h` | `udc` 后 `char sunxi_pad[16]`，`dev` 后 `char sunxi_pad2[184]` |
| `struct net_device`         | `linux/netdevice.h`  | 末尾 `char sunxi_tail_pad[256]`（`netdevice_h_pad.patch`）：厂商 `netdev_priv()` 从 1600 起，主线 1344。不加则 `struct eth_dev` 会叠在厂商 net_device 字段上；ECM 恰好能工作是因为那里 `wrap`/`unwrap` 为 NULL。NCM/RNDIS 会设置它们并在 `if_nlmsg_size`（`dev->rtnl_link_ops`）里 oops。 |

（`kmod/` 下 `*_pad.*` 文件为具体改动。）布局错误会表现为 IRQ 上下文里的 NULL 解引用。

编译：`make KCFLAGS="-march=armv7-a" M=<含 kmod/* 的目录> modules`，然后 `strip --strip-debug`。

## 说明
- 增删 function 链接前必须先解绑 UDC（`echo "" > UDC`），否则新 function 不会被绑定（没有 `usb0`）。
- macOS/Linux 宿主：宿主经 DHCP 拿到 192.168.7.2，设备为 192.168.7.1。

## 通过宿主共享上网
`usbnet.sh up`（以及开机时 `tomato`）会设置 `default via 192.168.7.2`，并在缺失时写入 `/tmp/resolv.conf.auto` 的 DNS。
宿主必须做 NAT：`host-tools/share-internet-macos.sh on`（sudo，pf anchor `com.apple/250.usbshare`）或
`share-internet-linux.sh on`，Windows 上用 `share-internet-windows.bat`（管理员；WinNAT 经 `New-NetNat`，不用会把地址强制成 192.168.137.1 的 ICS——真机未测）。然后在设备上：`ping -c3 8.8.8.8`、`ping -c3 www.baidu.com`，
`printf 'GET / HTTP/1.0\r\nHost: www.baidu.com\r\n\r\n' | nc www.baidu.com 80 | head -5`（adb shell；自带 curl 无任何协议）。

`kmod/f_rndis.patch`：Call Management 的 `bDataInterface` = 真实数据接口（主线写死为 1）。`kmod/f_ncm.patch`：`can_support_ecm` 绕过。
