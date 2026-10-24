# m21 本地构建(不依赖已失效的上游工具链)

上游 `makefile.toolchain` 克隆的 `shauninman/union-m21-toolchain` 已不存在。这里用 Docker 里的
`arm-linux-gnueabihf-gcc` 加设备自带的 glibc 库,直接编出 m21 的 glibc 程序(和 `USB Mode.pak/usbmode.elf` 同一套方法)。

## 内容
- `Dockerfile.kbuild-arm`、`Dockerfile.arm2`:构建镜像 `kbuild-arm2`(Debian bookworm + ARM 交叉工具链 + libc6-dev-armhf-cross)。
- `build.sh`:容器内执行,编 minui、minarch、keymon、clock、minput、clear_recent、convertboxart。
- `run.sh`:宿主机入口,`LIBDIR`(默认 `/tmp/ub`)放库和头文件,`OUT`(默认 `/tmp/mb`)放产物。

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

- 新编的:minui、minarch、keymon、libmsettings、show、overclock、Clock/Input/Clear Recently Played/Convert BoxArt 四个 pak 的程序。
- 取自上游最后一个 m21 发布包(`BASEZIP`):`rootfs.ext2`、24 个核心 `.so`、`MyCommander.elf`(Files.pak)、`prboom.wad`、`m21/libmusl/libmsettings.so`。
  这些现在在本机编不了(核心要大内存加联网克隆源码,Commander 是 C++),和上一个发布包同一来源。
- 不含 `ZXS.pak`(fuse 核心还没有 m21 版)。
- 含我们 fork 自己的 `USB Mode.pak`(在 skeleton 里)。
- 包根目录额外放了一份 `tomato` 和 `emulationstation`(和 `m21/` 里的相同,都是 `boot/boot.sh`),把 zip 直接解压到 FAT32 卡根目录即可,不用手动复制;`boot.sh` 里的 `unzip` 加了 `-x "m21/*" tomato emulationstation`,更新时不会覆盖正在运行的脚本。
- 包布局和上游 20260208b 的 m21 包一致:`Bios Cheats Emus Roms Tools m21 MinUI.zip MANUAL.txt`(上游旧包是 `README.txt`,新 makefile 已改成 `MANUAL.txt`)。

## 第一次真机测试的教训(2026-10-24)
第一版包在 M22Pro 上卡在彩条界面:`.userdata/m21/logs/minui.txt` 里是
`libc.so.6: version GLIBC_2.34 not found (required by .system/m21/lib/libmsettings.so)`。
原因:`libmsettings.so` 和 `overclock.elf` 是用构建机(Debian bookworm,glibc 2.36)的 libc 链接的,`shm_open` 绑定到了 2.34;
而设备 rootfs 只有 glibc 2.26。其它程序当时是对着设备库链接的,没有这个问题。
修复:两者都改成对着设备库(`libc.so.6`、`libdl.so.2`、`librt.so.1`)链接,并加上上面的版本检查和 `verify.sh`。

## 已知问题(上游代码里的 bug,未修)
- `workspace/m21/libmsettings/msettings.c` 的 `SaveSettings()`:`open()` 没有 `O_CREAT`,文件不存在时永远保存失败(设备上确实没有 `msettings.bin`),
  而且 `write()` 写的是指针变量地址 `&settings`,不是结构体。结果是 m21 上亮度和音量**从不持久化**,每次开机回到默认值。
  新版 gcc 把后一个问题当作错误,所以 `build.sh` 里只对这个警告降级,保持和上游一致的行为。以后可以修成 `O_CREAT|O_TRUNC` 加 `write(shm_fd, settings, ...)` 加 `close()`。

## 状态(2026-10-24)
- 上游最新源码(`72ed4f3`)的 minui、minarch、keymon、clock、minput、clear_recent、convertboxart 全部**编译并链接通过**,
  依赖的 glibc 符号版本不高于 2.17。
- **只验证了能编过,没有在设备上运行过。** 部署前必须备份设备上的 `.system/m21` 并逐项回归。
- 现在也覆盖了 `libmsettings`、`show`、`overclock`,并用上游相同的优化参数(minui `-Os -flto`,minarch `-O3 -flto=auto` 等)。
- 还没覆盖:`Commander`(C++)、核心 `.so`、fuse 核心。
