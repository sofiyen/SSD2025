#!/bin/bash

# Usage: ./run_benchmark.sh <workload_type> <algorithm>
# Example: ./run_benchmark.sh mem zstd

set -e

### ------------------------------
### 參數解析與準備
### ------------------------------
WORKLOAD=${1:-mem}
ALGO=${2:-lz4}
TS=$(date +%s)
OUTDIR=result/${ALGO}_${WORKLOAD}_${TS}
ZRAM_DEV=/dev/zram0
ZRAM_SYS=/sys/block/zram0
TRACE_OUT=trace_zram.dat

mkdir -p $OUTDIR
echo "[+] Output directory: $OUTDIR"

### ------------------------------
### 初始化 ZRAM（呼叫你的 script）
### ------------------------------
echo "[*] Initializing ZRAM with algorithm: $ALGO"
./scripts/build_zram.sh $ALGO

### ------------------------------
### 開始 trace
### ------------------------------
echo "[*] Starting trace-cmd recording..."
trace-cmd record -e zram:* -o $OUTDIR/$TRACE_OUT &
TRACE_PID=$!
sleep 1

### ------------------------------
### 執行 workload
### ------------------------------
echo "[*] Running workload: $WORKLOAD"

if [[ "$WORKLOAD" == "cpu" ]]; then
    echo "[*] Launching memory + CPU split-core workload"
    # Run memory pressure on core 0 
    taskset -c 0 stress-ng --vm 2 --vm-bytes 95% --timeout 30s 

    # Run CPU pressure on core 0 in background
    taskset -c 0 stress-ng --cpu 1 --cpu-method matrixprod --timeout 30s &
    CPU_PID=$!

    wait $CPU_PID
elif [[ "$WORKLOAD" == "mem" ]]; then
    stress-ng --vm 4 --vm-bytes 90% --vm-method all --timeout 30s

elif [[ "$WORKLOAD" == "io" ]]; then
    echo "[*] Generating test file..."
    rm -f /tmp/bigfile
    sync; echo 3 > /proc/sys/vm/drop_caches
    dd if=/dev/urandom of=/tmp/bigfile bs=1M count=2048 status=none

    echo "[*] Launching memory pressure (95%)..."
    stress-ng --vm 2 --vm-bytes 95% --timeout 30s &
    STRESS_PID=$!
    sleep 5 
    wait $STRESS_PID
    echo "orig, comp, total_mem, mem_limit, mem_used_max, same_pages, page_compacted, huge_pages, huge_pages_since"
    cat /sys/block/zram0/mm_stat
    # swap-in
    sync; echo 3 > /proc/sys/vm/drop_caches
    echo "[*] Starting memory-active IO workload to trigger swap-in"
    python3 scripts/read_swap.py &
    READ_PID=$!
    sleep 10
    # kill -INT $READ_PID

elif [[ "$WORKLOAD" == "mixed" ]]; then
    stress-ng --cpu 2 --vm 2 --vm-bytes 75% --hdd 1 --timeout 30s
else
    echo "[!] Unknown workload type: $WORKLOAD"
    kill -INT $TRACE_PID
    exit 1
fi

sleep 1

### ------------------------------
### 停止 trace
### ------------------------------
echo "[*] Stopping trace"
kill -INT $TRACE_PID
sleep 2

echo "[*] Saving trace log..."
trace-cmd report -i $OUTDIR/$TRACE_OUT > $OUTDIR/trace.log

### ------------------------------
### 分析 latency
### ------------------------------
echo "[*] Analyzing latency from trace..."
python3 ./scripts/parse_trace.py $OUTDIR/trace.log > $OUTDIR/latency.csv

### --- 平均 CPU 使用率（100 - %idle） ---
COMPRESSION_CPU_RATIO=$(grep "compression_cpu_ratio" $OUTDIR/latency.csv | tail -n1 | awk -F= '{printf "%.2f", $2}')

### ------------------------------
### 擷取 mm_stat
### ------------------------------
echo "[*] Capturing mm_stat..."
cat $ZRAM_SYS/mm_stat > $OUTDIR/mm_stat.txt

### ------------------------------
### 計算壓縮率
### ------------------------------
# 依照欄位順序取第 1 跟第 2 欄
ORIG=$(awk '{print $1}' $OUTDIR/mm_stat.txt)
COMPR=$(awk '{print $2}' $OUTDIR/mm_stat.txt)

# 加入錯誤處理
if [[ -z "$COMPR" || -z "$ORIG" || "$ORIG" == "0" ]]; then
    RATIO="0.0000"
else
    RATIO=$(awk -v c="$COMPR" -v o="$ORIG" 'BEGIN { printf "%.4f", c / o }')
fi


### ------------------------------
### 匯總 latency 為平均值 : 單位 microseconds
### ------------------------------
AVG_SWAP_IN=$(grep avg_swap_in_latency_us $OUTDIR/latency.csv | cut -d= -f2)
AVG_SWAP_OUT=$(grep avg_swap_out_latency_us $OUTDIR/latency.csv | cut -d= -f2)

### ------------------------------
### 輸出 summary.csv
### ------------------------------
echo "workload,algorithm,avg_swap_out_latency(µs),avg_swap_in_latency(µs),compression_ratio,compression_cpu_ratio(%)" > $OUTDIR/summary.csv
echo "$WORKLOAD,$ALGO,$AVG_SWAP_OUT,$AVG_SWAP_IN,$RATIO,$COMPRESSION_CPU_RATIO" >> $OUTDIR/summary.csv

echo "[✓] Benchmark completed. Summary:"
cat $OUTDIR/summary.csv
