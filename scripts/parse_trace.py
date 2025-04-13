# #!/usr/bin/env python3

# import sys
# import re
# from collections import defaultdict

# if len(sys.argv) != 2:
#     print("Usage: python3 parse_trace.py <trace.log>")
#     sys.exit(1)

# trace_path = sys.argv[1]

# with open(trace_path, 'r') as f:
#     lines = f.readlines()


# line_re = re.compile(r'\s+\S+\s+\[\d+\]\s+(\d+\.\d+):\s+(\S+):.*index=(\d+)(?: compressed_size=\d+)?')

# comp_start = {}
# comp_latency = {}

# decomp_start = {}
# decomp_latency = {}

# for line in lines:
#     match = line_re.match(line)
#     if not match:
#         continue

#     timestamp = float(match.group(1))
#     event = match.group(2)
#     index = int(match.group(3))

#     if event == 'zram_comp_start':
#         comp_start[index] = timestamp
#     elif event == 'zram_comp_end' and index in comp_start:
#         comp_latency[index] = timestamp - comp_start[index]
#     elif event == 'zram_decomp_start':
#         decomp_start[index] = timestamp
#     elif event == 'zram_decomp_end' and index in decomp_start:
#         decomp_latency[index] = timestamp - decomp_start[index]

# # 輸出
# all_indices = sorted(set(comp_latency.keys()) | set(decomp_latency.keys()))

# print("index,swap_out_latency,swap_in_latency")
# for idx in all_indices:
#     out_lat = f"{comp_latency[idx]:.6f}" if idx in comp_latency else ""
#     in_lat = f"{decomp_latency[idx]:.6f}" if idx in decomp_latency else ""
#     print(f"{idx},{out_lat},{in_lat}")

#!/usr/bin/env python3

import sys
import re
from collections import defaultdict

if len(sys.argv) != 2:
    print("Usage: python3 parse_trace.py <trace.log>")
    sys.exit(1)

trace_path = sys.argv[1]

with open(trace_path, 'r') as f:
    lines = f.readlines()

line_re = re.compile(r'\s+\S+\s+\[\d+\]\s+(\d+\.\d+):\s+(\S+):.*index=(\d+)(?: compressed_size=\d+)?')

comp_start = {}
comp_latency = {}
decomp_start = {}
decomp_latency = {}

all_comp_times = []

for line in lines:
    match = line_re.match(line)
    if not match:
        continue

    timestamp = float(match.group(1))
    event = match.group(2)
    index = int(match.group(3))

    if event == 'zram_comp_start':
        comp_start[index] = timestamp
    elif event == 'zram_comp_end' and index in comp_start:
        latency = timestamp - comp_start[index]
        comp_latency[index] = latency
        all_comp_times.append((comp_start[index], timestamp))
    elif event == 'zram_decomp_start':
        decomp_start[index] = timestamp
    elif event == 'zram_decomp_end' and index in decomp_start:
        decomp_latency[index] = timestamp - decomp_start[index]

# 輸出詳細 latency 列表
print("index,swap_out_latency,swap_in_latency")
for idx in sorted(set(comp_latency.keys()) | set(decomp_latency.keys())):
    out_lat = f"{comp_latency[idx]:.6f}" if idx in comp_latency else ""
    in_lat = f"{decomp_latency[idx]:.6f}" if idx in decomp_latency else ""
    print(f"{idx},{out_lat},{in_lat}")

# 額外 summary 行
if all_comp_times:
    total_comp_time = sum(end - start for start, end in all_comp_times)
    first_start = min(start for start, _ in all_comp_times)
    last_end = max(end for _, end in all_comp_times)
    duration = last_end - first_start
    compress_cpu_ratio = (total_comp_time / duration) * 100 if duration > 0 else 0.0

    print(f"# total_compress_time={total_comp_time:.6f}")
    print(f"# benchmark_window={duration:.6f}")
    print(f"# compression_cpu_ratio={compress_cpu_ratio:.2f}")
else:
    print("# total_compress_time=0.000000")
    print("# benchmark_window=0.000000")
    print("# compression_cpu_ratio=0.00")

