#!/usr/bin/env python3
"""plot_multicore.py — paper-format multicore figure (equal-work harmonic-mean).

Reproduces the bar chart from prefix_mc_results.ipynb, computed from the 4-core
heartbeat.txt files (equal-work 50M-per-core crossing times). Default font.

    plot_multicore.py --results-dir DIR [DIR ...] --out fig.pdf

DIR is a multicore results directory (walked recursively for
mc-v4-<scheme>_4core_<mix>/heartbeat.txt). Pass one (exp_ae_multicore/results,
which holds both suites) or several.
"""
import argparse, glob, math, os, re, sys
from collections import defaultdict

TGT = 50_000_000
IC_RE = re.compile(r"\[c0=(\d+), c1=(\d+), c2=(\d+), c3=(\d+)\]")
CK_RE = re.compile(r"\[c0=(\d+)ns, c1=(\d+)ns, c2=(\d+)ns, c3=(\d+)ns\]")

def cross_times(path):
    ic, ck = [], []
    for l in open(path):
        if "PERCORE-ICOUNT" in l and "agg=" in l:
            m = IC_RE.search(l);  ic.append([int(x) for x in m.groups()]) if m else None
        elif "PERCORE-CLOCK" in l:
            m = CK_RE.search(l);  ck.append([int(x) for x in m.groups()]) if m else None
    n = min(len(ic), len(ck))
    if n < 2: return None
    out = []
    for c in range(4):
        t = None
        for i in range(1, n):
            if ic[i][c] >= TGT:
                i0,i1 = ic[i-1][c], ic[i][c]; t0,t1 = ck[i-1][c], ck[i][c]
                t = t0 + (t1-t0)*((TGT-i0)/(i1-i0)) if i1 > i0 else t1
                break
        if t is None: return None
        out.append(t)
    return out

LAB = {"nopf":"no-pf","asp":"Baseline with\nTLB Prefetching","asp-dp":"DP","asp-recency":"Recency",
       "asp-atp":"ATP","asp-berti":"Berti","asp-l2tlb32k":"L2 TLB 32K","asp-l2tlb64k":"L2 TLB 64K",
       "trail":"Trail (best)","perfect":"Perfect L2 TLB"}
ORDER = ["Baseline with\nTLB Prefetching","DP","ATP","Recency","Berti",
         "L2 TLB 32K","L2 TLB 64K","Trail (best)","Perfect L2 TLB"]
CC = {"Baseline with\nTLB Prefetching":"#457b9d","DP":"#e07a5f","Recency":"#d4a373",
      "Berti":"#7b2d8e","ATP":"#e76f51","L2 TLB 32K":"#b5a8d0","L2 TLB 64K":"#8e7cb8",
      "Trail (best)":"#1b4332","Perfect L2 TLB":"#e9c46a"}
RUN_RE = re.compile(r"mc-v4-(.+?)_4core_(.+)$")

def gm(x): return math.exp(sum(math.log(v) for v in x)/len(x))
def hm(x): return len(x)/sum(1/v for v in x)

def load(dirs):
    data = defaultdict(dict)   # mix -> scheme -> [per-core times]
    for d in dirs:
        for hb in glob.glob(os.path.join(d, "**", "heartbeat.txt"), recursive=True):
            m = RUN_RE.search(os.path.basename(os.path.dirname(hb)))
            if not m: continue
            scheme, mix = m.group(1), m.group(2)
            ct = cross_times(hb)
            if ct: data[mix][scheme] = ct
    return data

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results-dir", nargs="+", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    data = load(a.results_dir)
    mixes = [m for m in data if "nopf" in data[m]]
    if not mixes:
        print("no complete mixes (need a 'nopf' baseline per mix) found", file=sys.stderr); sys.exit(1)

    results = {}
    for s, lab in LAB.items():
        if s == "nopf": continue
        sp = [data[m]["nopf"][c]/data[m][s][c] for m in mixes if s in data[m] for c in range(4)]
        if sp: results[lab] = (hm(sp), gm(sp))

    labels = [o for o in ORDER if o in results]
    vals = [(results[o][0]-1)*100 for o in labels]          # harmonic-mean speedup %
    colors = [CC.get(o, "#b0b0b0") for o in labels]

    import matplotlib; matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    plt.rcParams.update({"savefig.dpi":600, "axes.linewidth":1.2})
    fig, ax = plt.subplots(figsize=(8.0, 3.1))
    x = range(len(labels))
    bars = ax.bar(x, vals, color=colors, edgecolor="black", linewidth=1.0, width=0.62, zorder=3)
    ax.axhline(0, color="#DC2F2F", linestyle="--", linewidth=0.8, alpha=0.8, zorder=2)
    for bar, v in zip(bars, vals):
        ax.text(bar.get_x()+bar.get_width()/2, v+0.15, f"{v:+.1f}%", ha="center", va="bottom",
                fontsize=7.5, fontweight="bold", color="#c0392b")
    ax.set_xticks(list(x)); ax.set_xticklabels(labels, fontsize=8.5, rotation=30, ha="right")
    ax.set_ylabel("Equal-work Harmonic-mean IPC \nSpeedup over No-TLB Prefetch (%)", fontsize=10)
    ax.text(0.32, 0.86, f"{len(mixes)} mixes from the top-40\nmost translation-intensive workloads",
            transform=ax.transAxes, ha="center", va="center", fontsize=10,
            bbox=dict(boxstyle="round,pad=0.5", facecolor="#fdf6e3", edgecolor="black", linewidth=1.0, alpha=0.95), zorder=5)
    for sp_ in ax.spines.values(): sp_.set_color("black"); sp_.set_linewidth(1.2)
    ax.tick_params(axis="both", direction="out", length=5, width=1.0, top=False, right=False, labelsize=10)
    ax.grid(axis="y", linewidth=0.4, alpha=0.3, zorder=0); ax.set_axisbelow(True); ax.margins(x=0.03)
    ax.set_ylim(0, max(25, max(vals)*1.15 if vals else 25))
    fig.savefig(a.out, bbox_inches="tight")
    print(f"[wrote {a.out}]  mixes={len(mixes)}, configs={len(labels)}")

if __name__ == "__main__":
    main()
