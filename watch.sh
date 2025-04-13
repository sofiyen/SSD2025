#!/bin/bash

rm zram_mm_stat.csv

LOGFILE="zram_mm_stat.csv"
INTERVAL=10 #seconds

echo "timestamp,orig_data_size,compr_data_size,mem_used_total,mem_limit,max_used_total,same_pages,pages_compacted" > "$LOGFILE"

while true; do
	now=$(date +'%Y-%m-%d %H:%M:%S')
	mm=($(cat /sys/block/zram0/mm_stat))
	echo "$now,${mm[0]},${mm[1]},${mm[2]},${mm[3]},${mm[4]},${mm[5]},${mm[6]}" >> "$LOGFILE"
	sleep $INTERVAL
done

