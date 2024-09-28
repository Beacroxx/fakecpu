#!/usr/bin/env bash

# Check if GNU parallel is installed
if ! command -v parallel &> /dev/null; then
  echo "Install GNU parallel."
  exit 1
fi

# Parse command-line options
while getopts c:f: option; do
  case "${option}" in
    c) targetcores=${OPTARG};;
    f) customfreq=$(numfmt --from=auto --to=none $(echo ${OPTARG} | tr '[:lower:]' '[:upper:]' | sed 's/[HZ]//g'))
      if [ $? -ne 0 ]; then
        echo "Error: Invalid frequency format for -f: ${OPTARG}"
        exit 1
      fi;;
    *)
      echo "Usage: $0 -c [core count] (-f max cpu freq, optional)"
      exit 1;;
  esac
done

# Check if -c was provided
if [ -z "$targetcores" ]; then
  echo "Error: -c option is required."
  echo "Usage: $0 -c [core count] (-f max cpu freq, optional)"
  exit 1
fi

# Reset any previous fake CPU setup
if [ -d "./workdir" ]; then
  ./fakecpu-reset.sh
fi

# Get the number of physical cores
cores=$(unset LD_PRELOAD; nproc)
extracores=$((targetcores-cores))

# Inform the user about the operation
if [[ "$customfreq" =~ ^-?[0-9]+$ ]]; then
  echo "Creating $extracores additional CPU cores and setting max clock to $(numfmt --to=si --suffix 'Hz' $customfreq)..."
else
  echo "Creating $extracores additional CPU cores..."
fi

# Create necessary directories
mkdir original_cpu_dir custom_cpu workdir

# Mount the CPU sysfs directory
sudo mount --bind /sys/devices/system/cpu ./original_cpu_dir

# Set online and possible CPUs in the custom directory
echo "0-$((cores+extracores-1))" | sudo tee ./custom_cpu/online >/dev/null
echo "0-$((cores+extracores-1))" | sudo tee ./custom_cpu/possible >/dev/null

# Create an overlay mount to combine original and custom CPU configurations
sudo mount -t overlay overlay -o lowerdir=$PWD/original_cpu_dir,upperdir=$PWD/custom_cpu,workdir=$PWD/workdir /sys/devices/system/cpu

# Set custom CPU frequency if provided
if [[ "$customfreq" =~ ^-?[0-9]+$ ]]; then
  parallel -j $cores 'sudo mkdir -p ./custom_cpu/cpufreq/policy{1}; echo {2} | sudo tee ./custom_cpu/cpufreq/policy{1}/cpuinfo_max_freq >/dev/null' ::: $(seq 0 $((cores-1))) ::: $customfreq
fi

# Create symbolic links for the extra CPU cores
parallel -j $cores 'sudo ln -s /sys/devices/system/cpu/cpu$(( {1} % {2} )) ./custom_cpu/cpu{1}' ::: $(seq $cores $((extracores+cores-1))) ::: $cores
