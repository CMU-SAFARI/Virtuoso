#!/usr/bin/env python3

import sys
import pdb

def merge_intervals(intervals):
    """
    Merge a list of intervals (start, end) and return a new list
    of merged intervals covering the union of all.
    """
    if not intervals:
        return []

    # Sort intervals by starting address
    intervals.sort(key=lambda x: x[0])
    merged = []

    current_start, current_end = intervals[0]
    print(f"Initial: {current_start:012x}-{current_end:012x}")
    for i in range(1, len(intervals)):
        start, end = intervals[i]

        # print (f"Checking: {start:012x}-{end:012x}")

        if (start <= current_start) and (end >= current_end):
            # Current interval is completely contained in the new interval
            # print (f"Completely contained current: {start:012x}-{end:012x}")
            current_start, current_end = start, end
        if (start < current_start) and (end > current_start) and (end < current_end):
            # Overlap with start of new interval
            # print (f"Overlap with start: {start:012x}-{end:012x}")
            current_start = start
        if (current_start <= start) and (start < current_end) and (end > current_end):
            # Overlap with end of new interval
            # print (f"Overlap with end: {start:012x}-{end:012x}")
            current_end = end
        if (start >= current_end) or (end <= current_start):
            # print (f"No overlap: {start:012x}-{end:012x}")
            merged.append((current_start, current_end))
            current_start, current_end = start, end

        # print (f"New Current: {current_start:012x}-{current_end:012x}")
        # print (f"Current: {current_start:012x}-{current_end:012x}")

    # Append the last interval
    merged.append((current_start, current_end))
    return merged

def main(file_path):
    intervals = []

    # Read lines, parse hex ranges
    with open(file_path, "r") as f:
        for line in f:
            line = line.strip()
            # Skip lines like "VMA: 0" or "VMA: 10000"
            if line.startswith("VMA:"):
                continue
            if not line:
                continue

            # Lines of the form: 55930f0b5000-55930f0b9000
            start_str, end_str = line.split('-')
            start_addr = int(start_str, 16)
            end_addr  = int(end_str, 16)

            intervals.append((start_addr, end_addr))

    # Remove duplicates
    intervals = list(set(intervals))

    # Merge overlapping intervals
    merged = merge_intervals(intervals)

    # Print out the merged superset in hex
    for (s, e) in merged:
        print(f"{s:012x}-{e:012x}")

    # Write out the merged superset to a file
    with open(file_path + ".merged", "w") as f:
        for (s, e) in merged:
            f.write(f"{s:012x}-{e:012x}\n")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} /path/to/vma_file")
        sys.exit(1)
    main(sys.argv[1])
