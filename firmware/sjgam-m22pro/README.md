# SJGAM M22Pro 原厂固件导出

从设备 16 MiB SPI NOR 导出的原厂固件(Tina Linux 4.0.0 / Linux 5.4.61,版本 `2024.10.26-v1.1.1`)及其分析。
**分析文档:[ANALYSIS.md](ANALYSIS.md)**

```
images/                         原样导出的分区镜像(共 ≈16.7 MB)
  mtd0_uboot.bin                1 MiB   boot0 SPL + U-Boot 2018.05 + OP-TEE + DTB
  mtd1_env.bin                  128 KiB U-Boot 环境变量
  mtd2_boot.bin                 4 MiB   Android boot image(gzip zImage,Linux 5.4.61)
  mtd3_boot-resource.bin        ≈950 KiB FAT16(开机 logo)
  mtd4_rootfs.squashfs          ≈10.2 MB SquashFS(xz) 原厂 rootfs
  mtd_info.txt                  /proc/mtd 与 /proc/partitions
  SHA256SUMS
extracted/                      从镜像/运行设备中提取的可读内容
  dts/                          两份 DTB 及反编译的 DTS(st7703s 640x480 / hx8399 1080x1920)
  bootloader/mtd1_uboot_env.txt U-Boot 环境变量(文本)
  kernel/kernel_config.txt      内核 .config
  kernel/kallsyms.txt           内核符号表(配合反汇编 vmlinux 使用)
  boot-resource/                BOOTLOGO.BMP/png、MAGIC.BIN
  rootfs_notable/               rootfs 中有代表性的脚本和配置(etc/、guimenu、setusbconfig、/usr/tomato ...)
  rootfs_filelist.txt           rootfs 完整文件清单
```

校验:`cd images && shasum -a 256 -c SHA256SUMS`

> 版权归 Allwinner 与设备厂商所有,仅供研究、备份和兼容性开发。刷写 SPI NOR 有变砖风险,本仓库不提供也不建议刷写。
> MyMinUI 只需要 SD 卡,不会改动这些分区。
