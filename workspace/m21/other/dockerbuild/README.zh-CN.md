# m21 本地构建(不依赖已失效的上游工具链)

> English (主文档): [README.md](README.md) | 返回 [README.zh-CN.md](../../../../README.zh-CN.md)

上游 `makefile.toolchain` 克隆的 `shauninman/union-m21-toolchain` 已不存在。这里用 Docker 里的
`arm-linux-gnueabihf-gcc` 加设备自带的 glibc 库,直接编出 m21 的 glibc 程序(和 `USB Mode.pak/usbmode.elf` 同一套方法)。

## 内容
- `Dockerfile.kbuild-arm`、`Dockerfile.arm2`:构建镜像 `kbuild-arm2`(Debian bookworm + ARM 交叉工具链 + libc6-dev-armhf-cross)。
- `build.sh`:容器内执行,编 minui、minarch、keymon、clock、minput、clear_recent、convertboxart、**language**(语言选择器)、**usbmode**(USB Mode 界面)。
- `run.sh`:宿主机入口,`LIBDIR`(默认 `/tmp/ub`)放库和头文件,`OUT`(默认 `/tmp/mb`)放产物。
- `package.sh`:组装发布包;`verify.sh`:qemu 链接校验;`build_fsckfat.sh`:编译修复只读 SD 卡用的静态 ARM `fsck.fat`,见 [SD_RESTORE.zh-CN.md](../../../../SD_RESTORE.zh-CN.md) 第 7 节。

## LIBDIR 需要什么(仓库里没有,不提交二进制库)
- `m21_glibc/`:设备 rootfs 里的 libc、libm、libpthread、libdl、libz、libSDL2、libSDL2_image、libSDL2_ttf、libsamplerate、libmsettings。
  libmsettings 也可以用 `workspace/m21/libmsettings` 重新编译。
- `sdl2_inc/`:SDL2 头文件和 `samplerate.h`。
- `crt_glibc.c`:自带 `_start`,使产物能在设备的旧 glibc(<= 2.17)上启动。
- 当前这份 `sdl2_inc` 不完整(缺 `IMG_GetError`、`IMG_SavePNG_RW`、`TTF_SetFontOutline` 的声明),所以 `build.sh` 里用 `-DIMG_GetError=SDL_GetError`
  绕过;另外两个函数只出现隐式声明警告,返回值不影响。换成完整的 SDL2_image/SDL2_ttf 头文件后可以去掉这些绕法。

## 校验(打包前必做)
`verify.sh` 用上游发布包里真实的设备 rootfs(glibc 2.26)加 qemu-user,把每个编出来的程序"加载"一遍,能提前复现设备上的
`version GLIBC_x not found` 一类错误。`build.sh` 末尾还有 glibc 版本检查(2.34 及以上直接报错)。
`LIBDIR/m21_glibc/` 还需要 `librt.so.1`(从设备 rootfs 的 `/lib/librt-2.26.so` 取出)。

## 打包
`run.sh` 编完以后执行 `package.sh`,按顶层 makefile 的 `setup`/`system`/`cores`/`package` 步骤组装发布包,输出到 `releases/MyMinUI-<日期>b-0-m21.zip`:

```
sh workspace/m21/other/dockerbuild/run.sh        # 编译,产物在 /tmp/mb
BASEZIP=/path/MyMinUI-20260208b-0-m21.zip sh workspace/m21/other/dockerbuild/package.sh
```

- 新编的:minui、minarch、keymon、libmsettings、show、overclock、**language.elf**(放到 `.system/m21/bin/`)、**usbmode.elf**(放到 `Tools/m21/USB Mode.pak/`)、Clock/Input/Clear Recently Played/Convert BoxArt 四个 pak 的程序。
- 多语言:`skeleton/SYSTEM/res/lang/` 和各 pak 的 `lang/` 原样进包,见 [I18N.zh-CN.md](../../../../I18N.zh-CN.md)。
- 中文字体:`workspace/m21/other/cjkfont/` 里的同名替换件会被 `package.sh` 放进包里(`.system/res/BPreplayBold-unhinted.otf` 和 `Tools/m21/Files.pak/res/FreeSans.ttf`),不需要再手动装。
- 取自上游最后一个 m21 发布包(`BASEZIP`):`rootfs.ext2`、24 个核心 `.so`、`MyCommander.elf`(Files.pak)、`prboom.wad`、`m21/libmusl/libmsettings.so`。
  这些现在在本机编不了(核心要大内存加联网克隆源码,Commander 是 C++),和上一个发布包同一来源。
- 不含 `ZXS.pak`(fuse 核心还没有 m21 版)。
- 含我们 fork 自己的 `USB Mode.pak`(在 skeleton 里)和 `Language.pak` 工具。
- `tomato` 用 `workspace/m21/other/usbmode/tomato.m21`(11749 字节,上游 `boot.sh` 加 USB 开机逻辑)。
- 包根目录额外放了一份 `tomato` 和 `emulationstation`(和 `m21/` 里的相同,都是 `boot/boot.sh`),把 zip 直接解压到 FAT32 卡根目录即可,不用手动复制;`boot.sh` 里的 `unzip` 加了 `-x "m21/*" tomato emulationstation`,更新时不会覆盖正在运行的脚本。
- 包布局和上游 20260208b 的 m21 包一致:`Bios Cheats Emus Roms Tools m21 MinUI.zip MANUAL.txt`(上游旧包是 `README.txt`,新 makefile 已改成 `MANUAL.txt`)。

## 第一次真机测试的教训(2026-10-24)
第一版包在 M22Pro 上卡在彩条界面:`.userdata/m21/logs/minui.txt` 里是
`libc.so.6: version GLIBC_2.34 not found (required by .system/m21/lib/libmsettings.so)`。
原因:`libmsettings.so` 和 `overclock.elf` 是用构建机(Debian bookworm,glibc 2.36)的 libc 链接的,`shm_open` 绑定到了 2.34;
而设备 rootfs 只有 glibc 2.26。其它程序当时是对着设备库链接的,没有这个问题。
修复:两者都改成对着设备库(`libc.so.6`、`libdl.so.2`、`librt.so.1`)链接,并加上上面的版本检查和 `verify.sh`。

## 在设备上发现并修掉的问题(2026-10-24 实机测试)
- **GBA 只有 30fps、音频破音**:上游为了 Sharp 缩放器把显示层从 RGB565 改成 ARGB8888,每帧写显存的量翻倍(1080x1920 面板上约 4.9MB→9.8MB),
  绘制线程占 CPU 从约 35% 涨到约 60%,一帧超出 16.7ms,每帧都错过 vsync,卡在 30fps。
  修复:m21 的显示层恒为 RGB565,不提供 Sharp(`platform.h` 里 `NO_SHARP_SCALER`,菜单里只剩 Near)。实测提交间隔 16.8ms、59.4fps。
  定位方法见 `workspace/m21/other/dtb_60fps/pantrace`(测 `DISP_LAYER_SET_CONFIG`/`FBIOPAN_DISPLAY` 间隔),旧版 minarch 同一游戏 59.5fps 作对照。
- **HUD 的 CPU 频率恒为 0**:`getCPUusage()` 里 `static long a[9]` 却读 10 个值,越界写入相邻变量,换了编译器后正好清零 `cur_cpu_freq`。改成 `a[10]`。
- **HUD 字体又大又糊**:上游按游戏画面最大边选 15x24 字体,m21 的 720x1280 画面又会被显示引擎放大 1.5 倍。`platform.h` 里 `HUD_FONT_MID_MAX 1280` 让它沿用 10x16。
- **亮度/音量不保存**(上游老 bug,已修):`SaveSettings()` 的 `open()` 没有 `O_CREAT`,而且 `write()` 写的是指针变量地址 `&settings`。
  现在是 `O_CREAT|O_TRUNC` 加 `write(fd, settings, ...)` 加 `close()`;读取时校验版本和取值范围,`jack`/`hdmi` 不持久化。
  注意:更新到修复版之前,若还有旧 keymon 在运行,它会把 `msettings.bin` 覆盖成乱码,所以更新后要完整重启一次;乱码文件会被校验丢弃并回到默认值。

## 状态(2026-10-25)
- 上游最新源码(`72ed4f3`)的 minui、minarch、keymon、clock、minput、clear_recent、convertboxart 全部**编译并链接通过**,
  依赖的 glibc 符号版本不高于 2.17。
- 已在 SJGAM M22Pro 上实测 GBA(见上一节)。其它核心和 PICO-8 还没逐个回归,部署前先备份 `.system/m21`。
- 现在也覆盖了 `libmsettings`、`show`、`overclock`,并用上游相同的优化参数(minui `-Os -flto`,minarch `-O3 -flto=auto` 等)。
- 还没覆盖:`Commander`(C++)、核心 `.so`、fuse 核心。
