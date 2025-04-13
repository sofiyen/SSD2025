#!/bin/bash

# set algorithm
# echo zstd > /sys/block/zram0/comp_algorithm

# set default swap space size
echo 2G > /sys/block/zram0/disksize

# create swap header
mkswap /dev/zram0

# turnon zram swap
swapon /dev/zram0

echo "Current ZRAM situation: "
cat /proc/swaps
