import subprocess
import re

output = subprocess.check_output(["trace-cmd", "report", "-i", "trace_zram.dat"], text=True)

comp_start = {}
latencies = []

for line in output.splitlines():
    match_start = re.search(r'zram_comp_start.*index=(\d+)', line)
    match_end = re.search(r'zram_comp_end.*index=(\d+).*compressed_size=(\d+)', line)
    time_match = re.search(r' +(\d+\.\d+):', line)

    if time_match:
        timestamp = float(time_match.group(1))

    if match_start:
        index = int(match_start.group(1))
        comp_start[index] = timestamp

    if match_end:
        index = int(match_end.group(1))
        if index in comp_start:
            latency = timestamp - comp_start[index]
            latencies.append((index, comp_start[index], timestamp, latency))
            del comp_start[index]  # avoid duplicate

# 輸出成 CSV
with open("compression_latency.csv", "w") as f:
    f.write("index,start,end,latency_sec\n")
    for idx, start, end, latency in latencies:
        f.write(f"{idx},{start},{end},{latency:.9f}\n")

print(f"[✔] 轉換完成，共 {len(latencies)} 筆壓縮事件，已儲存為 compression_latency.csv")
