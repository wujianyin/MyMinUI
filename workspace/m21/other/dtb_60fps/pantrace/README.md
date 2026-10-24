# pantrace: Passive Frame-Submit Timing Tool

> 中文版: [README.zh-CN.md](README.zh-CN.md)

Small `LD_PRELOAD` shared library active only in `minarch.elf`. Records start/end times for display-related ioctls ( `/dev/disp` calls with `req < 0x200` and fb calls `0x46xx`), computes inter-call statistics (mean, stddev, p50, p95, p99, max, counts over 20 ms and 30 ms, under 12 ms) and call duration, writes to
`/mnt/SDCARD/.userdata/m21/logs/pantrace_<unix_time>.txt`. Flushes on each game exit (or every 8192 records); first 3 seconds ignored per session. Does not change behavior.

Main request numbers: `0x47` = `DISP_LAYER_SET_CONFIG` (submit layer buffer), `0x4606` = `FBIOPAN_DISPLAY` (blocks until next vblank).

## Build (docker `kbuild-arm2`, link against device glibc)

```sh
docker run --rm -v /tmp/ub:/t kbuild-arm2 sh -c 'cd /t/shim && \
  arm-linux-gnueabihf-gcc -O2 -shared -fPIC -o pantrace.so pantrace.c \
  -L/t/m21_glibc -l:libc.so.6 -ldl -lm -Wl,--allow-shlib-undefined && \
  arm-linux-gnueabihf-strip pantrace.so'
```

## Install and remove (device chroot at `/overlay`)

```sh
adb push pantrace.so /overlay/lib/pantrace.so
adb shell 'chmod 755 /overlay/lib/pantrace.so; echo /lib/pantrace.so > /overlay/etc/ld.so.preload; sync'
# Uninstall:
adb shell 'rm -f /overlay/etc/ld.so.preload /overlay/lib/pantrace.so; sync'
```
