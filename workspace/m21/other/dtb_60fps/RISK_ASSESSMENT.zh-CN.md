# M22Pro 内置屏 60fps(设备树补丁)风险评估

> English (主文档): [RISK_ASSESSMENT.md](RISK_ASSESSMENT.md)

日期:2026-10-20。**本文只做评估,没有向设备写入任何东西。**
上游的做法见同目录 [Readme.md](Readme.md)(作者 Turro75,只对 M22Pro 有效,对 M21 会变砖)。

## 1. 要解决的问题

M22Pro 内置屏(hx8399,1080x1920 MIPI-DSI)在原厂设备树里是 **37.6Hz**(`ref_fps:38`)。后果 [作者 + 代码 `platform.c:1292`]:
- 内置屏不能开 vsync(开了帧率被拖到 38fps),只能靠双缓冲减轻撕裂,仍会撕裂;
- 游戏最高只能约 38fps 刷新(60fps 游戏会丢帧显示)。

补丁把 LCD 时序改成 ~59.5Hz。`launch.sh` 通过 `sys` 里是否还有 `fps:38` 判断是否已打补丁并设置 `NEWDTB`,`platform.c` 据此在内置屏也启用 vsync,**不需要改程序**。

## 2. 我们验证过的事实(离线,基于 `firmware/sjgam-m22pro/images/mtd0_uboot.bin`)

| 事实 | 结论 |
|---|---|
| 我们设备 hx8399 DTB 的 `lcd0@5461000` 节点 | 与上游 `orig.dts` **完全一致**(同一版固件),补丁数值可直接用 |
| 设备树位置 | mtd0 偏移 `0xd6000`,60608 字节;mtd0 里还有一份 st7703s(640x480)DTB 在 `0x862b8`,**不动** |
| mtd0 布局 | `0x0` boot0 SPL(28KiB,eGON.BT0);`0x10000` `sunxi-package`(u-boot、optee、dtb 三项,有效长度 `0xd8000`) |
| **package 头部有加法校验和** | 头部 `add_sum=0x8bb7e81c`,我们用算法(求和时该字段按戳值 `0x5F0A6C39` 计)复算**完全一致**,说明校验和覆盖整个包(含 DTB) |
| 时序计算 | 原厂 90MHz / (1230×1945) = **37.62Hz**;补丁 162MHz / (1350×2016) = **59.52Hz** |
| DSI 速率(4 lane,RGB888) | 原厂 540Mbit/s/lane → 补丁 **972Mbit/s/lane**(接近翻倍) |
| 闪存擦除粒度 | 4KiB(`/proc/mtd` erasesize=0x1000) |

### 上游 `dd` 做法的问题(我们的判断)
上游流程是:`dtc` 重新编译整个 DTS(得到 58503 字节,比原来的 60608 小 2105 字节),再 `dd seek=876544 conv=notrunc` 写回去,最后把**整个 1MiB** 的 mtdblock0 重写。这意味着:
1. DTB 变短,后面留着 2105 字节旧数据;
2. package 头部校验和**没有更新**(包内容变了,校验和对不上);
3. 重写了全部 256 个扇区,包括 boot0 和 U-Boot,不必要地扩大了暴露面。

它在作者的设备上能启动,说明 boot0/U-Boot **实际上没有强制校验**这个加法校验和 [推断,依据是作者能启动];但没有源码证据,不能当作保证。

## 3. 我们准备的、更保守的补丁方式

脚本:[`patch_mtd0_60fps.py`](patch_mtd0_60fps.py)(只读转储、只写新文件,**不会刷机**)。

- 直接在原 DTB **二进制里原位修改 7 个 4 字节的时序属性**(`lcd_dclk_freq`、`lcd_hbp`、`lcd_ht`、`lcd_hspw`、`lcd_vbp`、`lcd_vt`、`lcd_vspw`),不经过 dtc,DTB 大小和布局完全不变;
- 在 DTB 后面 0 填充的空隙里写一个**补偿字**,让包校验和保持原值,于是 **package 头部扇区一个字节都不用改**;
- 离线验证结果:全文件只有 10 个字节不同;**boot0 区(0~0x7000)未改**;校验和复算通过;`--restore` 回写后与原转储**逐字节一致**(往返验证);
- **只需要重写 2 个 4KiB 扇区**:`0xde000`(时序所在)和 `0xe4000`(补偿字)。上游的方案要重写 256 个。
- 备选 `--fix-header`:改为更新头部校验和字段,需要多写 `0x10000` 一个扇区(头部扇区断电损坏的后果最严重,所以默认不用)。

## 4. 风险清单

| # | 风险 | 后果 | 可能性/说明 | 缓解 |
|---|---|---|---|---|
| 1 | 写闪存过程中断电/死机 | DTB 扇区损坏 → U-Boot 无法正常启动,**设备可能起不来** | 窗口很短(两个 4KiB 扇区);上游方案要写整块 1MiB,窗口大得多 | 满电 + 接电源;只写 2 个扇区;写后回读比对;写之前确认备份 |
| 2 | 面板不接受 972Mbit/s 的 DSI 速率,或时序超出面板范围 | 开机后**黑屏/花屏**(系统可能仍能启动) | **未核实** hx8399 的最大 lane 速率;上游作者在他的 M22Pro 上点亮并测到 60.2Hz,但只有这一个样本,面板批次可能不同 | 屏黑时靠 **adb 恢复**(见 §5);备份保留 |
| 3 | 刷新率升高后显示引擎欠载/错误增多 | 偶发花屏、撕裂、卡顿 | 上游的 `sys` 输出里 `err:277`(60Hz 样本)对比我们设备原厂的 `err:83`,运行时间不同不能直接比,**需要实测** | 刷后观察 `cat /sys/class/disp/disp/attr/sys` 的 `err`/`skip` 增长速率 |
| 4 | 功耗/发热变化 | 电池续航下降 | 像素时钟翻倍,面板与 DSI 功耗会上升;**没有数据** | 刷后观察电量下降速度 |
| 5 | 补偿字写在包内空隙,若 boot0/U-Boot 对该区域有别的用途 | 理论上可能影响启动 | 该区域是 0 填充,位于 DTB 之后、dtb 条目长度之外 [推断];上游方案同样改动了 DTB 之后的区域 | 可改用 `--fix-header` 方案(需写头部扇区) |
| 6 | 误刷到 M21 | **变砖** | 上游明确警告 | 脚本只改含 `hx8399` 的 DTB,找不到就退出;设备确认是 hx8399(`lcd_driver_name`) |
| 7 | 刷机后无法恢复 | 需要硬件手段 | boot0/U-Boot 无法启动时,Allwinner BROM 一般会进入 **FEL(USB 刷机)模式** [推断,**本设备未验证**,也不知道有没有进入 FEL 的物理方式] | 在设备上确认能否进入 FEL 之前,把"无法启动"当作不可恢复来对待 |

## 5. 恢复路径

1. **能启动但屏幕异常**(风险 2、3):系统仍会运行,`tomato` 启动 adbd,**adb 应该仍能连接**。用恢复镜像把 `0xde000` 和 `0xe4000` 两个扇区写回原值(`--restore` 产物与原转储逐字节一致),重启。
2. **不能启动**(风险 1、5):只能靠 FEL 或编程器。没有验证,**不要依赖**。
3. 原厂转储备份:`firmware/sjgam-m22pro/images/`(带 `SHA256SUMS`)和 Desktop 备份。

## 6. 刷机前必须做的检查(如果决定做)

1. 读设备当前的 `/dev/mtdblock0`,与 `mtd0_uboot.bin` 做 SHA256 对比,**必须一致**(证明备份就是当前闪存内容)。
2. 确认设备树里的屏幕是 hx8399(`cat /proc/device-tree/soc@3000000/lcd0@5461000/lcd_driver_name`)。
3. 电量 > 50% 并接上电源;adb 稳定。
4. 先生成补丁镜像并检查脚本输出("package checksum verified"、"header sector changed: False"、"boot0 area changed: False")。
5. 只写 `0xde000` 与 `0xe4000` 两个扇区,写完立即回读并 `cmp`;不要 `dd` 整个 1MiB。
6. 把恢复用的两个扇区备份(原值)拷到 SD 卡和电脑上,确认 adb 重新连接后再重启。
7. 重启后:`cat /sys/class/disp/disp/attr/sys` 应显示 `ref_fps:60`;`launch.sh` 会设置 `NEWDTB=1`。

## 7. 评估结论(供决策)

- **收益**:内置屏 38Hz → ~60Hz,可以开 vsync、消除大部分撕裂,60fps 游戏能满帧显示。无需改程序。
- **相对上游方案的改进**:原位改 7 个字、写 2 个扇区、头部和 boot0 不动、可往返恢复;已离线验证。
- **仍然存在的风险**:(1)写闪存本身的断电风险;(2)面板是否接受 972Mbit/s(无数据,只有作者一台设备的成功样本);(3)刷坏后没有已验证的硬件恢复手段。
- **没有任何已知的、不刷闪存就能试的办法**:`sunxi` 显示驱动的 LCD 时序来自设备树,我们没有发现运行时调节途径 [推断,设备未连接,尚未在 `/sys/class/disp/` 下核对]。
- 建议:**先在设备上做 §6 的第 1~2 项只读检查**,并确认能否进入 FEL 模式后,再决定是否刷。


## 补充 1:上游发布包不含刷写工具(2026-10-20 核实)

下载并检查了 m21 发布包 `20260118b-0`、`20260126b-0`、`20260208b-0`,以及仓库全部 git 历史:

- 包内没有任何 dtb/mtd/flash 相关文件或脚本,`tomato`/`emulationstation` 里只有一行注释掉的 `#export NEWDTB=0`。
- 上游只有两样东西:`dtb_60fps/Readme.md` 的**手动步骤**,以及提交 `1ec9f1b` 的**检测逻辑**(`launch.sh` 读 `/sys/class/disp/disp/attr/sys`,没有 `fps:38` 则设 `NEWDTB=1`)。
- Readme 里的 "modified tomato script ... can work even as a tool" 是作者自用的改版,没有随包发布。

所以刷 SPI Flash 没有现成、经过验证的工具,只能自己做,本目录的 `patch_mtd0_60fps.py` 就是这个用途。

## 补充 2:设备只读核对结果(2026-10-20,未写入任何内容)

| 项目 | 结果 |
|---|---|
| `/dev/mtdblock0` SHA256 | `852d4d22…c90aa7`,与 `firmware/sjgam-m22pro/images/mtd0_uboot.bin` **完全一致** |
| 分区 | mtd0 `uboot` 1 MiB、擦除块 4 KiB;mtd1 env 128 KiB;mtd2 boot 4 MiB;mtd4 rootfs |
| 屏驱动 | dmesg:`hx8399 lcd_panel_init is ok`,确认是 hx8399 |
| 当前帧率 | `ref_fps:38`,实测 `fps:37.8`,DE 时钟 300 MHz,`err:53` |
| 运行中设备树 `lcd0@5461000` | dclk=0x5a(**90 MHz**)、ht=1230、vt=1945、1080x1920、4 lane、hbp=32、hspw=8、vbp=7、vspw=6;与 flash 里的 DTB 及补丁脚本的假设一致 |
| 运行时调时序接口 | `/sys/class/disp/disp/attr/` 只有 `sys xres yres colorbar enhance_* runtime_enable ...`,**没有改 LCD 时序的接口**,`runtime_enable=0`。只能改 flash 里的 DTB,无法先在运行时试验 |

结论:补丁脚本的前提(型号、原始数据、时序)已在真机上全部核实。**唯一没核实的是 FEL 救砖能否进入**,以及 hx8399 能否承受 972 Mbit/s 的 lane 速率;这两项只有刷了才知道,所以仍不建议在没有 FEL 手段前写入。


## 补充 3:FEL 实测(2026-10-20,未写 Flash)

- 设备的 `/sbin/reboot` 支持 `efex`:`adb shell reboot efex`(会提示 `can not access .../rtc/flag`,可忽略)后设备进入 FEL,Mac 上 `sunxi-fel version` 返回 `AWUSBFEX soc=00001859(R528)`。**设备能进 FEL,Mac 能通信。**
- 设备电源是**拨动开关**(没有电源键):拨到关再拨到开即可冷启动并恢复,实测恢复正常(MyMinUI-20260208b-0,`ref_fps:38`,adb 正常)。
- `sunxi-tools` 不在 brew 里,需自行编译:`brew install dtc pkgconf`,`git clone https://github.com/linux-sunxi/sunxi-tools`,`make sunxi-fel`。
- 这个版本的 `sunxi-fel` **没有实现 R528 的 SPI 支持**(`spiflash-info`/`spiflash-read` 报 `SPI support not implemented yet for 1859 (R528)`),所以不能直接通过 FEL 读写 SPI NOR。真要救砖,需要先通过 FEL 上传自制的 SPI 读写小程序,**尚未实现、尚未验证**。
- 仍未验证:Flash 内容损坏后,上电能否**自动**进入 FEL;没有电源键,无法"按键上电进 FEL",手动进入需要短接 SPI NOR 引脚或用 TF 卡启动,两者都未调查。
- 结论:FEL 通道可用,但"刷坏了一定能救回来"这件事**仍没有保障**。不建议在做出可用的 FEL 救砖流程前写 Flash。


## 补充 4:通过 FEL 读 SPI NOR 已跑通(2026-10-20,只读)

- 给 `sunxi-tools`(基线提交 `4ab7b32`)的 `fel-spiflash.c` 打补丁,加上 R528/D1/T113(`0x1859`)的 SPI0 支持:SPI0 基址 `0x04025000`、CCU `0x02001000`、GPIO `0x02000000`(端口步长 0x30)、引脚 PC2~PC7 功能 2,并把引脚复用的掩码从 3 位改成 4 位(BROM 把 PC 脚留在 `0xf`,3 位掩码会写出非法的 `0xa`)。补丁见 [sunxi-fel-r528-spi.patch](sunxi-fel-r528-spi.patch),用法:`git apply` 后 `make sunxi-fel`。
- 实测 `sunxi-fel spiflash-info`:Manufacturer `85h`(Puya),model `20h`,**容量 16 MiB**。`mtd0` 的 1 MiB 只是整颗 SPI NOR 的第一个分区。
- `sunxi-fel spiflash-read 0 0x1000000 <out>` 读出整颗 16 MiB,与仓库 `firmware/sjgam-m22pro/images/` 里 5 个分区镜像(uboot/env/boot/boot-resource/rootfs)**逐字节一致**,尾部全是 `0xff`。完整备份保存在 `~/Desktop/mtd_dump/spi_full_16M_fel.bin`,SHA256 `76aa79b7…ec42a`。
- 所以:**设备在 Linux 起不来时,只要能进 FEL,就能把整颗 SPI NOR 读出/比对。写入路径(`spiflash-write`,擦除+页编程)尚未测试**,Puya 不在工具的已知芯片表里,会按默认的 4 KiB 擦除 `0x20` 和 256 字节页编程 `0x02` 处理,需要先在一个无关紧要的区域(如文件尾部 `0xff` 区)做写入/回读验证后才能信赖。


## 补充 5:FEL 写入路径实测(2026-10-20)

- 测试区域:SPI 偏移 `0xfb0000` 起,位于 rootfs 分区之后的空白尾部(文件系统在 `0xfa7800` 结束,之后全是 `0xff`),不影响任何系统内容。
- `sunxi-fel spiflash-write 0xfb0000 <8 KiB 随机数据>` 后回读,**逐字节一致**:擦除(4 KiB,`0x20`)+页编程(256 B,`0x02`)在 Puya 芯片上可用,没有写保护。
- 恢复过程中出过一次失误:用 `tr` 生成"全 0xff"文件时,因为终端区域设置得到了 UTF-8 的 `c3 bf` 序列,导致 `0xfb0000` 起 16 KiB 被写成了 `c3bf`。随后用 Python 生成真正的 16 KiB `0xff` 写回,再读出整颗 16 MiB,**与刷前的完整备份 SHA256 完全一致(`76aa79b7…ec42a`)**,即已原样恢复。教训:生成测试数据一律用 Python 或 `xxd`,并写后立刻整片回读比对。
- 结论:**设备进 FEL 后,可以用 `sunxi-fel` 读出、写入、校验整颗 SPI NOR**。因此只要能进 FEL(目前只验证了 Linux 活着时 `reboot efex` 进入),刷坏的设备就有了可用的恢复手段:`spiflash-write 0 ~/Desktop/mtd_dump/spi_full_16M_fel.bin`。
- 仍未验证:Flash 内容损坏、Linux 起不来时,设备能否自动/手动进入 FEL。这是现在唯一剩下的缺口。


## 补充 6:已刷入并验证(2026-10-20)

- 方法:`patch_mtd0_60fps.py` 生成补丁镜像(只改 10 字节,落在 `0xde000`、`0xe4000` 两个 4 KiB 扇区),设备 `reboot efex` 进 FEL 后用打了 R528 补丁的 `sunxi-fel spiflash-write` **只写这两个扇区**,再读回整颗 16 MiB 校验:前 1 MiB 与补丁镜像一致,其余 15 MiB 与刷前完整备份一致。补丁镜像存于 `~/Desktop/mtd_dump/mtd0_60hz_patched.bin`。
- 结果:拨动开关重启后**正常开机**,adb 正常,`ref_fps:60`,实测 `fps:60.2`,`mgr0: 1080x1920`;dmesg 里 `hx8399 lcd_panel_init is ok`;运行中设备树 `lcd_dclk_freq=0xa2`(162 MHz)、`lcd_ht=0x546`、`lcd_vt=0x7e0`。
- DE 的 `err` 计数在开机后稳定在 81,15 秒内不再增长;SoC 温度约 37.6 ℃(空闲)。
- `/sys/class/disp/disp/attr/sys` 里不再有 `fps:38`,`launch.sh` 下次启动会设 `NEWDTB=1`,`platform.c` 随之对内屏启用 vsync。
- 恢复方法(如需回退):`reboot efex` 进 FEL,`sunxi-fel spiflash-write 0 ~/Desktop/mtd_dump/spi_full_16M_fel.bin`(整片),或只写 `0xde000`/`0xe4000` 两个扇区的原始内容;万一连 FEL 都进不去,用 CH341 编程器写入同一份备份。
- 待观察:长时间运行的发热、耗电、有无闪屏;游戏内 vsync 的实际效果(撕裂是否消失、`minarch_prevent_tearing` 等选项是否需要调整)。


## 补充 7:60Hz 后的帧提交节奏实测(2026-10-20)

工具:[pantrace](pantrace/README.zh-CN.md)(被动记录 `minarch.elf` 的显示 ioctl 时间)。游戏:GBA《龙珠大冒险》,连续滚动,约 100~120 秒/次,**每组只测了一次**。原始输出见 [measurements/](measurements/)。

| | A:Prevent Tearing=Off,Core Sync=Core | B:Prevent Tearing=Lenient,Core Sync=Auto |
|---|---|---|
| 提交间隔 p50 | **16.80 ms**(59.5 Hz) | **16.80 ms**(59.5 Hz) |
| p95 | 17.22 ms | 17.22 ms |
| p99 | 22.4 ms | **18.3 ms** |
| 间隔 >20 ms 的次数 | 76(1.3%) | **48(0.7%)** |
| 间隔 <12 ms(追赶式连发)的次数 | 12 | **4** |
| `FBIOPAN_DISPLAY` 平均耗时 | 6.06 ms | 6.05 ms |
| `>30 ms` 次数(菜单/暂停造成) | 7 | 7 |

结论(只写数据能支撑的部分):

1. **两种配置下提交节奏都锁在 16.80 ms(59.5 Hz),即屏幕的新刷新率**,与我们算出的 59.52 Hz 吻合。`FBIOPAN_DISPLAY` 每帧阻塞约 6 ms 等 vblank。
2. **M22Pro 竖屏走旋转分支,`PLAT_flip` 在该分支里无条件调用 `pan_display`**(`sync && isnewdtb` 只用于不旋转的分支),所以 vsync 等待并不依赖 Prevent Tearing,也不依赖 `NEWDTB`。这解释了为什么只改这个开关感觉不出大差别。
3. 之前 38 Hz 的屏幕上,因为每帧都要等 vblank,**最多只能每秒提交 38 帧**,而 GBA 核心每秒产出约 59.7 帧,必然丢帧。现在每秒能提交约 59.5 帧。这条是机制推导,**没有 38 Hz 时代的 pantrace 数据做对比**(工具当时还没有)。
4. 在 60Hz 下,B(Lenient+Auto)比 A 略好:>20 ms 的次数 1.3%→0.7%,连发次数 12→4。差异方向一致,但**每组只有一次、约 100 秒、含菜单暂停,不能当作统计显著**。
5. **撕裂本身没有测到**,这套工具只能反映提交节奏。要看撕裂需要高速摄像(如手机 240 fps 慢动作拍 `0testtearing.p8`)。


## 补充 8:发现并修复 system.cfg 默认值写错(2026-10-20)

- `minarch` 的选项值按 `strcmp` **区分大小写**匹配,匹配不上就回落到第 0 项。`skeleton/SYSTEM/m21/system.cfg` 里原来写的是 `minarch_prevent_tearing = lenient`(小写),而合法值是 `Off/Lenient/Strict`,所以出厂默认**实际是 `Off`**(日志里也能看到 `SET Prevent Tearing TO Off`)。
- 已改成 `Lenient`(仓库 `skeleton/SYSTEM/m21/system.cfg`,设备上的 `/mnt/SDCARD/.system/m21/system.cfg` 也已同步,原文件备份在 `~/Desktop/mtd_dump/system.cfg.orig`)。
- 设备上 `GBA-gpsp`、`FC-fceumm`、`P8-fake08` 已保存的 `minarch.cfg` 会覆盖系统默认,已全部改为 `Prevent Tearing=Lenient`、`Core Sync=Auto`(原文件备份在 `~/Desktop/mtd_dump/*_minarch.cfg.orig`)。以后新玩的核心会直接用新默认值。
- 跟踪工具 `pantrace` 已从设备卸载(`/overlay/etc/ld.so.preload` 和 `/overlay/lib/pantrace.so` 已删除)。
