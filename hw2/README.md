# Assignment 2

## Building the Kernel

`cd` into kernel mainline v6.1 directory. You need to patch `syscall.patch` to the kernel source code first.

```sh
patch -p1 < path/to/syscall.patch
```

Then, you configure the kernel with the following options:

```sh
make defconfig
scripts/config -e CONFIG_USERFAULTFD
scripts/config -d CONFIG_TRANSPARENT_HUGEPAGE
```

After that, you can build the kernel with the following command:

```sh
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc)
```

## Test the System Call

In your new kernel, run the following command perform the tests:

```sh
test/test_syscall
```