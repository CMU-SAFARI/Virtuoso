#!/usr/bin/env python3
"""
analyze.py -- Post-process eBPF page-fault trace output.

Reads the per-event output produced by the ``instrument`` eBPF loader and
generates:
  1. Latency histogram with percentile annotations
  2. Fault-reason breakdown (ANON / FILE / COW / SWAP / UNKNOWN)
  3. LLC-misses-per-fault histogram
  4. Summary statistics printed to stdout

Usage:
    python3 analyze.py <trace_file> [--out-dir DIR]

The trace file is the stdout captured from:
    sudo ./instrument --aggregate 1000 2>&1 | tee ebpf_trace.log
"""

import argparse
import os
import re
import sys
from collections import Counter
from pathlib import Path

import numpy as np

# ---------------------------------------------------------------------------
# Matplotlib -- imported lazily so the script can still print stats even when
# running on a headless machine without a display.
# ---------------------------------------------------------------------------
_MPL_AVAILABLE = True
try:
    import matplotlib
    matplotlib.use("Agg")  # non-interactive backend
    import matplotlib.pyplot as plt
except ImportError:
    _MPL_AVAILABLE = False


# ---------------------------------------------------------------------------
# Regex patterns that match the per-event lines emitted by instrument.c
# ---------------------------------------------------------------------------
RE_LATENCY = re.compile(r"\blatency=(\d+)\s*ns")
RE_LLC     = re.compile(r"\bllc=(\d+)")
RE_INSN    = re.compile(r"\binsn=(\d+)")
RE_REASON  = re.compile(r"\breason=(\w+)")


def parse_trace(text: str):
    """Return parallel arrays of latency_ns, llc_misses, insn_retired and a
    list of reason strings parsed from the raw trace text."""
    latencies = []
    llc_misses = []
    instructions = []
    reasons = []

    for line in text.splitlines():
        m_lat = RE_LATENCY.search(line)
        if m_lat is None:
            continue  # skip header / aggregation lines

        latencies.append(int(m_lat.group(1)))

        m_llc = RE_LLC.search(line)
        llc_misses.append(int(m_llc.group(1)) if m_llc else 0)

        m_insn = RE_INSN.search(line)
        instructions.append(int(m_insn.group(1)) if m_insn else 0)

        m_reason = RE_REASON.search(line)
        reasons.append(m_reason.group(1) if m_reason else "UNKNOWN")

    return (
        np.array(latencies, dtype=np.int64),
        np.array(llc_misses, dtype=np.int64),
        np.array(instructions, dtype=np.int64),
        reasons,
    )


# ---------------------------------------------------------------------------
# Plotting helpers
# ---------------------------------------------------------------------------

REASON_COLORS = {
    "ANON":    "#4c72b0",
    "FILE":    "#55a868",
    "COW":     "#c44e52",
    "SWAP":    "#8172b2",
    "UNKNOWN": "#ccb974",
}


def _ensure_dir(path: str):
    os.makedirs(path, exist_ok=True)


def plot_latency_histogram(latencies_ns, out_dir: str):
    """Plot a histogram of per-fault latencies (in microseconds)."""
    lat_us = latencies_ns / 1000.0

    fig, ax = plt.subplots(figsize=(7, 4))

    # Clip extreme outliers for the histogram bins
    p999 = np.percentile(lat_us, 99.9)
    clipped = lat_us[lat_us <= p999]
    ax.hist(clipped, bins=100, color="#4c72b0", edgecolor="white", linewidth=0.3)

    # Percentile markers
    for pct, color, ls in [(50, "black", "--"), (90, "#ff7f0e", "-."), (99, "#d62728", ":")]:
        val = np.percentile(lat_us, pct)
        ax.axvline(val, color=color, linestyle=ls, linewidth=1.2,
                   label=f"p{pct} = {val:.1f} us")

    mean_val = np.mean(lat_us)
    ax.axvline(mean_val, color="green", linestyle="-", linewidth=1.2,
               label=f"mean = {mean_val:.1f} us")

    ax.set_xlabel("Page fault latency (us)")
    ax.set_ylabel("Count")
    ax.set_title("Minor Page Fault Latency Distribution")
    ax.legend(fontsize=8)
    fig.tight_layout()

    path = os.path.join(out_dir, "latency_histogram.png")
    fig.savefig(path, dpi=200)
    plt.close(fig)
    print(f"  Saved: {path}")


def plot_reason_breakdown(reasons, out_dir: str):
    """Bar chart of fault reasons."""
    counts = Counter(reasons)
    labels = sorted(counts.keys())
    values = [counts[l] for l in labels]
    colors = [REASON_COLORS.get(l, "#999999") for l in labels]

    fig, ax = plt.subplots(figsize=(5, 3.5))
    bars = ax.bar(labels, values, color=colors, edgecolor="white", linewidth=0.5)

    total = sum(values)
    for bar, val in zip(bars, values):
        pct = 100.0 * val / total if total else 0
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height(),
                f"{pct:.1f}%", ha="center", va="bottom", fontsize=8)

    ax.set_xlabel("Fault Reason")
    ax.set_ylabel("Count")
    ax.set_title("Page Fault Reason Breakdown")
    fig.tight_layout()

    path = os.path.join(out_dir, "reason_breakdown.png")
    fig.savefig(path, dpi=200)
    plt.close(fig)
    print(f"  Saved: {path}")


def plot_llc_histogram(llc_misses, out_dir: str):
    """Histogram of LLC misses observed per fault."""
    fig, ax = plt.subplots(figsize=(6, 3.5))

    max_val = int(np.percentile(llc_misses, 99.5)) + 1 if len(llc_misses) else 10
    bins = np.arange(0, max_val + 1) - 0.5
    ax.hist(llc_misses, bins=bins, color="#55a868", edgecolor="white", linewidth=0.3)

    mean_llc = np.mean(llc_misses)
    ax.axvline(mean_llc, color="red", linestyle="--", linewidth=1.2,
               label=f"mean = {mean_llc:.2f}")

    ax.set_xlabel("LLC Misses per Fault")
    ax.set_ylabel("Count")
    ax.set_title("LLC Misses During Minor Page Faults")
    ax.legend(fontsize=8)
    fig.tight_layout()

    path = os.path.join(out_dir, "llc_per_fault.png")
    fig.savefig(path, dpi=200)
    plt.close(fig)
    print(f"  Saved: {path}")


# ---------------------------------------------------------------------------
# Summary statistics
# ---------------------------------------------------------------------------

def print_summary(latencies_ns, llc_misses, instructions, reasons):
    """Print human-readable summary statistics to stdout."""
    n = len(latencies_ns)
    if n == 0:
        print("No events parsed.")
        return

    lat_us = latencies_ns / 1000.0

    print("=" * 64)
    print(f"  eBPF Page Fault Trace -- Summary ({n:,} events)")
    print("=" * 64)

    print("\n  Latency (us):")
    print(f"    Mean   = {np.mean(lat_us):10.2f}")
    print(f"    Median = {np.median(lat_us):10.2f}")
    print(f"    p90    = {np.percentile(lat_us, 90):10.2f}")
    print(f"    p99    = {np.percentile(lat_us, 99):10.2f}")
    print(f"    Min    = {np.min(lat_us):10.2f}")
    print(f"    Max    = {np.max(lat_us):10.2f}")

    print("\n  LLC Misses per Fault:")
    print(f"    Mean   = {np.mean(llc_misses):10.2f}")
    print(f"    Median = {np.median(llc_misses):10.2f}")
    print(f"    Max    = {np.max(llc_misses):10.0f}")

    print("\n  Instructions per Fault:")
    print(f"    Mean   = {np.mean(instructions):10.0f}")
    print(f"    Median = {np.median(instructions):10.0f}")
    print(f"    p99    = {np.percentile(instructions, 99):10.0f}")

    counts = Counter(reasons)
    print("\n  Fault Reason Breakdown:")
    for reason in sorted(counts.keys()):
        cnt = counts[reason]
        pct = 100.0 * cnt / n
        print(f"    {reason:8s}  {cnt:>8,}  ({pct:5.1f}%)")

    total_us = np.sum(lat_us)
    print(f"\n  Total accumulated fault latency: {total_us:,.0f} us"
          f"  ({total_us / 1e6:.3f} s)")
    print("=" * 64)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Analyze eBPF page-fault trace output."
    )
    parser.add_argument("input", nargs="?",
                        help="Trace file (stdin if omitted)")
    parser.add_argument("--out-dir", default="plots",
                        help="Directory for plot output (default: plots/)")
    args = parser.parse_args()

    # Read trace data
    if args.input:
        text = Path(args.input).read_text(encoding="utf-8", errors="ignore")
    else:
        text = sys.stdin.read()

    latencies, llc_misses, instructions, reasons = parse_trace(text)

    if len(latencies) == 0:
        print("ERROR: No page-fault events found in input.", file=sys.stderr)
        print("Make sure the input is the stdout of `./instrument`.",
              file=sys.stderr)
        sys.exit(1)

    # Always print summary
    print_summary(latencies, llc_misses, instructions, reasons)

    # Generate plots if matplotlib is available
    if _MPL_AVAILABLE:
        _ensure_dir(args.out_dir)
        print(f"\nGenerating plots in {args.out_dir}/")
        plot_latency_histogram(latencies, args.out_dir)
        plot_reason_breakdown(reasons, args.out_dir)
        plot_llc_histogram(llc_misses, args.out_dir)
    else:
        print("\nmatplotlib not available -- skipping plot generation.",
              file=sys.stderr)


if __name__ == "__main__":
    main()
