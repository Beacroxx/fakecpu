## Fake CPU Core Count

This project allows you to simulate a higher number of CPU cores and a custom CPU frequency on your Linux system. It's useful for testing and development purposes when you need to emulate a system with more cores than your physical hardware provides.

### Files

- `fakecpu.sh`: Main script to set up the fake CPU cores
- `fakecpu-reset.sh`: Script to reset the system to its original state
- `fake_sysconf.c`: C code for intercepting system calls related to CPU count (nproc)

### Usage

1. To simulate a specific number of cores:
   ```
   ./fakecpu.sh [desired core count] (optional max frequency in Hz)
   ```
   Example: `./fakecpu.sh 128`

2. After running the script, follow the instructions to set the `LD_PRELOAD` environment variable.

3. To reset the system:
   ```
   ./fakecpu-reset.sh
   ```

### Requirements

- Linux system
- GCC compiler
- Bash shell
- Root privileges (sudo)

### Note

This is for testing and development purposes only. It does not actually increase your system's processing power.
