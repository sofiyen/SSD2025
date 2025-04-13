import pandas as pd
import matplotlib.pyplot as plt

# 讀取 CSV
df = pd.read_csv("compression_latency.csv")

# 轉換單位：秒 → 毫秒（方便閱讀）
df["latency_ms"] = df["latency_sec"] * 1000

# 畫 histogram
plt.figure(figsize=(10, 6))
plt.hist(df["latency_ms"], bins=50, color='skyblue', edgecolor='black')
plt.title("ZRAM Compression Latency Distribution")
plt.xlabel("Latency (ms)")
plt.ylabel("Number of Events")
plt.grid(True)
plt.tight_layout()
plt.savefig("compression_latency_histogram.png")
plt.show()
