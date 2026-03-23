#!/usr/bin/env python3
import re
import sys
from pathlib import Path

def parse_lines(text: str):
    total_cycles = 0
    # Regexes for the fields we need
    re_cycles = re.compile(r'cycles≈(\d+)')
    # Fallback cycles via latency (ns) and freq GHz if cycles≈ is absent.
    re_latency_ns = re.compile(r'\blatency=(\d+)\s*ns')
    re_freq = re.compile(r'core=([\d\.]+)\s*GHz')

    # Try to find frequency from the header if we need it
    freq_ghz = None
    for line in text.splitlines():
        m = re_freq.search(line)
        if m:
            try:
                freq_ghz = float(m.group(1))
            except ValueError:
                pass

    for line in text.splitlines():
        # Prefer cycles≈ if present
        mc = re_cycles.search(line)
        if mc:
            total_cycles += int(mc.group(1))
            continue

        # Otherwise compute cycles from latency and frequency if both available
        ml = re_latency_ns.search(line)
        if ml and freq_ghz is not None:
            lat_ns = int(ml.group(1))
            cyc = int(round(lat_ns * freq_ghz))  # cycles ≈ latency(ns) * freq(GHz)
            total_cycles += cyc

    return total_cycles, freq_ghz

def main():
    import argparse
    p = argparse.ArgumentParser(description="Parse pgfault trace and accumulate total fault latency.")
    p.add_argument("input", nargs="?", help="Input file (if omitted, read stdin).")
    args = p.parse_args()

    # Read input text
    if args.input:
        text = Path(args.input).read_text(encoding="utf-8", errors="ignore")
    else:
        text = sys.stdin.read()

    total_cycles, freq_ghz = parse_lines(text)

    if total_cycles == 0:
        print("No page fault latency data found.")
        sys.exit(1)

    if freq_ghz is None:
        print("Warning: No frequency found, cannot convert to milliseconds.")
        print(f"Total page fault latency: {total_cycles} cycles")
    else:
        # Convert cycles to milliseconds: cycles / (freq_ghz * 1e9) * 1000
        total_ms = total_cycles / (freq_ghz * 1e6)
        print(f"Total page fault latency: {total_ms:.3f} ms")

if __name__ == "__main__":
    main()
