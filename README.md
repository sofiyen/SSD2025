# SSD final project
## Stage 1 : Benchmarking
> ⚠️ Current Problem : all good。

### Run
```bash
./scripts/run_benchmark.sh <workload> <algorithm>
```
- Available workloads :
    1. cpu
    2. mem
    3. io
    4. mixed
- Available algorithms :
    1. lzo 
    2. lzo-rle 
    3. lz4 
    4. lz4hc 
    5. zstd
Results will be shown at : `./result/<algorithm>_<workload>_<...>`.
#### File Structure
```
benchmark
├── README.md
├── all_benchmarks_summary.csv : 所有 test 的 summary 結果。
├── plot_mem_summary.png : 對於某一個 workload (這裏是 mem) 的 summary plot。
└── scripts
    ├── access_anonymous.py 
    ├── allocate_and_touch.py
    ├── build_zram.sh : 開起 zram。
    ├── merge_summary.py : 產生 ../all_benchmarks_summary.csv。
    ├── parse_trace.py : 處理 results/trace.log。
    ├── plot.py : 根據不同 workload，plot ../all_benchmarks.summary.csv。
    └── run_benchmark.sh : 主要的 shell script，用來跑一個 workload + algorithm 組合。
```
### Benchmarks

### Benchmark Conditions
