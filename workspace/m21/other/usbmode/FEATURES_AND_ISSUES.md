# USB Mode.pak(m21)新增功能与问题点总结

范围:2026-10-19 ~ 2026-10-20 这一轮开发(USB Host、ext4、串口/网络的 Windows 支持、共享上网脚本、诊断工具、菜单二级化)。
细节与调试过程见 `NOTES.md`(§4–§7),使用说明见 `README.md`。

## 1. 新增功能一览

### 1.1 USB Host(OTG 主机)子页面
| 功能 | 说明 |
|---|---|
| 主机模式持久化 | 退出 pak 后主机模式不再被关掉(后台守护进程 `usbmode.elf --host-daemon`),重新进入时按真实角色(读 `otg_role`)显示 |
| 设备列表 + 存储自动挂载 | 枚举 USB 设备;U 盘分区挂到 `/mnt/SDCARD/USB/<sdXn>`(文件管理器可直接看到) |
| 存储行 A 键动作菜单 | Eject / Mount-retry / Details / Cancel |
| 非破坏性弹出 | 弹出后设备仍挂着(不再 SCSI delete),界面显示 "ejected (safe to unplug)",可从菜单重新挂载,无需重新插拔;同时释放系统自动挂载的 `/mnt/exUDISK` |
| 任意设备的详情页 | 类型、VID:PID、端口、速度、USB 版本、厂商、供电、序列号、接口/驱动、挂载点 |
| ext2/3/4 U 盘 | 通过原厂 `fuse2fs` 挂载(先 rw 再 ro 兜底);vfat/exfat/ntfs(只读)走内核 `mount()` |
| 未知文件系统诊断 | 无法识别时显示魔数并写 `/mnt/SDCARD/usbprobe.txt` |
| 实测 | 带供电 hub:FAT32、EXT4、NTFS 的 U 盘均可挂载读取;其他 USB 设备可识别 |
| 注意 | Type-C 口**不供 5V**,必须用自供电 USB Hub |

### 1.3 菜单重构(二级菜单,已在设备屏幕上确认显示和操作正常)
| 功能 | 说明 |
|---|---|
| 主菜单 7 项 | USB Storage、ADB、USB Serial、USB Network(A 进二级)、USB Host、Boot Defaults(A 进二级)、Return to MinUI;行数按屏幕高度自适应(1280x720@2x = 8 行),放不下时可滚动 |
| USB Network 二级菜单 | Mode(OFF / +Internet / Local only)、Network Type(ECM/NCM/RNDIS)、How to connect(Windows/macOS/Linux 可滚动说明页:该选哪种类型、电脑 IP、脚本位置、运行命令、如何停止) |
| Boot Defaults 二级菜单 | 勾选框:ADB、USB Storage(Off/RW/RO)、USB Serial、USB Network + Network Type;串口与网络互斥;改动即保存。取代了原来的 Boot Default / Boot USB Network 两行 |
| 开机配置 3 行格式 | 行1 `adb_only|adb_ums_rw|adb_ums_ro|ums_rw|ums_ro|off`,行2 `net|-`,行3 `serial|-`;旧 1~2 行格式兼容;`tomato.m21` 已支持开机不启 ADB、开串口 |
| 删除 On Exit to MinUI | 退出 USB Mode 一律自动关闭 USB Storage(避免设备与电脑同时写同一张 SD 卡) |
| 脚本位置 | 共享网络/诊断脚本只放在 `Tools/m21/USB Mode.pak/host-tools/`,不再复制到 SD 根目录;说明页写明路径 |

### 1.2 USB 串口与 USB 网络(设备模式)——支持 Windows
| 功能 | 说明 |
|---|---|
| USB Serial 改为标准 CDC-ACM | 新增 `f_acm_x.ko`;Windows 自带 `usbser`,Mac/Linux 免驱,设备端为 `ttyGS0`(原 `gser` 是厂商类,Windows 无驱动) |
| Network Type 选择 | 在 USB Network / Boot Defaults 二级菜单里选 `ECM / NCM / RNDIS`,保存在 `.userdata/m21/usb_nettype`,开机也生效 |
| ECM / NCM / RNDIS 三种网络 | 新增 `f_ncm_x.ko`、`f_rndis_x.ko`(ECM 为原有);ECM=Mac/Linux,NCM=Mac/Linux/Win11,RNDIS=Windows 10/11 |
| 串口与网络互斥 | UDC 只有 1 个中断端点,开其中一个会自动关另一个并提示;ADB + 存储 + 网络仍可共存 |
| 设备类 EF/02/01 | 串口或网络开启时设置 IAD 复合设备类,Windows 才会按功能绑定内置驱动 |
| 开机默认网络/串口 | `usb_boot_mode.cfg` 第 2 行 `net`、第 3 行 `serial` + `usb_nettype`(现由 Boot Defaults 二级菜单设置) |
| RNDIS 的 Windows 兼容 | RNDIS 排在配置第一位 + MS OS 兼容 ID 描述符 + `bDataInterface` 修正(见 §2) |

### 1.3 共享上网(宿主 NAT)脚本——三个平台
设备侧已自动设默认路由(经 192.168.7.2)和 DNS;宿主必须做 NAT:
| 平台 | 脚本 | 说明 |
|---|---|---|
| macOS | `share-internet-macos.sh on/off/status` | pf anchor NAT,需 sudo |
| Linux | `share-internet-linux.sh on/off` | iptables MASQUERADE,需 sudo |
| Windows | `share-internet-windows.bat`(调用 `.ps1`)`on/off/status` | 先试 `New-NetNat`;不可用(家庭版常见,报"无效类")就自动回退到 ICS(把 ICS 内网地址通过注册表改成 192.168.7.2) |

四个脚本在 pak 的 `host-tools/`,同时放了一份在 **SD 卡根目录**(方便在电脑上直接复制)。

### 1.4 诊断工具
- `host-tools/usb-diag.sh`(设备端)+ `usb-diag-windows.bat`:依次测网关、各 DNS 的 ping、`nslookup`、大包、系统解析器,结果保存为 `usb-diag.txt` 并复制到剪贴板(设备上位于 `Tools/m21/USB Mode.pak/host-tools/`)。
- `dhcpd_mini` 现在把收发的 DHCP 报文写到 `/tmp/dhcpd.log`。
- 设备 UDC 调试开关:`/sys/devices/platform/soc@3000000/4100000.udc-controller/{queue_debug,irq_debug}` 写 1。

## 2. 问题点、根因与修复

| # | 现象 | 根因 | 修复 / 结论 |
|---|---|---|---|
| 1 | 退出 pak 后主机模式又变回设备模式 | 状态只在 UI 进程里 | 后台守护进程 + 状态文件 `/tmp/usbhost.state`,进入时读 `otg_role` |
| 2 | 弹出 U 盘后一直 "reading",要重新插拔 | 弹出时做了 SCSI `device/delete` | 弹出改为 umount+sync+标记 `ejected`,不删设备;菜单可重新挂载 |
| 3 | ext4 显示 unknown / 魔数 `55534243` | 那支 58.5GB 三分区 U 盘在 EHCI 下传输读出损坏(数据里出现 CBW "USBC");换 4KB 请求上限也没用 | 硬件/传输层问题,不是 ext4;换 ext4 TF 卡正常。已写入 NOTES,**不要对该 U 盘写入** |
| 4 | Windows 上串口/网络"没有驱动" | `gser` 是厂商类;Windows 无 ECM 驱动;设备类不是 EF/02/01 | 换 CDC-ACM;提供 NCM/RNDIS;设备类 EF/02/01 |
| 5 | NCM/RNDIS 内核 oops(`strlen` in `if_nlmsg_size`) | 厂商内核 `struct net_device` 比主线大(`netdev_priv` 偏移 1600 vs 1344),`struct eth_dev` 叠在 net_device 尾部;ECM 恰好没事(`wrap/unwrap` 为 NULL) | 构建树头文件里给 `net_device` 末尾加 `char sunxi_tail_pad[256]`(`kmod/netdevice_h_pad.patch`),**所有网络模块重编** |
| 6 | RNDIS 类属性读回 0 | configfs 用 `%02hhx` 解析 | 写 `e0`/`01`/`03`,不要写 `0xe0` |
| 7 | 切换类型后网卡变成 usb1/usb2 | 每个注册过的网络功能占一个 `usbN` | `usbnet.sh up` 读 `functions/<type>/ifname`,清理旧的 `usbN` |
| 8 | 从 `adb shell` 里改 gadget,脚本中途消失 | 解绑 UDC 时 adb 自己也断了;stock 的 `sleep` 被连带杀掉 | 脚本用 `(trap "" HUP; sh script &)` 后台跑,用 `usleep` |
| 9 | **Windows RNDIS 代码 10**(最难) | 设备端 `rndis_setup` 一次没被调用(Windows 在配置后对设备无任何请求)。最终:① RNDIS 必须是配置中**第一个**功能(接口 0/1),在 ADB 后面(接口 1/2)必失败;② `f_rndis.c` 把 Call Management 的 `bDataInterface` 写死为 1;③ E0/01/03 路线的"Internet 共享设备"驱动在此启动失败,改用 MS OS 兼容 ID 路线 | `usbnet.sh on rndis` / `tomato` 建 RNDIS 链接时先摘掉 ffs/acm 链接,建完再补回;`kmod/f_rndis.patch`;`os_desc`(`MSFT100`、`0xcd`、`RNDIS`/`5162001`)+ `bcdDevice` 0x0511(Windows 按 VID/PID/REV 缓存 MS OS 有无) |
| 10 | Windows 拿到 169.254 | 从 `adb shell` 直接跑 `usbnet.sh up` 时 `dhcpd_mini.elf`(glibc)在 stock 根文件系统里 "not found" | 菜单/开机都在 chroot 内运行,不受影响;测试用 `chroot /overlay /tmp/dhcpd_mini.elf ...` |
| 11 | Windows 共享脚本 `New-NetNat` 报"无效类" | 该 Windows 缺少 NetNat 的 WMI 类 | 自动回退 ICS(注册表 `ScopeAddress`=192.168.7.2) |
| 12 | 设备上 8.8.8.8 通、`www.baidu.com` 卡住(Windows 共享) | **未定位**。Mac 路径下 DNS 正常;Windows 路径的诊断日志被 Ctrl+C 中断(只测到网关) | 见 §4 待办 |

## 3. 兼容性/验证矩阵
| 项目 | Mac | Windows | 说明 |
|---|---|---|---|
| USB Storage | ✅ 挂载读取正常 | ✅ 挂载读取正常 | 用户实测 |
| ADB | ✅ | ✅(开关正常) | ⚠ 偶发:切换 USB Network / USB Host 等其他开关后 adb 失效,需把 ADB 关了再开(见 §4 第 9 项) |
| USB Host(U 盘/TF/ext4/Hub) | — | — | ✅ 带供电 hub:FAT32、EXT4、NTFS U 盘可挂载读取,其他 USB 设备可识别(用户实测);Type-C 不供 5V,必须自供电 hub |
| USB 网络 ECM | ✅ ping www.baidu.com 正常 | ❌ Windows 无内置 ECM 驱动(预期,未实测) | 仅 Mac/Linux |
| USB 网络 NCM | ✅ DHCP+ping 大包;ping www.baidu.com 正常 | ✅ 可识别(Win11);共享后 ping www.baidu.com 仍有问题 | |
| USB 网络 RNDIS | ❌(Mac 无驱动,只验证枚举/控制通路) | ✅ 可识别(拿到 192.168.7.2);共享后 ping www.baidu.com 仍有问题 | 必须 RNDIS 排第一 |
| USB 串口 CDC-ACM | ✅ 双向收发 | ✅ 正确识别为 COM 口,可连接进入类似 adb shell 的界面(用户实测) | 开机 `adb_only`+`serial` 重启实测通过 |
| 共享上网 | ✅(pf) | 🟡 8.8.8.8 通,域名解析卡住,待查 | |
| 菜单界面(二级菜单、全屏、说明页、互斥提示) | — | — | ✅ 用户在设备屏幕确认显示和二级菜单正常;运行时切换/绑定失败回退分支未逐项测 |

## 4. 未解决 / 待办
1. **Windows 共享后域名解析卡住**:先在 RNDIS/NCM + Windows 共享已 `on` 的状态下完整运行 `usb-diag-windows.bat`(不要 Ctrl+C,约 1 分钟)。
   **切换网络类型后 Windows 会出现新网卡,ICS 绑定的是旧网卡**,必须重新运行 `share-internet-windows.bat on`。
2. Mac 路径下 `ping -s 1400 8.8.8.8` 100% 丢包(小包正常),`ECM` 模式下观察到;可能是 Mac 侧 NAT/MTU,也可能是大帧问题回归,待确认。
3. 设备端工具的 https 能力:设备上 `wget` 只能访问 http,https 不行;自带 `curl` http/https 都不行(编译时没有任何协议)。这是设备自带工具的限制,不是 USB 网络问题;需要的话另找带 TLS 的 wget/curl 放进 pak。
4. 菜单:显示和二级菜单已确认;仍未逐项测的是"开机关闭 ADB"组合、开机模式 `off`、绑定失败自动回退分支。
5. Windows ICS 回退路径、`New-NetNat` 路径:脚本未经完整真机验证(ICS 路径已能让 8.8.8.8 通)。
6. 58.5GB 三分区 U 盘的传输错误(可能需要按 VID:PID 加 quirks,未尝试)。
7. `uh_tune_disk` 把 `max_sectors_kb` 限成 4 对该 U 盘无效且更慢,可以去掉。
8. 设备当前状态:`usb_boot_mode.cfg`=`adb_only`+`net`、`usb_nettype`=`ncm`。SD 根目录的调试文件和 share/diag 脚本副本已清理(脚本只保留在 pak 的 host-tools 里)。
9. **ADB 偶发失效**:切换 USB Network、USB Host 等其他开关后,偶尔 adb 连不上,需要在菜单里把 ADB 关掉再打开才恢复。未排查;怀疑是 `apply_usb_config()` 解绑/重绑 UDC 时 adbd 的 FunctionFS 端点状态没有跟着重置(假设,未验证)。下次复现时在设备上看 `cat /sys/kernel/config/usb_gadget/g1/UDC`、`ls configs/c.1`、`ps | grep adbd`、`dmesg | tail`。
10. USB Host 的其余设备(键盘/手柄等)只验证了"能识别",没有验证功能。

## 5. 相关提交(`fork/main`)
- USB Host:`4f39fb3` 持久化 · `f8d379e` A 键菜单 · `519994a` 非破坏性弹出 · `74ef8f7` 设备详情 · `33573a8` ext2/3/4 · `3f31bb9` 未知文件系统诊断 · `791087a` 读取参数
- Windows 支持:`a7d9a65` ACM/NCM/RNDIS + net_device 填充 + 设备类 · `123fcb9` RNDIS 排第一 + MS OS + 描述符修复
- 共享与诊断:`d2ed7ce` Windows 共享脚本 · `00da38b` ICS 回退 · `b470884` 诊断脚本 · `9e753c6` 本文档
- 菜单重构:`48ff1b8` 二级菜单/说明页/开机 3 行配置 · `4dfe4c8` 行数随屏幕高度 · `3faa59d` 删除 On Exit · `12bf099` 脚本只放 pak 的 host-tools
