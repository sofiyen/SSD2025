#!/bin/bash

dmesg -C

dmesg -w > dmesg_benchmark_log.txt &
DMESG_PID=$!

MODE="zstd"
bash /root/final/run_benchmark.sh $MODE

kill $DMESG_PID
