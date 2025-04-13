#!/bin/bash


OUTPUT=trace_zram.dat
DURATION=30  # trace + stress-ng 執行秒數

echo "[*] Cleaning old trace-cmd data..."
sudo trace-cmd reset

echo "[*] Recording tracepoints..."
sudo trace-cmd record -e zram:zram_comp_start -e zram:zram_comp_end \
                      -o $OUTPUT &

TRACECMD_PID=$!

echo "[*] Running stress-ng to trigger zram swap..."
stress-ng --vm 8 --vm-bytes 100% --timeout ${DURATION}s
sleep 2

echo "[*] Stopping trace-cmd..."
sudo kill -INT $TRACECMD_PID
sleep 5

echo "[*] Trace saved to $OUTPUT"
