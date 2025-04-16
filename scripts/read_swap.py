# read_swap.py
import mmap
import os

file_path = "/tmp/bigfile"
page_size = mmap.PAGESIZE

with open(file_path, "rb") as f:
    size = os.path.getsize(file_path)
    mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
    for i in range(0, size, page_size):
        _ = mm[i]  # 強制觸發每一頁 page fault
    mm.close()
