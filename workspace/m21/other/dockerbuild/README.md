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

## 状态(2026-10-24)
- 上游最新源码(`72ed4f3`)的 minui、minarch、keymon、clock、minput、clear_recent、convertboxart 全部**编译并链接通过**,
  依赖的 glibc 符号版本不高于 2.17。
- **只验证了能编过,没有在设备上运行过。** 部署前必须备份设备上的 `.system/m21` 并逐项回归。
- 还没覆盖:`show`、`overclock`、`Commander`(C++)、核心 `.so`。
