#!/usr/bin/env python3
"""
compare.py -- Compare baseline vs HW-fault-handler Virtuoso simulation results.

Reads sim.stats (or sim.out) from two result directories and produces:
  1. IPC comparison bar chart
  2. Page fault latency comparison
  3. Translation time breakdown (TLB + PTW + fault)

Usage:
    python3 compare.py <baseline_dir> <hwfaults_dir> [--out-dir DIR]
"""

import argparse
import os
import re
import sys
from pathlib import Path

import numpy as np

# ---------------------------------------------------------------------------
_MPL_AVAILABLE = True
try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError:
    _MPL_AVAILABLE = False


# ---------------------------------------------------------------------------
# Stats parsing
# ---------------------------------------------------------------------------

def parse_sim_out(results_dir: str) -> dict:
    """Parse the human-readable sim.out for key metrics."""
    metrics = {}
    sim_out = os.path.join(results_dir, "sim.out")
    if not os.path.isfile(sim_out):
        return metrics

    text = Path(sim_out).read_text(errors="ignore")
    for line in text.splitlines():
        # IPC line: "IPC  0.1234"
        if "IPC" in line:
            m = re.search(r"IPC\s+([\d.]+)", line)
            if m:
                metrics["ipc"] = float(m.group(1))
        # Cycles
        if "Cycles" in line:
            m = re.search(r"Cycles\s+([\d]+)", line)
            if m:
                metrics["cycles"] = int(m.group(1))
    return metrics


def parse_sim_stats(results_dir: str) -> dict:
    """Parse sim.stats for MMU-specific counters.

    The Sniper sim.stats file is a sequence of lines like:
        <name>  <core>  <value>
    We look for mmu.* metrics on core 0.
    """
    metrics = {}
    stats_file = os.path.join(results_dir, "sim.stats")
    if not os.path.isfile(stats_file):
        return metrics

    text = Path(stats_file).read_text(errors="ignore")
    for line in text.splitlines():
        parts = line.strip().split()
        if len(parts) < 2:
            continue
        name = parts[0]
        # Take the last numeric token as the value
        try:
            val = int(parts[-1])
        except ValueError:
            try:
                val = float(parts[-1])
            except ValueError:
                continue

        if "page_faults" in name:
            metrics.setdefault("page_faults", val)
        elif "total_walk_latency" in name and "total_walk_latency" not in metrics:
            metrics["total_walk_latency"] = val
        elif "total_translation_latency" in name and "total_translation_latency" not in metrics:
            metrics["total_translation_latency"] = val
        elif "total_tlb_latency" in name and "total_tlb_latency" not in metrics:
            metrics["total_tlb_latency"] = val
        elif "total_fault_latency" in name and "total_fault_latency" not in metrics:
            metrics["total_fault_latency"] = val
        elif "num_translations" in name and "num_translations" not in metrics:
            metrics["num_translations"] = val
        elif "page_table_walks" in name and "page_table_walks" not in metrics:
            metrics["page_table_walks"] = val

    return metrics


def load_results(results_dir: str) -> dict:
    """Load and merge sim.out + sim.stats into a single dict."""
    d = parse_sim_out(results_dir)
    d.update(parse_sim_stats(results_dir))
    return d


# ---------------------------------------------------------------------------
# Plotting
# ---------------------------------------------------------------------------

COLORS = ["#4c72b0", "#c44e52"]
LABELS = ["Baseline", "HW Faults"]


def _ensure_dir(path: str):
    os.makedirs(path, exist_ok=True)


def plot_ipc_comparison(base: dict, hw: dict, out_dir: str):
    """Bar chart comparing IPC."""
    ipc_base = base.get("ipc", 0)
    ipc_hw = hw.get("ipc", 0)

    if ipc_base == 0 and ipc_hw == 0:
        print("  Skipping IPC plot (no data).")
        return

    fig, ax = plt.subplots(figsize=(4, 3.5))
    bars = ax.bar(LABELS, [ipc_base, ipc_hw], color=COLORS, edgecolor="white", width=0.5)

    for bar, val in zip(bars, [ipc_base, ipc_hw]):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 0.002,
                f"{val:.4f}", ha="center", va="bottom", fontsize=9)

    ax.set_ylabel("IPC")
    ax.set_title("IPC: Baseline vs HW Fault Handler")
    ax.set_ylim(bottom=0)
    fig.tight_layout()

    path = os.path.join(out_dir, "ipc_comparison.png")
    fig.savefig(path, dpi=200)
    plt.close(fig)
    print(f"  Saved: {path}")


def plot_fault_latency(base: dict, hw: dict, out_dir: str):
    """Bar chart comparing total fault latency."""
    fl_base = base.get("total_fault_latency", 0)
    fl_hw = hw.get("total_fault_latency", 0)

    if fl_base == 0 and fl_hw == 0:
        print("  Skipping fault latency plot (no data).")
        return

    fig, ax = plt.subplots(figsize=(4, 3.5))
    bars = ax.bar(LABELS, [fl_base, fl_hw], color=COLORS, edgecolor="white", width=0.5)

    for bar, val in zip(bars, [fl_base, fl_hw]):
        label = f"{val:,.0f}" if val < 1e6 else f"{val / 1e6:.2f}M"
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height(),
                label, ha="center", va="bottom", fontsize=8)

    ax.set_ylabel("Total Fault Latency (cycles)")
    ax.set_title("Page Fault Latency Comparison")
    ax.set_ylim(bottom=0)
    fig.tight_layout()

    path = os.path.join(out_dir, "fault_latency_comparison.png")
    fig.savefig(path, dpi=200)
    plt.close(fig)
    print(f"  Saved: {path}")


def plot_translation_breakdown(base: dict, hw: dict, out_dir: str):
    """Stacked bar chart: TLB + PTW + Fault latency breakdown."""
    def get_breakdown(d):
        total_trans = d.get("total_translation_latency", 0)
        total_tlb = d.get("total_tlb_latency", 0)
        total_walk = d.get("total_walk_latency", 0)
        total_fault = d.get("total_fault_latency", 0)

        # If we have total_translation but not the components, estimate
        ptw = total_walk
        fault = total_fault
        tlb = total_tlb
        other = max(0, total_trans - tlb - ptw - fault)
        return tlb, ptw, fault, other

    tlb_b, ptw_b, fault_b, other_b = get_breakdown(base)
    tlb_h, ptw_h, fault_h, other_h = get_breakdown(hw)

    if all(v == 0 for v in [tlb_b, ptw_b, fault_b, tlb_h, ptw_h, fault_h]):
        print("  Skipping translation breakdown plot (no data).")
        return

    fig, ax = plt.subplots(figsize=(5, 4))
    x = np.arange(2)
    width = 0.45

    component_colors = ["#4c72b0", "#55a868", "#c44e52", "#ccb974"]
    component_labels = ["TLB", "PTW", "Fault", "Other"]

    bottoms_b = [0, 0]
    for i, (vals, color, label) in enumerate(zip(
        [(tlb_b, tlb_h), (ptw_b, ptw_h), (fault_b, fault_h), (other_b, other_h)],
        component_colors, component_labels
    )):
        ax.bar(x, vals, width, bottom=bottoms_b, color=color, label=label, edgecolor="white")
        bottoms_b = [bottoms_b[j] + vals[j] for j in range(2)]

    ax.set_xticks(x)
    ax.set_xticklabels(LABELS)
    ax.set_ylabel("Latency (cycles)")
    ax.set_title("Translation Time Breakdown")
    ax.legend(fontsize=8)
    fig.tight_layout()

    path = os.path.join(out_dir, "translation_breakdown.png")
    fig.savefig(path, dpi=200)
    plt.close(fig)
    print(f"  Saved: {path}")


# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

def print_comparison(base: dict, hw: dict):
    """Print a textual comparison table."""
    print("=" * 66)
    print("  Virtuoso Simulation Comparison: Baseline vs HW Fault Handler")
    print("=" * 66)

    rows = [
        ("IPC", "ipc", ".4f"),
        ("Num Translations", "num_translations", ",d"),
        ("Page Table Walks", "page_table_walks", ",d"),
        ("Page Faults", "page_faults", ",d"),
        ("Total TLB Latency (cyc)", "total_tlb_latency", ",d"),
        ("Total Walk Latency (cyc)", "total_walk_latency", ",d"),
        ("Total Fault Latency (cyc)", "total_fault_latency", ",d"),
        ("Total Translation Lat (cyc)", "total_translation_latency", ",d"),
    ]

    header = f"  {'Metric':<30s}  {'Baseline':>15s}  {'HW Faults':>15s}  {'Delta':>10s}"
    print(header)
    print(f"  {'-'*30}  {'-'*15}  {'-'*15}  {'-'*10}")

    for label, key, fmt in rows:
        v_base = base.get(key, None)
        v_hw = hw.get(key, None)

        def fmt_val(v, fmt_str):
            if v is None:
                return "N/A"
            return f"{v:{fmt_str}}"

        s_base = fmt_val(v_base, fmt)
        s_hw = fmt_val(v_hw, fmt)

        delta = ""
        if v_base is not None and v_hw is not None and v_base != 0:
            pct = 100.0 * (v_hw - v_base) / abs(v_base)
            delta = f"{pct:+.1f}%"

        print(f"  {label:<30s}  {s_base:>15s}  {s_hw:>15s}  {delta:>10s}")

    print("=" * 66)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Compare baseline vs HW-fault-handler simulation results."
    )
    parser.add_argument("baseline_dir", help="Path to baseline results directory")
    parser.add_argument("hwfaults_dir", help="Path to HW faults results directory")
    parser.add_argument("--out-dir", default="plots",
                        help="Directory for plot output (default: plots/)")
    args = parser.parse_args()

    for d in [args.baseline_dir, args.hwfaults_dir]:
        if not os.path.isdir(d):
            print(f"ERROR: Results directory not found: {d}", file=sys.stderr)
            sys.exit(1)

    base = load_results(args.baseline_dir)
    hw = load_results(args.hwfaults_dir)

    # Print comparison
    print_comparison(base, hw)

    # Generate plots
    if _MPL_AVAILABLE:
        _ensure_dir(args.out_dir)
        print(f"\nGenerating plots in {args.out_dir}/")
        plot_ipc_comparison(base, hw, args.out_dir)
        plot_fault_latency(base, hw, args.out_dir)
        plot_translation_breakdown(base, hw, args.out_dir)
    else:
        print("\nmatplotlib not available -- skipping plot generation.",
              file=sys.stderr)


if __name__ == "__main__":
    main()
