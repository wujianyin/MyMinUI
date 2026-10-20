# pantrace:帧提交节奏的被动计时工具

`LD_PRELOAD` 式的小共享库,只在 `minarch.elf` 里生效,记录显示相关 ioctl(`req < 0x200` 的 `/dev/disp` 调用和 `0x46xx` 的 fb 调用)的起止时间,统计调用间隔(均值/标准差/p50/p95/p99/最大值/超过 20 ms、30 ms 的次数/小于 12 ms 的次数)和调用耗时,写到
`/mnt/SDCARD/.userdata/m21/logs/pantrace_<unix时间>.txt`。每次游戏退出(或每 8192 条记录)写一次,每次统计忽略前 3 秒。不改变任何行为。

主要请求号:`0x47` = `DISP_LAYER_SET_CONFIG`(提交图层缓冲),`0x4606` = `FBIOPAN_DISPLAY`(会阻塞到下一个 vblank)。

## 编译(docker `kbuild-arm2`,链接设备上的 glibc 库)

```sh
docker run --rm -v /tmp/ub:/t kbuild-arm2 sh -c 'cd /t/shim && \
  arm-linux-gnueabihf-gcc -O2 -shared -fPIC -o pantrace.so pantrace.c \
  -L/t/m21_glibc -l:libc.so.6 -ldl -lm -Wl,--allow-shlib-undefined && \
  arm-linux-gnueabihf-strip pantrace.so'
```

## 安装与卸载(设备的 chroot 在 `/overlay`)

```sh
adb push pantrace.so /overlay/lib/pantrace.so
adb shell 'chmod 755 /overlay/lib/pantrace.so; echo /lib/pantrace.so > /overlay/etc/ld.so.preload; sync'
# 卸载:
adb shell 'rm -f /overlay/etc/ld.so.preload /overlay/lib/pantrace.so; sync'
```
