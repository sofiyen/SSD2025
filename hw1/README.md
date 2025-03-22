# Assignment 1

## Rootkit Module

This module uses Kprobes module of Linux, so to load and run this module you should compile the guest OS with Kprobes enabled.

```sh
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
# Enable Kprobes and Kprobe events
scripts/config --enable CONFIG_KPROBES
scripts/config --enable CONFIG_KPROBE_EVENTS
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc)
```

## Test

Please run test on **guest** machine. To run the test you must have `gcc` and `make` installed. Then follow these steps to test the module.
1. Under `test/` run `make` to compile test programs.
2. Run `test/test.sh` to test the module.
