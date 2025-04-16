# allocate_and_touch.py
import time

size_in_mb = 1024  # 1GB
block_size = 4096
count = (size_in_mb * 1024 * 1024) // block_size

print("[*] Allocating 1GB of anonymous memory...")
a = [bytearray(block_size) for _ in range(count)]

print("[*] Touching all pages to populate and stay alive for swapping...")
for block in a:
    block[0] = 123  # force page allocation

print("[*] Sleeping to allow swap out...")
time.sleep(30)  # let system swap it out
