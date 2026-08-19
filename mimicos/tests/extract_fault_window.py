#!/usr/bin/env python3
"""Cut one page fault out of a recorded MimicOS trace.

tests/fault_driver.cc calls fault_marker() before every fault, so the
recording looks like

    ... setup ... [marker] fault 0 [marker] fault 1 [marker] ...

Everything between two markers is one fault and nothing else.  This
picks one of those windows and writes it out as its own ChampSim trace,
which is what ChampSim injects into the instruction stream when MimicOS
reports a fault.

Skip the first few faults: they warm the allocator's free lists and
take a different path from the steady state.

Usage:
    extract_fault_window.py <recorded.champsimtrace> <marker_addr> <out.champsimtrace>
                            [--fault N] [--marker-size BYTES] [--stats]
"""

import argparse
import struct
import sys
from collections import Counter

RECORD_SIZE = 64
INSTR_FORMAT = "<Q2B2B4B2Q4Q"


def read_records(path):
    with open(path, "rb") as handle:
        while True:
            chunk = handle.read(RECORD_SIZE)
            if len(chunk) < RECORD_SIZE:
                return
            yield chunk


def ip_of(record):
    return struct.unpack_from("<Q", record, 0)[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("trace")
    parser.add_argument("marker_addr", type=lambda x: int(x, 0))
    parser.add_argument("output")
    parser.add_argument("--fault", default="median",
                        help="which fault to extract: an index, or 'median' "
                             "(the default) to pick one of typical length. "
                             "The first few warm the allocator and are not "
                             "representative, so an index picks from the rest.")
    parser.add_argument("--marker-size", type=int, default=64,
                        help="bytes occupied by fault_marker()")
    parser.add_argument("--stats", action="store_true")
    args = parser.parse_args()

    lo = args.marker_addr
    hi = args.marker_addr + args.marker_size

    records = list(read_records(args.trace))
    if not records:
        sys.exit(f"{args.trace}: empty")

    # Group runs of marker instructions; each run is one call.
    marker_runs = []
    in_run = False
    for index, record in enumerate(records):
        inside = lo <= ip_of(record) < hi
        if inside and not in_run:
            marker_runs.append([index, index])
            in_run = True
        elif inside:
            marker_runs[-1][1] = index
        else:
            in_run = False

    if len(marker_runs) < 2:
        sys.exit(f"found {len(marker_runs)} marker calls in {args.trace}; "
                 f"is {hex(args.marker_addr)} the right address? "
                 f"(record with ASLR off)")

    windows = []
    for i in range(len(marker_runs) - 1):
        start = marker_runs[i][1] + 1
        end = marker_runs[i + 1][0]
        if end > start:
            windows.append((start, end))

    if args.stats:
        lengths = [e - s for s, e in windows]
        print(f"{len(windows)} faults recorded")
        if lengths:
            ordered = sorted(lengths)
            print(f"  instructions per fault: min {ordered[0]} "
                  f"median {ordered[len(ordered) // 2]} max {ordered[-1]}")
            print(f"  first five: {lengths[:5]}")
            print(f"  most common: {Counter(lengths).most_common(3)}")

    if args.fault == "median":
        # Drop the warm-up faults, then take the one whose length is the
        # median.  Picking a fixed index can land on an outlier such as a
        # buddy refill, which is not what a typical fault costs.
        warm = windows[4:] if len(windows) > 8 else windows
        by_length = sorted(warm, key=lambda w: w[1] - w[0])
        start, end = by_length[len(by_length) // 2]
        print(f"picked the median fault of {len(warm)} steady-state faults")
    else:
        index = int(args.fault)
        if index >= len(windows):
            sys.exit(f"asked for fault {index} but only {len(windows)} were recorded")
        start, end = windows[index]
    with open(args.output, "wb") as out:
        for record in records[start:end]:
            out.write(record)

    print(f"wrote {end - start} instructions to {args.output}")


if __name__ == "__main__":
    main()
