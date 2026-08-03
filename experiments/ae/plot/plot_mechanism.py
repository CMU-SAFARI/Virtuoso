#!/usr/bin/env python3
"""plot_mechanism.py — Figure 13: TRAIL mechanism metrics, from the 8 MB NUCA
single-core sim.stats. Three stacked panels:

  (a) Demand Walks Avoided (%)          = 1 - walks_scheme / walks_no-pf
  (b) Demand Walk Latency Reduction (%) = 1 - walk_lat_scheme / walk_lat_no-pf
  (c) Predicted Cache Lines Used vs Wasted (%)   (L2 page-table-prefetch accuracy)

Aggregated (geomean/mean) over the top-200 non-srv workloads, same selection as
the head-to-head figure. Default matplotlib font.

    plot_mechanism.py --results-dir <8MB results dir> --out fig.pdf
"""
import argparse, glob, math, os, sys
from collections import defaultdict

KEYS = {
    "instr":   "performance_model.instruction_count",
    "cyc":     "performance_model.cycle_count",
    "walks":   "mmu.page_table_walks",
    "walklat": "mmu.total_table_walk_latency",
    "l2_hit_pf":  "L2.hits-prefetch",
    "l2_evict_pf":"L2.evict-prefetch",
}

def readstats(path):
    want = {k: None for k in KEYS}
    inv = {v: k for k, v in KEYS.items()}
    for line in open(path):
        sp = line.find("=")
        if sp < 0: continue
        key = line[:sp].strip()
        if key in inv:
            try: want[inv[key]] = float(line[sp+1:].split(",")[0])
            except ValueError: pass
    return want

# 8 MB NUCA scheme -> label (rundir config prefix, no -nuca2mb suffix)
LAB8 = {"v4-noprefetch":"no-pf","v4-asp":"ASP","v4-asp-stride":"NextPage","v4-asp-dp":"DP",
        "v4-asp-atp":"ATP","v4-asp-berti":"Berti","v4-asp-recency":"Recency",
        "v4-trail":"Trail (best)","v4-trail-sidecar":"Trail (sidecar)","v4-perfect-l2tlb":"Perfect L2 TLB"}
CONFIG_ORDER = ["ASP","NextPage","DP","ATP","Berti","Recency","Trail (sidecar)","Trail (best)","Perfect L2 TLB"]
B = "no-pf"

def load(results_dir):
    schemes = sorted(LAB8, key=len, reverse=True)
    data = defaultdict(dict)
    for p in glob.glob(os.path.join(results_dir, "*", "simulation", "sim.stats")):
        d = os.path.relpath(p, results_dir).split(os.sep)[0]   # rundir basename (robust to 'results' in parent paths)
        for s in schemes:
            if d.startswith(s + "_"):
                data[d[len(s)+1:]][LAB8[s]] = readstats(p); break
    return data

def ipc(s): return s["instr"]/s["cyc"] if s and s.get("instr") and s.get("cyc") else None
def safe(num, den): return num/den if (den and den > 0) else None
def mean(x): return sum(x)/len(x) if x else float("nan")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results-dir", required=True)
    ap.add_argument("--top200", default=None)   # accepted for interface parity; not required
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    data = load(a.results_dir)
    elig = [t for t in data if not t.startswith("srv")
            and all(c in data[t] for c in CONFIG_ORDER + [B])
            and all(ipc(data[t][c]) for c in CONFIG_ORDER + [B])]
    elig.sort(key=lambda t: max(ipc(data[t]["Trail (best)"]), ipc(data[t]["Trail (sidecar)"]))/ipc(data[t][B]),
              reverse=True)
    top = elig[:200]
    if not top:
        print("no eligible workloads (need all schemes present)", file=sys.stderr); sys.exit(1)

    rows = {}
    for c in CONFIG_ORDER:
        av, lat, l2u = [], [], []
        for t in top:
            s, b = data[t][c], data[t][B]
            r = safe(s["walks"], b["walks"]);      av.append((1-r)*100)  if r is not None else None
            r = safe(s["walklat"], b["walklat"]);  lat.append((1-r)*100) if r is not None else None
            h, e = s["l2_hit_pf"], s["l2_evict_pf"]
            if h is not None and e is not None and (h+e) > 0: l2u.append(h/(h+e)*100)
        rows[c] = dict(avoid=mean(av), lat=mean(lat),
                       used=mean(l2u) if l2u else float("nan"),
                       wast=100-mean(l2u) if l2u else float("nan"))

    import matplotlib; matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.patches import Patch
    plt.rcParams.update({"font.size": 9, "axes.edgecolor": "black", "axes.linewidth": 1.2,
                         "axes.grid": True, "axes.grid.axis": "y", "grid.color": "#cccccc",
                         "grid.linewidth": 0.6, "axes.axisbelow": True, "savefig.dpi": 600})
    DISP = {"ASP":"Baseline TLB\nPrefetching","NextPage":"Next\nPage","DP":"DP","ATP":"ATP","Berti":"Berti",
            "Recency":"Recency","Trail (sidecar)":"Trail\nSidecar","Trail (best)":"Trail\nBest"}
    CC = {"ASP":"#457b9d","NextPage":"#83b0c9","DP":"#e07a5f","Recency":"#d4a373","Berti":"#7b2d8e",
          "ATP":"#e76f51","Trail (sidecar)":"#5fa8a0","Trail (best)":"#1b4332"}
    C_USED, C_WASTED = "#55a868", "#d4694e"

    order = [c for c in CONFIG_ORDER if c != "Perfect L2 TLB"]
    x = list(range(len(order)))
    avoided = [rows[c]["avoid"] for c in order]; lat_red = [rows[c]["lat"] for c in order]
    used = [rows[c]["used"] for c in order];     wasted = [rows[c]["wast"] for c in order]
    bar_colors = [CC[c] for c in order]

    fig, (axA, axB, axC) = plt.subplots(3, 1, figsize=(7.6, 5.2), sharex=True, gridspec_kw={"hspace": 0.18})
    def lbl(ax, vals, dy):
        for xi, v in zip(x, vals):
            ax.text(xi, v+dy, f"{v:.1f}", ha="center", va="bottom", fontsize=7, fontweight="bold")

    axA.bar(x, avoided, color=bar_colors, edgecolor="black", linewidth=0.8, width=0.72)
    lbl(axA, avoided, max(avoided)*0.012); axA.set_ylabel("Demand Walks\nAvoided (%)")
    axA.set_ylim(0, max(avoided)*1.16); axA.text(0.011, 0.84, "(a)", transform=axA.transAxes, fontweight="bold", fontsize=11)

    axB.bar(x, lat_red, color=bar_colors, edgecolor="black", linewidth=0.8, width=0.72)
    lbl(axB, lat_red, max(lat_red)*0.012); axB.axhline(0, color="#888", lw=0.7, zorder=1)
    axB.set_ylabel("Demand Walk\nLatency Reduction (%)")
    axB.set_ylim(min(0, min(lat_red))*1.2, max(lat_red)*1.16); axB.text(0.011, 0.84, "(b)", transform=axB.transAxes, fontweight="bold", fontsize=11)

    axC.bar(x, used, color=C_USED, edgecolor="black", linewidth=0.8, width=0.72)
    axC.bar(x, wasted, bottom=used, color=C_WASTED, edgecolor="black", linewidth=0.8, width=0.72)
    for xi, u, w in zip(x, used, wasted):
        if u >= 7: axC.text(xi, u/2, f"{u:.0f}%", ha="center", va="center", fontsize=7, fontweight="bold", color="white")
        if w >= 7: axC.text(xi, u+w/2, f"{w:.0f}%", ha="center", va="center", fontsize=7, fontweight="bold", color="white")
    axC.set_ylabel("Predicted Cache\nLines (%)"); axC.set_ylim(0, 100); axC.set_yticks([0,25,50,75,100])
    axC.text(0.011, 0.84, "(c)", transform=axC.transAxes, fontweight="bold", fontsize=11)
    axC.legend(handles=[Patch(facecolor=C_USED, edgecolor="black", label="Used"),
                        Patch(facecolor=C_WASTED, edgecolor="black", label="Wasted")],
               loc="lower left", frameon=True, framealpha=0.9, fontsize=7, handlelength=1.1)
    axC.set_xticks(x); axC.set_xticklabels([DISP[c] for c in order], fontsize=8); axC.set_xlim(-0.6, len(order)-0.4)

    fig.savefig(a.out, bbox_inches="tight")
    print(f"[wrote {a.out}]  n_top200={len(top)}")

if __name__ == "__main__":
    main()
