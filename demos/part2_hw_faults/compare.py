#!/usr/bin/env python3
"""
compare.py -- Compare baseline vs HW-fault-handler Virtuoso simulation results.

Reads sim.stats and sim.out from two result directories and produces:
  1. IPC comparison bar chart
  2. Fault latency breakdown (HW-handled vs OS-handled vs baseline)
  3. Translation time breakdown (TLB + PTW + Fault)
  4. Fault coverage pie chart (HW-handled vs OS-handled)
  5. Summary table to stdout

Usage:
    python3 compare.py <baseline_dir> <hwfaults_dir> [--out-dir DIR]
"""

import argparse
import os
import re
import sys
from pathlib import Path

import numpy as np

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

def _find_file(results_dir, name):
    for subdir in ["simulation", ""]:
        p = os.path.join(results_dir, subdir, name) if subdir else os.path.join(results_dir, name)
        if os.path.isfile(p):
            return p
    return None


def parse_sim_out(results_dir):
    metrics = {}
    sim_out = _find_file(results_dir, "sim.out")
    if not sim_out:
        return metrics
    for line in Path(sim_out).read_text(errors="ignore").splitlines():
        if "IPC" in line and "ipc" not in metrics:
            m = re.search(r"([\d.]+)\s*$", line)
            if m:
                metrics["ipc"] = float(m.group(1))
        if "Cycles" in line and "cycles" not in metrics:
            m = re.search(r"(\d+)\s*$", line)
            if m:
                metrics["cycles"] = int(m.group(1))
    return metrics


def parse_sim_stats(results_dir):
    metrics = {}
    stats_file = _find_file(results_dir, "sim.stats")
    if not stats_file:
        return metrics
    for line in Path(stats_file).read_text(errors="ignore").splitlines():
        if not line.startswith("mmu."):
            continue
        parts = line.split("=", 1)
        if len(parts) != 2:
            continue
        key = parts[0].strip().replace("mmu.", "")
        try:
            val = int(parts[1].strip())
        except ValueError:
            try:
                val = float(parts[1].strip())
            except ValueError:
                continue
        if key not in metrics:
            metrics[key] = val
    return metrics


def load_results(results_dir):
    d = parse_sim_out(results_dir)
    d.update(parse_sim_stats(results_dir))
    return d


# ---------------------------------------------------------------------------
# Plotting
# ---------------------------------------------------------------------------

C_BLUE, C_RED, C_GREEN, C_ORANGE, C_GRAY = "#4c72b0", "#c44e52", "#55a868", "#ff7f0e", "#ccb974"


def _ensure_dir(path):
    os.makedirs(path, exist_ok=True)


def _to_T(v):
    """Convert raw SubsecondTime (femtoseconds) to teracycles-ish for display."""
    return v / 1e12


def plot_ipc(base, hw, out_dir):
    ipc_b, ipc_h = base.get("ipc", 0), hw.get("ipc", 0)
    if ipc_b == 0 and ipc_h == 0:
        print("  Skipped: ipc_comparison.png (no data)"); return

    fig, ax = plt.subplots(figsize=(4, 3.5))
    bars = ax.bar(["Baseline", "HW Faults"], [ipc_b, ipc_h],
                  color=[C_BLUE, C_GREEN], edgecolor="white", width=0.5)
    for bar, val in zip(bars, [ipc_b, ipc_h]):
        ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.001,
                f"{val:.2f}", ha="center", va="bottom", fontsize=9)
    ax.set_ylabel("IPC"); ax.set_title("IPC: Baseline vs HW Fault Handler"); ax.set_ylim(bottom=0)
    fig.tight_layout()
    p = os.path.join(out_dir, "ipc_comparison.png"); fig.savefig(p, dpi=200); plt.close(fig); print(f"  Saved: {p}")


def plot_fault_latency(base, hw, out_dir):
    fl_b = base.get("total_fault_latency", 0)
    fl_h = hw.get("total_fault_latency", 0)
    fl_hw = hw.get("total_hw_fault_latency", 0)
    fl_os = fl_h - fl_hw
    if fl_b == 0 and fl_h == 0:
        print("  Skipped: fault_latency.png (no data)"); return

    fig, ax = plt.subplots(figsize=(5, 4))
    ax.bar(0, _to_T(fl_b), color=C_RED, edgecolor="white", width=0.4, label="OS fault handling")
    ax.bar(1, _to_T(fl_hw), color=C_GREEN, edgecolor="white", width=0.4, label="HW fault handling")
    ax.bar(1, _to_T(fl_os), bottom=_to_T(fl_hw), color=C_RED, edgecolor="white", width=0.4)
    ax.set_xticks([0, 1]); ax.set_xticklabels(["Baseline", "HW Faults"])
    ax.set_ylabel("Total Fault Latency (T cycles)"); ax.set_title("Page Fault Latency Breakdown")
    ax.legend(fontsize=8); fig.tight_layout()
    p = os.path.join(out_dir, "fault_latency.png"); fig.savefig(p, dpi=200); plt.close(fig); print(f"  Saved: {p}")


def plot_translation_breakdown(base, hw, out_dir):
    g = lambda d, k: d.get(k, 0)
    tlb_b, ptw_b, f_b = g(base, "total_tlb_latency"), g(base, "total_table_walk_latency"), g(base, "total_fault_latency")
    tlb_h, ptw_h, f_h = g(hw, "total_tlb_latency"), g(hw, "total_table_walk_latency"), g(hw, "total_fault_latency")
    if all(v == 0 for v in [tlb_b, ptw_b, f_b, tlb_h, ptw_h, f_h]):
        print("  Skipped: translation_breakdown.png (no data)"); return

    fig, ax = plt.subplots(figsize=(5, 4))
    x, w = [0, 1], 0.4
    ax.bar(x, [_to_T(tlb_b), _to_T(tlb_h)], w, color=C_BLUE, label="TLB")
    ax.bar(x, [_to_T(ptw_b), _to_T(ptw_h)], w,
           bottom=[_to_T(tlb_b), _to_T(tlb_h)], color=C_ORANGE, label="PTW")
    ax.bar(x, [_to_T(f_b), _to_T(f_h)], w,
           bottom=[_to_T(tlb_b+ptw_b), _to_T(tlb_h+ptw_h)], color=C_RED, label="Fault")
    ax.set_xticks(x); ax.set_xticklabels(["Baseline", "HW Faults"])
    ax.set_ylabel("Latency (T cycles)"); ax.set_title("Translation Latency Breakdown")
    ax.legend(fontsize=8); fig.tight_layout()
    p = os.path.join(out_dir, "translation_breakdown.png"); fig.savefig(p, dpi=200); plt.close(fig); print(f"  Saved: {p}")


def plot_fault_coverage(hw, out_dir):
    hw_h = hw.get("page_faults_hw_handled", 0)
    os_h = hw.get("page_faults_os_handled", 0)
    total = hw_h + os_h
    if total == 0:
        print("  Skipped: fault_coverage.png (no data)"); return

    fig, ax = plt.subplots(figsize=(4, 4))
    ax.pie([hw_h, os_h],
           labels=[f"HW Pool\n{hw_h:,} ({100*hw_h/total:.1f}%)",
                   f"OS Trap\n{os_h:,} ({100*os_h/total:.1f}%)"],
           colors=[C_GREEN, C_RED], startangle=90, textprops={"fontsize": 9})
    ax.set_title(f"Fault Coverage ({total:,} total)"); fig.tight_layout()
    p = os.path.join(out_dir, "fault_coverage.png"); fig.savefig(p, dpi=200); plt.close(fig); print(f"  Saved: {p}")


# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

def print_comparison(base, hw):
    print("=" * 72)
    print("  Virtuoso Simulation: Baseline vs HW Fault Handler")
    print("=" * 72)
    rows = [
        ("IPC", "ipc", ".2f"),
        ("Num Translations", "num_translations", ",d"),
        ("Page Table Walks", "page_table_walks", ",d"),
        ("Page Faults (total)", "page_faults", ",d"),
        ("  HW-handled faults", "page_faults_hw_handled", ",d"),
        ("  OS-handled faults", "page_faults_os_handled", ",d"),
        ("Total TLB Latency", "total_tlb_latency", ",d"),
        ("Total Walk Latency", "total_table_walk_latency", ",d"),
        ("Total Fault Latency", "total_fault_latency", ",d"),
        ("  HW Fault Latency", "total_hw_fault_latency", ",d"),
        ("Total Translation Latency", "total_translation_latency", ",d"),
    ]
    fmt_h = f"  {'Metric':<30s}  {'Baseline':>18s}  {'HW Faults':>18s}  {'Delta':>10s}"
    print(fmt_h)
    print(f"  {'-'*30}  {'-'*18}  {'-'*18}  {'-'*10}")
    for label, key, fmt in rows:
        v_b, v_h = base.get(key), hw.get(key)
        s_b = "N/A" if v_b is None else f"{v_b:{fmt}}"
        s_h = "N/A" if v_h is None else f"{v_h:{fmt}}"
        delta = ""
        if v_b is not None and v_h is not None and v_b != 0:
            delta = f"{100*(v_h-v_b)/abs(v_b):+.1f}%"
        print(f"  {label:<30s}  {s_b:>18s}  {s_h:>18s}  {delta:>10s}")
    print("=" * 72)


def main():
    parser = argparse.ArgumentParser(description="Compare baseline vs HW fault handler results.")
    parser.add_argument("baseline_dir")
    parser.add_argument("hwfaults_dir")
    parser.add_argument("--out-dir", default="plots")
    args = parser.parse_args()

    for d in [args.baseline_dir, args.hwfaults_dir]:
        if not os.path.isdir(d):
            print(f"ERROR: {d} not found", file=sys.stderr); sys.exit(1)

    base, hw = load_results(args.baseline_dir), load_results(args.hwfaults_dir)
    print_comparison(base, hw)

    if _MPL_AVAILABLE:
        _ensure_dir(args.out_dir)
        print(f"\nGenerating plots in {args.out_dir}/")
        plot_ipc(base, hw, args.out_dir)
        plot_fault_latency(base, hw, args.out_dir)
        plot_translation_breakdown(base, hw, args.out_dir)
        plot_fault_coverage(hw, args.out_dir)
    else:
        print("\nmatplotlib not available -- skipping plots.", file=sys.stderr)


if __name__ == "__main__":
    main()
