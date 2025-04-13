#!/bin/bash

# Usage: ./build_zram.sh [algorithm]
# Example: ./build_zram.sh zstd

set -e

ALG=${1:-lz4}   # lzo lzo-rle lz4 [lz4hc] zstd 
ZRAM_DEV=/dev/zram0
ZRAM_SYS=/sys/block/zram0

echo "[+] Using compression algorithm: $ALG"

# Turn off swap if already on
if swapon --show | grep -q "$ZRAM_DEV"; then
    echo "[*] Turning off existing zram swap..."
    swapoff $ZRAM_DEV
fi

# Reset zram
echo "[*] Resetting zram..."
echo 1 > $ZRAM_SYS/reset

# Set compression algorithm
echo "[*] Setting compression algorithm to $ALG..."
echo $ALG > $ZRAM_SYS/comp_algorithm

# Set size (default: 2G)
echo "[*] Setting disksize to 1G..."
echo 1G > $ZRAM_SYS/disksize

# Create swap header and enable
echo "[*] Creating and activating zram swap..."
mkswap $ZRAM_DEV
swapon $ZRAM_DEV

# Confirm status
echo "[*] Current zram status:"
cat /proc/swaps
echo "[*] Algorithm in use: $(cat $ZRAM_SYS/comp_algorithm)"
