MODE=$1

# Check if zram is used as swap
if [ -f /proc/swaps ]; then
    grep -q zram0 /proc/swaps
    if [ $? -eq 0 ]; then
        swapoff /dev/zram0
    fi
fi

echo 1 > /sys/block/zram0/reset

echo "Available zram devices"
lsblk | head -n 1
lsblk | grep zram
if [ $? -ne 0 ]; then
    echo "No zram devices found. Creating zram device."
    cat /sys/class/zram-control/hot_add
    if [ $? -ne 0 ]; then
        echo "Failed to create zram device."
        exit 1
    fi
fi
echo

echo "Available zram algorithms"
cat /sys/block/zram0/comp_algorithm
echo

if [ "$MODE" = "zstd" ]; then
    echo "Setting zram algorithm to zstd"
    if ! echo zstd > /sys/block/zram0/comp_algorithm; then
        echo "Failed to set zram algorithm to zstd."
        exit 1
    fi
else
    echo "Setting zram algorithm to lz4"
    if ! echo lz4 > /sys/block/zram0/comp_algorithm; then
        echo "Failed to set zram algorithm to lz4."
        exit 1
    fi
fi


if [ "$MODE" = "dynamic" ]; then
    echo "Selecting zstd as a secondary algorithm"
    echo "algo=zstd priority=1" > /sys/block/zram0/recomp_algorithm
    if [ $? -ne 0 ]; then
        echo "Failed to set zstd as a secondary algorithm."
        exit 1
    fi
fi

THRESHOLD=50
echo "Setting dynamic threshold to $THRESHOLD%"
echo $THRESHOLD > /sys/block/zram0/dynamic_threshold
if [ $? -ne 0 ]; then
    echo "Failed to set dynamic threshold."
    exit 1
fi

ZRAM_SIZE=6G
echo "Setting zram size to ${ZRAM_SIZE}"
echo $ZRAM_SIZE > /sys/block/zram0/disksize
if [ $? -ne 0 ]; then
    echo "Failed to set zram size to ${ZRAM_SIZE}."
    exit 1
fi

echo "Creating swap space on zram0"
sudo mkswap /dev/zram0
if [ $? -ne 0 ]; then
    echo "Failed to create swap space on zram0."
    exit 1
fi

echo "Enabling swap space on zram0"
sudo swapon /dev/zram0
if [ $? -ne 0 ]; then
    echo "Failed to enable swap space on zram0."
    exit 1
fi

echo "Zram setup completed successfully."