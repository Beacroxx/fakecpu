#!/usr/bin/env bash

echo "Resetting modifications..."

sudo umount /sys/devices/system/cpu
sudo umount ./original_cpu_dir
sudo rm -rf original_cpu_dir custom_cpu workdir cpu_base stat cpuinfo
