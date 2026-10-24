# MyMinUI 在 SJGAM M21 / M22Pro(`m21` 平台)上的适配现状与已知问题

> English (主文档): [M21_STATUS.md](M21_STATUS.md) | 返回 [README.zh-CN.md](README.zh-CN.md)

整理日期:2026-10-20,2026-10-25 更新。目的:在动手做功能完善之前,先搞清楚上游作者已经做了什么、没做什么、哪里有坑。

## 0. 资料来源和可信度

| 来源 | 内容 | 说明 |
|---|---|---|
| `README.upstream.md`(987 行) | 作者 Turro75 的全部发布说明(2024-10 至 2026-08) | 与 GitHub Releases 页面一致;只有作者自己的说法,未经我们验证 |
| GitHub Releases(`gh release list`,共 40 个) | 哪些版本带了 m21 的发布包 | 已逐个核对附件名 |
| 仓库提交历史 | 与 `workspace/m21` 相关的非 fork 提交约 246 条 | 已按时间过一遍 |
| GitHub Issues / PR(`gh issue`/`gh pr`) | 与 m21/m22pro 相关的 issue #28 #38 #48 #51 #63 #71 #74 #75 #77 #79 #80,PR #111–#116 | 已读正文和评论 |
| 仓库代码和文档 | `workspace/m21/*`、`skeleton/SYSTEM/m21`、`INSTALL.md`、`workspace/m21/other/dtb_60fps/Readme.md`、`firmware/sjgam-m22pro/` | 已读 |
| 我们自己的设备(M22Pro) | 设备上读到的版本号、显示参数 | 标注为"实测" |

标记含义:**[作者]** 作者在发布说明/issue 里的说法;**[实测]** 在我们这台 M22Pro 上看到的;**[代码]** 从仓库代码得出;**[推断]** 我的判断,没有证据直接证明。

## 1. 平台概况

`m21` 是上游给两台 SJGAM 掌机共用的平台目录:**SJGAM M21**(3.5" 640x480 横屏)和 **SJGAM M22Pro**(1080x1920 竖屏面板,横着用)。另外还有一台 LOONG MAY-M21(PR 作者的设备,H133)。

- 芯片 [作者]:Allwinner H133(sun8iw20),双核 Cortex-A7,最高 1.2GHz,**没有 GPU**,厂商宣传的 "N909" 就是它。内存 256MB [我们的固件分析]。
- 系统 [作者][实测]:原厂 Tina Linux(musl)只读,16MiB SPI NOR 已满。作者的做法:原厂的 `/usr/tomato`(=SD 卡根目录的 `tomato`)作为钩子,用 fuse2fs 把 SD 卡上的 `rootfs.ext2`(glibc)挂到 `/overlay`,再 `chroot` 进去运行 MinUI。所有 MinUI 二进制都按 glibc 工具链编译。
- 显示 [代码][实测]:
  - 屏幕只接受 RGB8888,且每个像素最高字节必须是 0xff 才可见 [作者];所以 SDL 的 flip/render 用不了,作者自己写了 framebuffer + 显示层(`/dev/disp` + ION,`sunxi_display2.h`)的渲染器(`workspace/m21/platform/platform.c`)。
  - M22Pro 面板物理 1080x1920,程序按 1280x720 绘制(`DEVICE_WIDTH/HEIGHT`),旋转 90 度后以 720x1280 写入显示层,由显示引擎放大到面板大小。设备上实测:`fb[1280,1280] crop[0,0,720,1280] frame[0,0,1080,1920]`。
  - M21 与 M22 靠文件 `/mnt/SDCARD/m21/thisism22` 区分 [代码];我们的设备上这个文件存在(2 字节)。
  - 内置屏刷新:M22Pro 原厂设备树只有 **38fps**,M21 约 50fps [作者];HDMI 可以到 60fps。
- 输入 [作者]:
  - M22Pro 没有 Menu 键,用 **Select+Start** 模拟;没有电源键,只有物理电源开关,长按 Select+Start 2 秒执行关机(软件只能关到"可以断电"的状态)。
  - 面板按键是 BAYX 布局,代码里把 A/B、X/Y 对调成 ABYX(和其他 Miyoo/Anbernic 一致)。可编辑 `.system/m21/custombuttonmapping.env` 改键 [作者][代码]。
  - 摇杆是**数字摇杆,硬连到按键**,没有模拟量,也不能重映射(issue #77)。
- 时间/电源 [作者]:没有 RTC,关机时保存系统时间、开机恢复(用物理开关断电不会保存);睡眠 = 黑屏,Select 唤醒,2 分钟后自动"关机"(仍需手动拨开关);CPU 频率档位 480/720/912/1008/1104/1200MHz [代码]。
- 安装 [代码]:`INSTALL.md` 的 SJGAM 一节——FAT32 SD 卡,把发布包里的 `m21/` 目录复制过去,`m21/emulationstation` 和 `m21/tomato` 再复制到根目录,整个 zip 保持压缩放在根目录;首次启动自动安装(需要创建 swap,**耐心等**)。

### 1.1 硬件与显示链路(2026-10-22 adb 和固件设备树实测)

| 项目 | 结果 | 证据 |
|---|---|---|
| SoC | 全志 H133(sun8iw20,与 R528/T113 同族) | `/proc/device-tree/model`=`sun8iw20`,compatible=`allwinner,h133`、`arm,sun8iw20p1` |
| CPU | 双核 Cortex-A7,NEON、VFPv4;可用频率 480/720/912/1008/1104/1200 MHz | `/proc/cpuinfo`,`cpufreq/scaling_available_frequencies` |
| 内存 | 约 234MB 可用(`MemTotal` 239908 kB) | `/proc/meminfo` |
| GPU | **没有** | 固件 dts 和运行中的设备树里没有 gpu/mali/powervr/sgx 节点;`/dev` 下没有 dri/mali/pvr/kgsl;`/sys/class` 没有 drm 和 devfreq;dmesg 无 GPU 日志;overlay 里只有 libdrm,没有 Mali/EGL/GLES 库 |
| 有的图形相关硬件 | G2D(`g2d@5410000`,`/dev/g2d`,2D 加速)、显示引擎 DE(`disp@5000000`,`/dev/disp`)、视频编解码 Cedar(`ve@1c0e000`,`/dev/cedar_dev`)、deinterlace、CE 加密、摄像头输入 vind | 设备树节点和 `/dev` |

显示链路 [代码][推断]:核心或 MinUI 用 CPU(NEON)把一帧画进 ION 物理连续内存 → CPU 旋转 90° → 通过 `/dev/disp` 的 `pan_display` 让 DE 读取该图层 → DE 缩放到 1080x1920 并经 MIPI-DSI 送面板。DE 是固定功能的显示读取和合成单元,不是 GPU,画什么完全由 CPU 决定。`pan_display` 会阻塞约 6ms 等 vblank,所以 60Hz 补丁(改面板时序和 DSI 速率)和 Prevent Tearing 都作用在这一段。`colorbar` 属性是 DE 自带的测试图案,不经过 CPU。

由此带来的限制:需要 3D 的核心(PSP、DC 等)跑不动;能做的加速只有 NEON,以及用 G2D 做旋转和缩放(尚未评估,见第 6 节)。

## 2. 适配时间线(m21 相关)

| 日期 | 事件 |
|---|---|
| 2024-10-08 | 首次支持 M21。当时**不能用**:NDS、Amiga、retroarch、外接手柄、HDMI。特殊处理:双击 Menu 睡眠、长按 Menu 3 秒关机 |
| 2024-11-04/05 | 外接手柄(仅 SJGAM 私有协议、仅 1 个)、HDMI 输出含音频、最小音量静音、按键布局调整、CORE_THREAD_VIDEO(核心线程拆分,PS1 BR2 约 56fps)、32bpp 像素格式 |
| 2024-12-01 | 自动旋转核心画面(竖版街机游戏),m21 最先支持 |
| 2024-12-15 | 作者的 M21 坏了,**停止发布** |
| 2025-01-27 | 重新启用 m21 构建,并加入 M22Pro(HDMI + 1 个外接手柄) |
| 2025-04-02 | 全平台双缓冲 + vsync 大改,m21 又因作者设备损坏没跟上 |
| 2025-04-13 / 04-30 | 发布 `sjgam_m21_m22`、`SJGAM-M21-M22p` 版,靠用户测试修复撕裂和旋转 |
| 2025-05-25 | **重新完整支持 m21/m22**:显示层双缓冲、Select+Start 菜单/关机、睡眠逻辑、默认 1280x720、`custom_hdmi_settings.txt`;vsync 因固定 50/38fps 被禁用 |
| 2025-08-03 ~ 2025-12-28 | 渲染流程统一、修闪烁(#71,12-23 版修复);2025-12-29 提交锁定各核心的 git 版本 |
| 2026-01-09 | 显示层驱动完善、启动色条、可自定义按键映射(#74)、HDMI 下启用硬件 vsync |
| 2026-01-18 | 文档化 M22Pro 设备树修改以达到 60fps(`dtb_60fps`) |
| 2026-01-26 | 安装/更新改进,安装画面按屏幕方向旋转 |
| 2026-02-04 / 02-08 | 02-04 版在 M21 上黑屏(#79,驱动按 M22 调的,初始化值对 M21 不对),02-08 版修复。**这是最后一个带 m21 包的正式版本** |
| 2026-05-25 ~ 2026-08-05 | 5 个正式版,**都没有 m21 包**(见 §4.1) |
| 2026-06-06 | 作者声明:M22Pro 也坏了(摔了之后没显示),**不再支持 m21 平台、不再发布** |
| 2026-08-07 ~ 08-08 | 社区用户 WladmirDotNet(有一台 LOONG MAY-M21)提交并合并:PR #114 修复 m21 编译(bit-rot)、#116 用 ARGB8888 显示层给 m21 启用 sharp 缩放、#115 修红色通道、#111 越界写 |
| 2026-08-10 | 作者提交 `72ed4f3`:把 `m21` 恢复到默认构建平台列表。**但截至今天没有新的正式发布**(最新是 2026-08-05 版,无 m21 包) |

## 3. 当前功能状态

我们设备上运行的版本:**MyMinUI-20260208b-0**(`.system/version.txt`,提交 `2c7a860`)[实测]。

### 3.1 作者说"能用"的
| 功能 | 状态 | 备注 |
|---|---|---|
| 启动器 / 游戏内菜单 / 存档预览 / 封面 | 能用 [作者] | 我们的设备能进入界面 [实测] |
| 31 个系统(Emus pak) | 能用 [代码][作者] | A2600/5200/7800、DOOM、DOS、FBN、FC、GB/GBA/GBC、GG、MAME、MD、NG、NGCD、NGP/NGPC、PCE、PKM、PS、PUAE(Amiga)、QUAKE、SFC、SGB、SMS、SUPA、VB、WS、ZXS、P8/P8N(原生 Pico-8) |
| 渲染 | 显示层双缓冲 [作者] | 内置屏不能开 vsync(见 §4.2) |
| 缩放 | nearest;sharp(PR #116,**在 02-08 版之后,未发布**) | 当前设备版本只有旧缩放 [推断] |
| HDMI | 能用,默认 1280x720p60 [作者] | 可改 `.system/m21/custom_hdmi_settings.txt`(建议 854x480p60 提升性能) |
| 外接手柄 | 仅 SJGAM 私有手柄,1 个 [作者] | #63(M22 双手柄)未解决 |
| 自动旋转竖版街机游戏 | 能用 [作者] | |
| 睡眠 / 关机 | 能用但行为特殊 [作者] | 见 §1 |
| 调试 HUD(CPU、帧率、温度、电量) | 能用 [作者] | |
| 作弊码 | 能用 [作者] | 2025-12-16 起 |
| 重映射按键 | 编辑 `custombuttonmapping.env` [作者] | 没有界面 |

### 3.2 明确不能用或没做的
| 功能 | 状态 | 来源 |
|---|---|---|
| NDS 模拟 | 不支持 | [作者] 首发说明;NDS 只给 miyoomini/a30 |
| retroarch | 不能用 | [作者] 首发说明,作者曾考虑整体放弃 retroarch |
| 外接手柄 2P | 不支持 | issue #63,作者有旧分支 `testmultiplayer` 但没合并 |
| 摇杆模拟量 | 硬件限制 | #77 |
| 内置屏 vsync/60fps | 原厂设备树限制 | 见 §4.2 |
| 软件关机/开机 | 硬件限制(只有物理开关) | [作者] |
| 非 M22 的 Menu 键 | M22Pro 无 Menu 键,靠 Select+Start | [作者] |

## 4. 已知问题与风险

### 4.1 维护状态(最大的风险)
1. **没有人持有 M21/M22Pro 做主力测试。** 作者的 M21 和 M22Pro 先后损坏 [作者]。唯一的社区测试者持有 LOONG MAY-M21(不是 M22Pro)。
2. **最后一个 m21 正式包是 20260208b-0。** 之后上游改了很多全局代码(新音频引擎、新缩放器、睡眠模式、更新时重置亮度/音量设置、`PLAT_getVsyncInterval` 接口等),这些在 m21 上**从未由作者验证**:
   - 2026-08 的 PR #114 说明 m21 一度**连编译都不通过**(`SHARPNESS_SOFT` 被删、缺 `PLAT_getVsyncInterval`);
   - 2026-05-25 版本说明里"去掉 nextui 的音频修复、改用新音频引擎"对 m21 的影响没有任何说明 [推断:需要验证]。
3. **工具链来源已失效。** `makefile.toolchain` 从 `shauninman/union-m21-toolchain` 克隆,该仓库已不存在(PR #114 作者的说法)。需要手工使用作者 releases 里的 `sjgam_m21_toolchain` 并解压到 `toolchains/m21-toolchain/`。
4. m21 的构建流程(`make m21` = `clean setup common package`)与 h700/r36s(`build system cores package`)不同:**核心 `.so` 不在 git 里**(`**/cores/output` 被 `.gitignore` 忽略);我们本地的 `workspace/m21/cores/output` 里有 24 个 `.so`,来源没有核实 [代码]。所以从干净克隆出发无法直接 `make m21` 得到完整发布包,核心要另外构建或取自发布包 [推断]。

### 4.2 显示与撕裂
- M22Pro 内置屏原厂设备树把刷新率设成 **38fps**;M21 约 50fps。vsync 一旦打开帧率就被拖到这个数值,所以**内置屏上 vsync 被强制关闭** [作者],只靠双缓冲减轻撕裂,**仍可能看到撕裂**。
- 我们设备 [实测]:`ref_fps:38`,说明用的是原厂设备树,没有改过。
- 作者记录了修改设备树达到 60fps 的方法(`workspace/m21/other/dtb_60fps/Readme.md`):提取 `mtdblock0`、改 `lcd0` 节点的时序、重新编译,再用 `dd` 写回 `/dev/mtdblock0`。**只对 M22Pro 有效,M21 刷了会变砖**;刷 SPI NOR 本身有变砖风险。启动脚本通过 `fps:38` 判断是否已打过补丁(`NEWDTB` 环境变量)。**发布包不含刷写工具**(已核对 20260118/0126/0208 三个 m21 包和 git 历史),只有这份手动步骤和上述检测逻辑;作者 Readme 里提到的 "modified tomato script" 是自用,未发布。2026-10-20 已在设备上只读核对:`mtdblock0` 与仓库镜像 SHA256 一致、屏幕为 hx8399、运行中设备树时序与补丁假设一致,详见 [RISK_ASSESSMENT.zh-CN.md](workspace/m21/other/dtb_60fps/RISK_ASSESSMENT.zh-CN.md) 补充章节。FEL 实测:`reboot efex` 可进入,`sunxi-fel version` 识别为 R528;原版 `sunxi-fel` 不支持 R528 的 SPI,我们打补丁后(`sunxi-fel-r528-spi.patch`)已能读写整颗 16 MiB SPI NOR。**2026-10-20 已在本机刷入 60Hz 补丁并验证:`ref_fps:60`、`fps:60.2`、正常开机**,详见 RISK_ASSESSMENT.zh-CN.md 补充 6。
- issue #80(M22Pro,NES 画面撕裂/声音):作者建议内置屏关掉 tearing 设置,同步参考选 `Core` 或 `NoFix`;用户反馈 `Core` 最好 [作者]。
- issue #71:动作键按下时闪屏,与作弊码初始化有关,2025-12-23 版修复。

### 4.3 设备差异(M21 vs M22)
- 渲染驱动是按 **M22 的参数**调的,M21 上出现过黑屏(#79,2026-02-08 版修复)和菜单镜像/按键错位 [作者]。由于作者手上只剩 M22 的信息,**M21 的回归风险更高**。
- 两者靠 `thisism22` 文件区分:安装时需要正确放置,否则旋转/分辨率会错。
- HDMI 不旋转,内置屏旋转 90 度,代码里分别处理(包括原生 Pico-8)[作者]。

### 4.4 输入与电源
- 摇杆是数字的 → 没有模拟量游戏支持(#77)。
- M22 按 Select+Start 同时是原厂的"切换 1P/2P"(原厂固件里)和 MyMinUI 的菜单快捷键,导致与第二个手柄冲突(#63,用户要求加一个选项)。
- 没有软件开机/关机键,关机后需要手动拨电源开关。系统时间只在"软关机"时保存。

### 4.5 安装与升级
- 必须使用正确命名的 `MyMinUI-*-m21.zip`,不要改成 `MinUI.zip`(#51,多位用户踩坑);SD 卡兼容性也出现过问题。
- 升级会覆盖大部分文件,但 `custombuttonmapping.env` 不再被覆盖(2026-02-07 起)[作者]。
- 2026-06-14 起,每次更新都会重置亮度/音量设置 [作者](对 m21 的影响没有验证)。

### 4.6 构建
- 需要在 `PLATFORMS`/工具链里确认 m21 仍可构建;PR #114 说明需要 `-lsamplerate`(已在 `makefile.env` 加入)。
- fbneo、mame32 核心编译吃内存,Docker 内存太小会 `lto1` 被杀(#75);作者在 Mac mini 上需要 6GB 内存加 4GB swap,有时需要重跑。
- 编译产物里 glibc 版本的二进制只能在 chroot 里运行,要直接在原厂系统运行必须用 musl 工具链(#48)。

## 5. 我们设备上已经发现的、与上游无关的问题

这部分是我们自己在做 USB 功能时发现的,详细记录见 `workspace/m21/other/usbmode/FEATURES_AND_ISSUES.zh-CN.md`:
- USB Mode.pak(存储、ADB、串口、网络、Host、开机默认项)是我们在这个 fork 里加的,上游没有。
- `curl` 没有任何协议、`wget` 不支持 https:原厂工具限制。
- ADB 偶发:切换其他 USB 开关后可能失效,需要在 USB Mode 里把 ADB 关了再开(根因未查)。
- Windows 共享网络后域名解析卡住(IP 通,`ping www.baidu.com` 有问题,待查)。
- 硬断电后 SD 卡可能留下 FAT 错误,内核把它改成只读,MinUI 卡在色条界面。修复:[SD_RESTORE.zh-CN.md](SD_RESTORE.zh-CN.md) 第 7 节。

## 6. 适合着手的功能完善方向(建议,未做过评估)

这些是根据以上资料整理的候选项,是否做、先做哪个由你决定:

| 方向 | 价值 | 难度/风险 | 依据 |
|---|---|---|---|
| 把 20260208 之后的上游提交(新音频引擎、sharp 缩放、睡眠改进、PR #111–#116)合入并在 M22Pro 上逐项验证 | 高:当前设备版本落后很多,且 m21 一度编不过 | 中:需要验证音频、缩放、睡眠 | §4.1 |
| 在 M22Pro 上验证并(如需要)修复新缩放器 sharp 模式 | 中:画质提升 | 中:PR #116 是在 LOONG MAY-M21 上测的,不是 M22Pro | §2 |
| 内置屏 60fps:设备树补丁 | 高:解决撕裂/帧率上限,可开 vsync | **高**:写 SPI NOR,有变砖风险;只适用 M22Pro;已做离线评估和更保守的补丁脚本(只写 2 个扇区),见 [RISK_ASSESSMENT.zh-CN.md](workspace/m21/other/dtb_60fps/RISK_ASSESSMENT.zh-CN.md) | §4.2 |
| 外接手柄 2P(#63) | 中 | 中:有旧分支参考,需解决 Select+Start 冲突 | §4.4 |
| 按键映射界面(目前只能改 env 文件) | 低~中 | 低 | §3.1 |
| 修复构建链:工具链来源、README 说明、`make m21` 流程 | 中:便于以后复现 | 低 | §4.1 |
| 把我们的 USB Mode.pak 做成可随发布包分发的 pak | 中 | 低 | §5 |
| 排查 ADB 偶发失效、Windows DNS 问题 | 中(我们自己的功能) | 低~中 | §5 |

## 6.1 对 20260208 之后上游提交(2c7a860..HEAD,约 160 个)的审查结果(2026-10-24)

已在 SJGAM M22Pro 上用 adb 实测 GBA,并用 `-Wall -Warray-bounds -Wrestrict -Wformat-overflow` 等严格告警编译了 minarch/api/utils/platform/minui。

**已发现并修复(都已提交):**

| 问题 | 来源 | 现象/风险 | 修复 |
|---|---|---|---|
| 显示层改成 ARGB8888 且所有模式都写 32 位 | `c5d07f5`(PR #116,在 LOONG MAY-M21 上测的) | M22Pro 上 GBA 只有 30fps、音频破音 | 显示层恒为 RGB565,去掉 Sharp 选项(`NO_SHARP_SCALER`) |
| HUD 频率恒为 0 | `getCPUusage()` 里 `a[9]` 越界写 | 换 gcc 12 后正好清零 `cur_cpu_freq` | `a[10]` |
| HUD 字体变大变糊 | `e114217` 按画面最大边选字体 | 720x1280 画面还要被显示引擎放大 1.5 倍 | `HUD_FONT_MID_MAX 1280` 沿用 10x16 |
| 亮度/音量不保存 | m21 `SaveSettings()` 老 bug | 每次开机回到默认值 | `O_CREAT`+写结构体+读取校验 |
| `getCPUusage()` 里 `sprintf` 目标和参数重叠 | 上游 | 未定义行为 | 改成 `snprintf` 追加 |
| `CPU_SPEED_SLEEP` 环境变量缺失时空指针 | m21 平台代码(`66d5cce`/睡眠改进之后新增) | 旧的 `launch.sh` 搭新二进制会崩 | 缺失时回落到 480000 |
| 唤醒时对没初始化过的音频调 `SND_init(0,0)` | `PWR_exitSleep()` | MinUI 主界面自动休眠后唤醒,所有平台都会走到 | `sample_rate_bak>0` 才重新初始化 |
| `PLAT_shouldWake()` 在输入设备打不开时没有 return | m21 平台代码 | 未定义行为 | 补 `return 0` |
| `OptionQuicksave_onConfirm()` 没有 return | 上游 | 未定义行为(实际上 `PWR_powerOff()` 不返回) | 补 return |

**检查过、不需要改:**
- 存档从 `.sav` 改名为 `.srm`(`292ee24`):打开游戏时会自动把旧 `.sav` 改名。注意:改名后再退回旧版 minarch 会找不到存档。
- 更新时删除 `msettings.bin`(`66d5cce`):m21 的 `boot.sh` 已包含;配合我们加的版本校验是安全的。
- 自动休眠 30 秒、休眠后 120 秒关机:充电时(包括 USB 供电/adb 连着)不会触发,电池供电才有;旧版也有类似行为。
- `5f85ed8`(修关机崩溃)只改了 h700/miyoomini/my282/r36s 的平台文件,m21 的 `PLAT_quitVideo` 没有同类问题。
- libsamplerate 依赖:`6ed7394` 声称移除,实际 m21 仍链接 `-lsamplerate` 且设备上有该库,不受影响。

**没有验证、仍有风险:**
- **电池供电下的自动休眠/唤醒**没法用 adb 测(连着 adb 就是充电状态,休眠不会触发)。
- **其它核心**:卡里只有 GBA 和 PICO-8 的游戏,其它核心(FC/SFC/MD/PS 等)没跑过新 minarch;核心 `.so` 和 Commander 仍是旧包里的。
- **ZXS(fuse 核心)** 还没有 m21 版本,发布包里没带。

**上游遗留的潜在 bug(随后也修了):**
- `Core_updateAVInfo()` 在 `av_infonew!=NULL` 时 `memcpy(&av_info,&av_infonew,…)` 取了指针的地址,改成 `*av_infonew`(目前调用方都传 NULL)。
- `rotateIMGScalar()` 旋转 3 的分支缺少 `y` 循环、用了未初始化的 `y`(只有标量回退路径会用到),补上循环。
- `putInt()` 的 8 字节缓冲区装不下 11 位的 int,改成 16。
- 大量 `sprintf` 往 256/512 字节的路径缓冲区写,路径长了会溢出(严格告警里有 64 处)。m21 的 `platform.h` 里把 `sprintf` 定义成按目标数组大小限长的 `snprintf`
  (指针目标大小未知时仍然不限长,行为和以前一样)。改完后这类告警降为 0。这个宏只影响 m21 的构建。

## 6.2 2026-10-25 我们做的改动

都已提交并部署到我们的 M22Pro,细节见各文档。

| 改动 | 内容 | 文档 |
|---|---|---|
| 发布包内置中文字体 | `package.sh` 覆盖 `BPreplayBold-unhinted.otf` 和 `Files.pak/res/FreeSans.ttf` | [cjkfont README](workspace/m21/other/cjkfont/README.zh-CN.md) |
| 带 USB 开机默认值的 `tomato`,合并到当前 `boot.sh` | 更新不再覆盖正在运行的 `tomato`,更新时重置 msettings,unzip 加 `-x tomato emulationstation`;卡上放错的 `tomato` 曾导致开机 adb 关闭 | [README.zh-CN.md](README.zh-CN.md)、[SD_RESTORE.zh-CN.md](SD_RESTORE.zh-CN.md) |
| 可切换界面语言 | `TR()` + 语言包、首次选择语言、`Tools/Language.pak`、各 pak 自带语言包、全部有 zh_CN;四个 `Toggle*` 工具显示当前值 | [I18N.zh-CN.md](I18N.zh-CN.md) |
| 只读 SD 卡修复 | 静态 `fsck.fat` 构建脚本、步骤、发现的问题 | [SD_RESTORE.zh-CN.md](SD_RESTORE.zh-CN.md) 第 7 节 |

设备上仍未验证:各核心游戏内菜单的中文显示,以及 USB Mode 每一页的中文(主要页面已看过)。

## 7. 引用

- 上游:<https://github.com/Turro75/MyMinUI>;Releases:<https://github.com/Turro75/MyMinUI/releases>
- 相关 issue:[#63](https://github.com/Turro75/MyMinUI/issues/63) [#71](https://github.com/Turro75/MyMinUI/issues/71) [#74](https://github.com/Turro75/MyMinUI/issues/74) [#75](https://github.com/Turro75/MyMinUI/issues/75) [#77](https://github.com/Turro75/MyMinUI/issues/77) [#79](https://github.com/Turro75/MyMinUI/issues/79) [#80](https://github.com/Turro75/MyMinUI/issues/80) [#48](https://github.com/Turro75/MyMinUI/issues/48) [#51](https://github.com/Turro75/MyMinUI/issues/51) [#38](https://github.com/Turro75/MyMinUI/issues/38)
- 相关 PR:[#114](https://github.com/Turro75/MyMinUI/pull/114) [#115](https://github.com/Turro75/MyMinUI/pull/115) [#116](https://github.com/Turro75/MyMinUI/pull/116) [#111](https://github.com/Turro75/MyMinUI/pull/111)
- 本地资料:`INSTALL.md`(SJGAM 一节)、`workspace/m21/`、`workspace/m21/other/dtb_60fps/Readme.md`、`firmware/sjgam-m22pro/`、`workspace/m21/other/usbmode/`
