#!/usr/bin/env python3

import os
import glob
import pandas as pd
import sys

if len(sys.argv) != 2:
    print("Usage: python3 merge_summary.py <result_dir>")
    sys.exit(1)

result_dir = sys.argv[1]

all_summary_paths = glob.glob(os.path.join(result_dir, "*", "summary.csv"))

if not all_summary_paths:
    print("No summary.csv found.")
    sys.exit(1)

dfs = []

for path in all_summary_paths:
    try:
        df = pd.read_csv(path)
        df["source"] = os.path.basename(os.path.dirname(path))  # e.g. zstd_mem_1713108652
        dfs.append(df)
    except Exception as e:
        print(f"[!] Failed to read {path}: {e}")

if not dfs:
    print("No valid summary.csv found.")
    sys.exit(1)

final_df = pd.concat(dfs, ignore_index=True)

# 排序：先按 workload，再按 algorithm
final_df = final_df.sort_values(by=["workload", "algorithm"])

final_df.to_csv("all_benchmarks_summary.csv", index=False)

print("[✓] Merged and sorted results into all_benchmarks_summary.csv")
