#!/usr/bin/env python3
"""plot_pqsweep.py — Figure 20: TRAIL speedup vs same-size ASP across L2-TLB
prefetch-queue (PQ) sizes. Line plot; default matplotlib font.

    plot_pqsweep.py --results-dir DIR --top200 FILE --out fig.pdf
"""
import argparse, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "parse"))
from _common import load, read_list, speedup_vs

PQS = [64, 128, 256, 512, 1024]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results-dir", required=True)
    ap.add_argument("--top200", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    top = set(read_list(a.top200))

    labels, vals = [], []
    asp0 = load(a.results_dir, "asp-pq-nopq"); trail0 = load(a.results_dir, "trail-pq-nopq")
    if asp0 and trail0:
        t0, _ = speedup_vs(trail0, asp0, top); labels.append("cache\nonly"); vals.append(t0)
    for pq in PQS:
        asp = load(a.results_dir, f"asp-pq-pq{pq}"); trail = load(a.results_dir, f"trail-pq-pq{pq}")
        if asp and trail:
            t, _ = speedup_vs(trail, asp, top); labels.append(str(pq)); vals.append(t)
    if not vals:
        print("no PQ-sweep data found", file=sys.stderr); sys.exit(1)

    import matplotlib; matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    plt.rcParams.update({"savefig.dpi": 600, "axes.linewidth": 1.2})
    fig, ax = plt.subplots(figsize=(6.4, 3.2))
    x = range(len(labels))
    ax.plot(x, vals, "-o", color="#1b4332", markersize=6, linewidth=2.0,
            markeredgecolor="black", markerfacecolor="#5fa8a0", zorder=3)
    ax.axhline(0, color="#DC2F2F", ls="--", lw=0.8, alpha=0.8, zorder=2)
    for xi, v in zip(x, vals):
        ax.text(xi, v + 0.06, f"{v:+.2f}%", ha="center", va="bottom", fontsize=8, fontweight="bold", color="#1b4332")
    ax.set_xticks(list(x)); ax.set_xticklabels(labels, fontsize=10)
    ax.set_xlabel("L2-TLB prefetch-queue size (entries)", fontsize=11)
    ax.set_ylabel("TRAIL speedup over\nsame-size ASP (%)", fontsize=11)
    for s_ in ax.spines.values(): s_.set_color("black"); s_.set_linewidth(1.2)
    ax.tick_params(axis="both", direction="out", length=5, width=1.0, top=False, right=False, labelsize=10)
    ax.grid(axis="y", lw=0.5, alpha=0.35, zorder=0); ax.set_axisbelow(True)
    lo = min(0.0, min(vals)); hi = max(vals)
    ax.set_ylim(lo - 0.3, hi + 0.5); ax.margins(x=0.05)
    fig.savefig(a.out, bbox_inches="tight")
    print(f"[wrote {a.out}]  PQ points: {len(vals)}")

if __name__ == "__main__":
    main()
