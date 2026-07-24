#!/usr/bin/env python3
"""plot_singlecore.py — paper-format single-core figure (head8mb / head2mb).

Reproduces the grouped-bar-by-workload-family + GMEAN-panel figure from
prefix_sc_2mb_8mb_family.ipynb, driven directly by the AE results directories.
Uses the default matplotlib font (no Roboto to install).

    plot_singlecore.py --out fig.pdf [--head8mb <8mb results dir>]
                                     [--head2mb <2mb results dir>]

Give one for a single row, or both for the paper's 2x2 (2 MB over 8 MB).
"""
import argparse, glob, math, os, sys
from collections import defaultdict
import numpy as np

def ipc(p):
    i = c = None
    for l in open(p):
        if l.startswith("performance_model.instruction_count"): i = float(l.split("=")[1].split(",")[0])
        elif l.startswith("performance_model.cycle_count"):      c = float(l.split("=")[1].split(",")[0])
        if i and c: break
    return i / c if (i and c) else None

# rundir config prefix -> label, per experiment
LAB8 = {"v4-noprefetch":"no-pf","v4-asp":"Baseline with TLB Prefetching","v4-asp-stride":"NextPage",
        "v4-asp-dp":"DP","v4-asp-atp":"ATP","v4-asp-berti":"Berti","v4-asp-recency":"Recency",
        "v4-trail":"Trail (best)","v4-trail-sidecar":"Trail (sidecar)","v4-perfect-l2tlb":"Perfect L2 TLB"}
LAB2 = {k+"-nuca2mb": v for k, v in LAB8.items()}

def load(results_dir, labmap):
    schemes = sorted(labmap, key=len, reverse=True)
    data = defaultdict(dict)
    for p in glob.glob(os.path.join(results_dir, "*", "simulation", "sim.stats")):
        d = os.path.relpath(p, results_dir).split(os.sep)[0]   # rundir basename (robust to 'results' in parent paths)
        for s in schemes:
            if d.startswith(s + "_") and d == s + "_" + d[len(s)+1:]:
                v = ipc(p)
                if v: data[d[len(s)+1:]][labmap[s]] = v
                break
    return data

GRAPH=('bc','bfs','cc','dlrm','gc','gen','ligra','pr','sssp','tc')
SPEC=('436.','471.','473.','483.','605.','620.','623.','649.')
GOOGLE=('bravo','delta','sierra','merced','tahoe','tango','whiskey','yankee')
HPC=('rnd','rwkv')
def fam(wl):
    if wl.startswith(('compute_int','compute_fp')): return 'compute\n(CVP-1)'
    if wl.startswith('parsec'): return 'Parsec\nCanneal'
    if wl.startswith('xs'): return 'XSBench'
    if wl.startswith('paoding'): return 'Huawei\n(DPC-4)'
    if wl.startswith(GRAPH): return 'Graph\nAnalytics'
    if wl.startswith(SPEC): return 'SPEC'
    if wl.startswith(GOOGLE): return 'Google\nServer'
    if wl.startswith(HPC): return 'HPC'
    return 'other'
def gm(x): return math.exp(sum(math.log(v) for v in x)/len(x))

CONFIG_ORDER=["Baseline with TLB Prefetching","NextPage","DP","ATP","Berti","Recency","Trail (sidecar)","Trail (best)","Perfect L2 TLB"]
FAM_ORDER=['Google\nServer','compute\n(CVP-1)','Graph\nAnalytics','SPEC','Parsec\nCanneal','Huawei\n(DPC-4)','XSBench']
CC={"Baseline with TLB Prefetching":"#457b9d","NextPage":"#83b0c9","DP":"#e07a5f","Recency":"#d4a373",
    "Berti":"#7b2d8e","ATP":"#e76f51","Trail (sidecar)":"#5fa8a0","Trail (best)":"#1b4332","Perfect L2 TLB":"#e9c46a"}

def analyze(data):
    B = "no-pf"
    elig = [t for t in data if not t.startswith("srv") and all(c in data[t] for c in CONFIG_ORDER+[B])]
    elig.sort(key=lambda t: max(data[t]["Trail (best)"], data[t]["Trail (sidecar)"])/data[t][B], reverse=True)
    top = elig[:200]
    if not top:                       # no workload has every config yet — nothing to plot for this row
        return None
    fams = defaultdict(lambda: defaultdict(list))
    for t in top:
        f = fam(t)
        for c in CONFIG_ORDER: fams[c][f].append(data[t][c]/data[t][B])
    pivot = {c:[gm(fams[c][f]) if fams[c][f] else np.nan for f in FAM_ORDER] for c in CONFIG_ORDER}
    gmean = {c: gm([data[t][c]/data[t][B] for t in top]) for c in CONFIG_ORDER}
    return pivot, gmean, len(top)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--head8mb"); ap.add_argument("--head2mb"); ap.add_argument("--out", required=True)
    a = ap.parse_args()
    rows = []  # (tag, pivot, gmean, n)
    if a.head2mb:
        res = analyze(load(a.head2mb, LAB2))
        if res: rows.append(("2 MB NUCA", *res))
        else: print("  [warn] 2 MB: no workload has all configs yet — skipping row", file=sys.stderr)
    if a.head8mb:
        res = analyze(load(a.head8mb, LAB8))
        if res: rows.append(("8 MB NUCA", *res))
        else: print("  [warn] 8 MB: no workload has all configs yet — skipping row", file=sys.stderr)
    if not rows: print("no plottable data — need --head8mb/--head2mb with all configs present", file=sys.stderr); sys.exit(2)

    import matplotlib; matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    plt.rcParams.update({"savefig.dpi":600,"axes.linewidth":1.2})
    n = len(CONFIG_ORDER); bw = 0.8/n; YT=[0.90,0.95,1.00,1.05,1.10,1.15]

    def draw(ax, piv_or_g, xlabels, show_x, ylab, bg=None):
        if bg: ax.set_facecolor(bg)
        xp = np.arange(len(xlabels))
        for i,c in enumerate(CONFIG_ORDER):
            vals = piv_or_g[c] if isinstance(piv_or_g[c], list) else [piv_or_g[c]]
            ax.bar(xp+i*bw, vals, bw, label=c, color=CC[c], edgecolor="black", linewidth=0.8, zorder=3)
        if list(xlabels)==["GMEAN"]:
            for i,c in enumerate(CONFIG_ORDER):
                v = piv_or_g[c][0] if isinstance(piv_or_g[c], list) else piv_or_g[c]
                ax.text(i*bw, v+0.004, f"{(v-1)*100:+.1f}%", rotation=90, ha="center", va="bottom",
                        fontsize=6.5, fontweight="bold", color="#2c3e50", zorder=6)
        ax.axhline(1.0, color="#DC2F2F", ls="--", lw=0.8, alpha=0.8, zorder=2)
        ax.set_xticks(xp+bw*(n-1)/2); ax.set_yticks(YT); ax.set_ylim(0.88,1.22)
        ax.set_xticklabels(xlabels if show_x else [], fontsize=11)
        if ylab: ax.set_ylabel("Speedup over\nNo-Prefetch", fontsize=12)
        else: ax.tick_params(labelleft=False)
        for s_ in ax.spines.values(): s_.set_color("black"); s_.set_linewidth(1.2)
        ax.tick_params(axis="both", direction="out", length=5, width=1.0, top=False, right=False, labelsize=11)
        ax.grid(axis="both", lw=0.7, alpha=0.5, zorder=0); ax.set_axisbelow(True)

    nr = len(rows)
    fig, axes = plt.subplots(nr, 2, figsize=(12.0, 2.5*nr+0.5), squeeze=False,
        gridspec_kw={"width_ratios":[len(FAM_ORDER),1], "hspace":0.10, "wspace":0.04})
    for r,(tag,piv,gme,_) in enumerate(rows):
        last = (r==nr-1)
        draw(axes[r,0], piv, FAM_ORDER, last, True)
        draw(axes[r,1], gme, ["GMEAN"], last, False, bg="#ebebeb")
        axes[r,0].text(0.012,0.93,tag,transform=axes[r,0].transAxes,fontsize=11,fontweight="bold",
                       va="top",ha="left",bbox=dict(boxstyle="round,pad=0.3",facecolor="white",
                       edgecolor="black",linewidth=0.9,alpha=0.95))
    h,l = axes[0,0].get_legend_handles_labels()
    fig.legend(h,l,fontsize=10,ncol=9,loc="upper center",bbox_to_anchor=(0.5,1.0),
               framealpha=0.9,edgecolor="gray",borderpad=0.4,handlelength=1.2,columnspacing=0.8)
    fig.savefig(a.out, bbox_inches="tight")
    print(f"[wrote {a.out}]  " + ", ".join(f"{t}: n={nn}" for t,_,_,nn in rows))

if __name__ == "__main__":
    main()
