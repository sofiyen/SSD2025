#!/bin/bash

MODE=$1
echo "Clean-up before benchmarking..."
pkill -f /root/final/benchmark_worker

swapoff /dev/zram0
while grep -q zram0 /proc/swaps; do
    echo "Waiting for zram0 to fully swapoff..."
    sleep 1
done

echo 1 > /sys/block/zram0/reset
echo 3 | sudo tee /proc/sys/vm/drop_caches
bash /root/final/install_zram.sh $MODE
if [ $? -ne 0 ]; then
    echo "install_zram.sh failed, exiting."
    exit 1
fi

rm -rf /root/final/benchmark_worker
gcc -o benchmark_worker benchmark_worker.c -lm

echo "Clean-up DONE!"

# Config
TOTAL_PROCESS_COUNT=10
INACTIVE_ONLY_COUNT=3
ACTIVE_ONLY_COUNT=$((TOTAL_PROCESS_COUNT - INACTIVE_ONLY_COUNT))
ACTIVE_RATIO=10 # out of 100
PER_PROC_MEM="750MB"

LOG_FILE="/root/final/logs/$MODE/$(date +%Y%m%d_%H%M%S).txt"
mkdir -p "$(dirname "$LOG_FILE")"
echo "Benchmark Started at $(date)" > $LOG_FILE

# Arrays
PIDS=()
START_TIMES=()
declare -A PID_TO_INDEX

# Start inactive-only processes first
for i in $(seq 1 $INACTIVE_ONLY_COUNT); do
    echo "Issued INACTIVE-ONLY process $i"
    START_TIME=$(date +%s.%N)
    ./benchmark_worker --active-ratio $ACTIVE_RATIO --per-process-memory $PER_PROC_MEM --inactive-only &
    PID=$!
    echo "[Process $i | PID=$PID | Mode=INACTIVE-ONLY] Started at $START_TIME" >> $LOG_FILE
    PIDS+=($PID)
    START_TIMES[$i]=$START_TIME
    PID_TO_INDEX[$PID]=$i
done

# Then start active processes
for i in $(seq $((INACTIVE_ONLY_COUNT + 1)) $TOTAL_PROCESS_COUNT); do
    echo "Issued ACTIVE process $i"
    START_TIME=$(date +%s.%N)
    ./benchmark_worker --active-ratio $ACTIVE_RATIO --per-process-memory $PER_PROC_MEM &
    PID=$!
    echo "[Process $i | PID=$PID | Mode=ACTIVE] Started at $START_TIME" >> $LOG_FILE
    PIDS+=($PID)
    START_TIMES[$i]=$START_TIME
    PID_TO_INDEX[$PID]=$i
done

# Ctrl+C trap
trap 'echo "Ctrl+C pressed! Terminating all processes..."; 
      for PID in ${PIDS[@]}; do 
          if kill -0 $PID 2>/dev/null; then 
              kill -TERM $PID 
          fi 
      done; 
      exit 0' SIGINT

# Wait loop
OOM_DETECTED=0
while [ ${#PIDS[@]} -gt 0 ]; do
    wait -n
    EXIT_CODE=$?
    
    for idx in "${!PIDS[@]}"; do
        PID=${PIDS[$idx]}
        if ! kill -0 $PID 2>/dev/null; then
            PROCESS_IDX=${PID_TO_INDEX[$PID]}
            END_TIME=$(date +%s.%N)
            
            if [ $EXIT_CODE -eq 137 ]; then
                STATUS="OOM"
                OOM_DETECTED=1
                echo "[Process $PROCESS_IDX | PID=$PID] OOM kill at $END_TIME！" >> $LOG_FILE
            else
                STATUS="Normal"
                echo "[Process $PROCESS_IDX | PID=$PID] Ended at $END_TIME | Status: $STATUS" >> $LOG_FILE
            fi

            unset PIDS[$idx]
            break
        fi
    done

    if [ $OOM_DETECTED -eq 1 ]; then
        for PID in ${PIDS[@]}; do
            if kill -0 $PID 2>/dev/null; then
                echo "Sending SIGTERM to PID $PID"
                kill -TERM $PID
            fi
        done
        break
    fi
done

echo "Benchmark Finished at $(date)" >> $LOG_FILE
