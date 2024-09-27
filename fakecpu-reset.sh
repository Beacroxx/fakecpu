#!/usr/bin/env bash

echo "Resetting modifications..."

sudo umount /sys/devices/system/cpu
sudo umount ./original_cpu_dir
sudo umount /proc/cpuinfo /proc/stat
sudo rm -rf original_cpu_dir custom_cpu workdir cpu_base stat cpuinfo

if [[ $1 != "auto" ]]; then
  echo "Unset LD_PRELOAD."
  echo "Bash:   'unset LD_PRELOAD'"
  echo "Elvish: 'unset-env LD_PRELOAD'"
fi
