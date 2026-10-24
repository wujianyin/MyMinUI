# SJGAM M22Pro Stock Firmware Export

> 中文版: [README.zh-CN.md](README.zh-CN.md)

Stock firmware exported from the device's 16 MiB SPI NOR (Tina Linux 4.0.0 / Linux 5.4.61, version `2024.10.26-v1.1.1`) and its analysis.
**Analysis doc: [ANALYSIS.md](ANALYSIS.md)**

```
images/                         Partition images exported as-is (≈16.7 MB total)
  mtd0_uboot.bin                1 MiB   boot0 SPL + U-Boot 2018.05 + OP-TEE + DTB
  mtd1_env.bin                  128 KiB U-Boot environment variables
  mtd2_boot.bin                 4 MiB   Android boot image (gzip zImage, Linux 5.4.61)
  mtd3_boot-resource.bin        ≈950 KiB FAT16 (boot logo)
  mtd4_rootfs.squashfs          ≈10.2 MB SquashFS (xz) stock rootfs
  mtd_info.txt                  /proc/mtd and /proc/partitions
  SHA256SUMS
extracted/                      Readable content extracted from images / running device
  dts/                          Two DTBs and decompiled DTS (st7703s 640x480 / hx8399 1080x1920)
  bootloader/mtd1_uboot_env.txt U-Boot environment (text)
  kernel/kernel_config.txt      Kernel .config
  kernel/kallsyms.txt           Kernel symbol table (use with disassembled vmlinux)
  boot-resource/                BOOTLOGO.BMP/png, MAGIC.BIN
  rootfs_notable/               Representative scripts and configs from rootfs (etc/, guimenu, setusbconfig, /usr/tomato ...)
  rootfs_filelist.txt           Full rootfs file listing
```

Verify: `cd images && shasum -a 256 -c SHA256SUMS`

> Copyright belongs to Allwinner and the device vendor; for research, backup, and compatibility development only. Flashing SPI NOR carries brick risk; this repository does not provide or recommend flashing.
> MyMinUI only needs an SD card and does not modify these partitions.
