# access_anonymous.py
import time

size_in_mb = 1024
block_size = 4096
count = (size_in_mb * 1024 * 1024) // block_size

print("[*] Allocating and accessing again to trigger swap-in...")
a = [bytearray(block_size) for _ in range(count)]

for block in a:
    block[0] = (block[0] + 1) % 256

print("[*] Done.")
