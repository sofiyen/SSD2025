#!/bin/bash

echo "Clean-up before benchmarking..."

swapoff /dev/zram0
echo 1 > /sys/block/zram0/reset
bash /root/final/install_zram.sh
if [ $? -ne 0 ]; then
    echo "install_zram.sh failed, exiting."
    exit 1
fi
pkill -f /root/final/benchmark_worker
rm -rf /root/final/benchmark_worker

gcc -o benchmark_worker benchmark_worker.c

echo "Clean-up DONE!"

PROCESS_COUNT=6
ACTIVE_RATIO=20 # 20 / 80
OOM_DETECTED=0

LOG_FILE="benchmark_log_$(date +%Y%m%d_%H%M%S).txt"
echo "Benchmark Started at $(date)" > $LOG_FILE

for i in $(seq 1 $PROCESS_COUNT); do
    echo "Issued process $i"
    START_TIME=$(date +%s.%N)
    ./benchmark_worker --active-ratio $ACTIVE_RATIO &
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
                echo "[Process $PROCESS_IDX | PID=$PID] OOM kill！" >> $LOG_FILE
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