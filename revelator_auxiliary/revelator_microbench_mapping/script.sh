#!/bin/bash

# Run the scanner in isolation mode
# sudo ./vm_scan vm_scan_isolation.txt

# # Run the scanner in interference mode
# stress-ng --cpu 4 --timeout 20 &
# sudo ./vm_scan vm_scan_interference.txt
# sleep 20


# # Run the scanner after defragmentation
# sudo sh -c 'echo 1 > /proc/sys/vm/compact_memory'
# sudo ./vm_scan vm_scan_defrag.txt


# Run the scanner after memory overcommit
./hog_memory &
sleep 5
sudo ./vm_scan vm_scan_hogged.txt