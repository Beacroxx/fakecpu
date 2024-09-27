#!/usr/bin/env bash

if [ -z "$1" ] || ! [[ "$1" =~ ^-?[0-9]+$ ]]; then
  echo "Usage: $0 [core count] (max cpu freq in hz, optional)"
  echo "Example: $0 128 5000000"
  exit 1
fi

cores=$(unset LD_PRELOAD; nproc)
targetcores=$1
extracores=$((targetcores-cores))

customfreq=""
if [ -n "$2" ] && [[ "$2" =~ ^-?[0-9]+$ ]]; then
  customfreq=$2
fi

if [ -d "./cpu_base" ]; then
  ./fakecpu-reset.sh auto
fi

if [[ "$customfreq" =~ ^-?[0-9]+$ ]]; then
  echo "Creating $extracores additional CPU cores and setting max clock to $customfreq Hz..."
else
  echo "Creating $extracores additional CPU cores..."
fi

mkdir original_cpu_dir custom_cpu workdir
sudo mount --bind /sys/devices/system/cpu ./original_cpu_dir
echo "0-$((cores+extracores-1))" | sudo tee ./custom_cpu/online >/dev/null
echo "0-$((cores+extracores-1))" | sudo tee ./custom_cpu/possible >/dev/null
sudo mount -t overlay overlay -o lowerdir=$PWD/original_cpu_dir,upperdir=$PWD/custom_cpu,workdir=$PWD/workdir /sys/devices/system/cpu

if [[ "$customfreq" =~ ^-?[0-9]+$ ]]; then
  parallel -j $cores 'sudo mkdir -p ./custom_cpu/cpufreq/policy{1}; echo {2} | sudo tee ./custom_cpu/cpufreq/policy{1}/cpuinfo_max_freq >/dev/null' ::: $(seq 0 $((cores-1))) ::: $customfreq
fi

parallel -j $cores 'sudo ln -s /sys/devices/system/cpu/cpu$(( {1} % {2} )) ./custom_cpu/cpu{1}' ::: $(seq $cores $((extracores+cores-1))) ::: $cores

gcc -shared -fPIC -DFAKE_NPROCESSORS=$((cores+extracores)) fake_sysconf.c -o libfakesysconf.so -ldl

echo "Load the shared library libfakesysconf.so using LD_PRELOAD to spoof nproc."
echo "Bash:   'export LD_PRELOAD=$PWD/libfakesysconf.so'"
echo "Elvish: 'set-env LD_PRELOAD \$E:PWD/libfakesysconf.so'"
