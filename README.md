## Fake CPU Core Count

This project allows you to simulate a higher number of CPU cores and a custom CPU frequency on your Linux system. It's useful for testing and development purposes when you need to emulate a system with more cores than your physical hardware provides.

### Files

- `fakecpu.sh`: Main script to set up the fake CPU cores
- `fakecpu-reset.sh`: Script to reset the system to its original state

### Usage

1. To simulate a specific number of cores:
   ```
   ./fakecpu.sh -c [core count] (-f max cpu freq, optional)
   ```
   Example: `./fakecpu.sh -c 128 -f 8GHz`

2. To reset the system:
   ```
   ./fakecpu-reset.sh
   ```

### Requirements

- Linux system
- Bash shell
- Root privileges (sudo)

### Note

This is for testing and development purposes only. It does not actually increase your system's processing power. Additionally, tools like `nproc` and `htop` won't show these fake cores, however `btop` and `fastfetch` will.
