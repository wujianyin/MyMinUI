set -e
cd /work/linux-5.4.61
export ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-
cp /work/device.config .config
for o in USB_CONFIGFS_ECM USB_CONFIGFS_NCM USB_CONFIGFS_RNDIS; do
  sed -i "s/^# CONFIG_$o is not set/CONFIG_$o=m/" .config
done
make olddefconfig >/dev/null
diff <(grep -v '^#' /work/device.config | sort) <(grep -v '^#' .config | sort) || true
make -j10 modules_prepare >/dev/null 2>&1 && echo PREPARED
