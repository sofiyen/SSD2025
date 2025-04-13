import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns

# 讀取 summary 檔案
df = pd.read_csv("all_benchmarks_summary.csv")

# 清理欄位名稱（去除單位標記）
df.columns = [col.replace("(µs)", "").replace("(%)", "").strip() for col in df.columns]

# 每個 workload 畫一張圖
workloads = df["workload"].unique()

for workload in workloads:
    subset = df[df["workload"] == workload]

    plt.figure(figsize=(12, 5))

    # 子圖 1: compression CPU ratio
    plt.subplot(1, 2, 1)
    sns.barplot(data=subset, x="algorithm", y="compression_cpu_ratio", palette="Set2")
    plt.title(f"Compression CPU Usage (%) - {workload}")
    plt.ylabel("CPU Usage (%)")
    plt.xlabel("Compression Algorithm")

    # 子圖 2: compression ratio
    plt.subplot(1, 2, 2)
    sns.barplot(data=subset, x="algorithm", y="compression_ratio", palette="Set3")
    plt.title(f"Compression Ratio - {workload}")
    plt.ylabel("Compression Ratio")
    plt.xlabel("Compression Algorithm")

    plt.suptitle(f"Benchmark Comparison for Workload: {workload}", fontsize=14)
    plt.tight_layout(rect=[0, 0.03, 1, 0.95])

    # 儲存成 PNG
    filename = f"plot_{workload}_summary.png"
    plt.savefig(filename)
    print(f"[✓] Saved plot to: {filename}")

    plt.close()  # 關掉畫布避免多圖重疊