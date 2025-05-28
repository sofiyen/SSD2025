#!/bin/bash

MODE=$1
echo "Clean-up before benchmarking..."

swapoff /dev/zram0
echo 1 > /sys/block/zram0/reset
echo 3 | sudo tee /proc/sys/vm/drop_caches
bash /root/final/install_zram.sh $MODE
if [ $? -ne 0 ]; then
    echo "install_zram.sh failed, exiting."
    exit 1
fi
pkill -f /root/final/benchmark_worker
rm -rf /root/final/benchmark_worker
gcc -o benchmark_worker benchmark_worker.c -lm

echo "Clean-up DONE!"


PROCESS_COUNT=10
ACTIVE_RATIO=5 # out of 100
OOM_DETECTED=0
PER_PROC_MEM="750MB"

LOG_FILE="/root/final/logs/$MODE/$(date +%Y%m%d_%H%M%S).txt"
echo "Benchmark Started at $(date)" > $LOG_FILE

for i in $(seq 1 $PROCESS_COUNT); do
    echo "Issued process $i"
    START_TIME=$(date +%s.%N)
    ./benchmark_worker --active-ratio $ACTIVE_RATIO --per-process-memory $PER_PROC_MEM &
    PID=$!

    echo "[Process $i | PID=$PID] Started at $START_TIME" >> $LOG_FILE
    PIDS[$((i-1))]=$PID
    START_TIMES[$i]=$START_TIME
done

declare -A PID_TO_INDEX
for i in $(seq 1 $PROCESS_COUNT); do
    idx=$((i-1))
    PID=${PIDS[$idx]}
    PID_TO_INDEX[$PID]=$i
done

# press Enter and terminate all processes
trap 'echo "Ctrl+C pressed! Terminating all processes..."; 
      for PID in ${PIDS[@]}; do 
          if kill -0 $PID 2>/dev/null; then 
              kill -TERM $PID 
          fi 
      done; 
      exit 0' SIGINT

while [ ${#PIDS[@]} -gt 0 ]; do
    wait -n
    EXIT_CODE=$?
    
    for idx in ${!PIDS[@]}; do
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
