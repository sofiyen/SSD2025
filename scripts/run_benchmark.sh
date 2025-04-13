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
    fio --name=test --rw=randrw --rwmixread=50 --bs=4k --size=1G --runtime=30s --numjobs=2 --group_reporting
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
AVG_SWAP_OUT=$(awk -F',' '{sum+=$2*1000000} END{if(NR>0) printf "%.0f", sum/NR}' $OUTDIR/latency.csv)
AVG_SWAP_IN=$(awk -F',' '{sum+=$3*1000000} END{if(NR>0) printf "%.0f", sum/NR}' $OUTDIR/latency.csv)

### ------------------------------
### 輸出 summary.csv
### ------------------------------
echo "workload,algorithm,avg_swap_out_latency(µs),avg_swap_in_latency(µs),compression_ratio,compression_cpu_ratio(%)" > $OUTDIR/summary.csv
echo "$WORKLOAD,$ALGO,$AVG_SWAP_OUT,$AVG_SWAP_IN,$RATIO,$COMPRESSION_CPU_RATIO" >> $OUTDIR/summary.csv

echo "[✓] Benchmark completed. Summary:"
cat $OUTDIR/summary.csv
