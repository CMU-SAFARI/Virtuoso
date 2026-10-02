#!/usr/bin/env python3
"""paper_figures.py — the paper's evaluation figures and tables from AE results.

  paper_figures.py figure11 --head2mb DIR --head8mb DIR --out F.pdf [--md F.md]
  paper_figures.py figure13 --head2mb DIR --out F.pdf [--md F.md]
  paper_figures.py figure16 --multicore DIR --out F.pdf [--md F.md]
  paper_figures.py figure18 --head2mb DIR --mtps 400=DIR 800=DIR ... --top200 FILE --out F.pdf [--md F.md]
  paper_figures.py figure19 --abl DIR --head2mb DIR --top200 FILE --out F.pdf [--md F.md]
  paper_figures.py table4   --results DIR --head8mb DIR --out F.pdf [--md F.md]
  paper_figures.py table5   --results DIR --head8mb DIR --out F.pdf [--md F.md]

DIR is a suite's results directory (experiments/exp_ae_<suite>/results).
Single-core speedups are IPC ratios over No-TLB-Prefetcher, aggregated by
geometric mean; the multicore metric is the equal-work harmonic mean of
per-core speedups.  A PNG is written next to every PDF.
"""
import argparse, glob, math, os, re, sys, warnings
from collections import defaultdict

import numpy as np
import matplotlib
matplotlib.use("Agg")
warnings.filterwarnings("ignore", message=".*tight_layout.*")
import matplotlib.pyplot as plt
import matplotlib.font_manager as fm
import matplotlib.ticker as mticker
from matplotlib.lines import Line2D
from matplotlib.patches import Patch

# ─────────────────────────────── style ───────────────────────────────
for _f in ("/usr/share/fonts/truetype/roboto/unhinted/RobotoTTF/Roboto-Regular.ttf",
           "/usr/share/fonts/truetype/roboto/hinted/Roboto-Regular.ttf"):
    if os.path.exists(_f):
        fm.fontManager.addfont(_f); plt.rcParams["font.family"] = fm.FontProperties(fname=_f).get_name(); break
plt.rcParams.update({"figure.dpi": 150, "savefig.dpi": 600, "axes.linewidth": 1.2,
                     "grid.linewidth": 0.4, "grid.alpha": 0.3, "pdf.fonttype": 42})

CC = {"IP-Stride": "#457b9d", "Next-Page": "#83b0c9", "DP": "#e07a5f", "Recency": "#d4a373",
      "Berti": "#7b2d8e", "ATP": "#e76f51", "Trail-External": "#5fa8a0", "Trail-Best": "#1b4332",
      "Perfect L2 TLB": "#e9c46a", "L2 TLB 64K": "#8e7cb8"}
CONFIG_ORDER = ["IP-Stride", "Next-Page", "DP", "ATP", "Berti", "Recency", "Trail-External", "Trail-Best", "Perfect L2 TLB"]
NP_ORDER = ["Recency+Next-Page", "Trail-External+Next-Page", "Trail-Best+Next-Page"]
NP_BASE = {"Recency+Next-Page": "Recency", "Trail-External+Next-Page": "Trail-External", "Trail-Best+Next-Page": "Trail-Best"}
SCHEME = {"no-pf": "v4-noprefetch", "IP-Stride": "v4-asp", "Next-Page": "v4-asp-stride", "DP": "v4-asp-dp",
          "ATP": "v4-asp-atp", "Berti": "v4-asp-berti", "Recency": "v4-asp-recency", "Trail-Best": "v4-trail",
          "Trail-External": "v4-trail-sidecar", "Perfect L2 TLB": "v4-perfect-l2tlb",
          "Recency+Next-Page": "v4-asp-recency-stride", "Trail-External+Next-Page": "v4-trail-sidecar-stride",
          "Trail-Best+Next-Page": "v4-trail-stride"}

GRAPH = ('bc', 'bfs', 'cc', 'dlrm', 'gc', 'gen', 'ligra', 'pr', 'sssp', 'tc')
SPEC = ('436.', '471.', '473.', '483.', '605.', '620.', '623.', '649.')
GOOGLE = ('bravo', 'delta', 'sierra', 'merced', 'tahoe', 'tango', 'whiskey', 'yankee')
FAM_ORDER = ['Google\nServer', 'CVP-1\n Qualcomm', 'Graph\nAnalytics', 'SPEC', 'Parsec\nCanneal', 'Huawei\nDPC-4', 'XSBench']


def fam(wl):
    if wl.startswith(('compute_int', 'compute_fp')): return 'CVP-1\n Qualcomm'
    if wl.startswith('parsec'): return 'Parsec\nCanneal'
    if wl.startswith('xs'): return 'XSBench'
    if wl.startswith('paoding'): return 'Huawei\nDPC-4'
    if wl.startswith(GRAPH): return 'Graph\nAnalytics'
    if wl.startswith(SPEC): return 'SPEC'
    if wl.startswith(GOOGLE): return 'Google\nServer'
    return 'other'


def gm(x): return math.exp(sum(math.log(v) for v in x) / len(x))
def hm(x): return len(x) / sum(1 / v for v in x)
def mean(x): return sum(x) / len(x) if x else float("nan")
def pct(v): return f"{(v - 1) * 100:+.2f}%"


def spines(ax):
    for s in ax.spines.values(): s.set_visible(True); s.set_color("black"); s.set_linewidth(1.2)
    ax.tick_params(axis="both", direction="out", length=5, width=1.0, top=False, right=False)


def save(fig, out):
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    fig.savefig(out, bbox_inches="tight")
    fig.savefig(os.path.splitext(out)[0] + ".png", bbox_inches="tight")
    plt.close(fig)
    print(f"  figure: {out}")


def write_md(path, text):
    print(text)
    if path:
        with open(path, "w") as f: f.write(text + "\n")


# ─────────────────────────────── stats ───────────────────────────────
STAT_KEYS = {"instr": "performance_model.instruction_count", "cyc": "performance_model.cycle_count",
             "walks": "mmu.page_table_walks", "pfaults": "mmu.page_faults", "walklat": "mmu.total_table_walk_latency",
             "l2_hit_pf": "L2.mmu-hits-prefetch", "l2_evict_pf": "L2.mmu-evict-prefetch",
             "tlat": "memory_manager.translation_latency", "macc": "memory_manager.memory_accesses"}
_KEYINV = {v: k for k, v in STAT_KEYS.items()}
_cache = {}


def stats(rundir):
    """Selected counters of one run (None if the run has no valid sim.stats)."""
    if rundir in _cache: return _cache[rundir]
    w = {}
    try:
        for line in open(os.path.join(rundir, "simulation", "sim.stats")):
            k, _, v = line.partition("=")
            k = k.strip()
            if k in _KEYINV:
                try: w[_KEYINV[k]] = float(v.split(",")[0])
                except ValueError: pass
    except OSError:
        w = None
    if w is not None and not (w.get("instr") and w.get("cyc")): w = None
    _cache[rundir] = w
    return w


def ipc(rundir):
    s = stats(rundir)
    return s["instr"] / s["cyc"] if s else None


def workloads(results, scheme):
    """Workload names that have a run directory <scheme>_<workload> under results."""
    out = set()
    for d in glob.glob(os.path.join(results, scheme + "_*")):
        out.add(os.path.basename(d)[len(scheme) + 1:])
    return out


def load_ipc(results, labels, sfx):
    """{workload: {label: ipc}} for the given labels (SCHEME names + suffix)."""
    data = defaultdict(dict)
    for lab in labels:
        sch = SCHEME[lab] + sfx
        for t in workloads(results, sch):
            v = ipc(os.path.join(results, f"{sch}_{t}"))
            if v: data[t][lab] = v
    return data


def head_top(data, n):
    """Top-n non-srv workloads by Trail speedup (max of Trail-Best and Trail-External),
    among workloads that completed for every standalone configuration."""
    elig = [t for t in data if not t.startswith("srv") and all(c in data[t] for c in CONFIG_ORDER + ["no-pf"])]
    elig.sort(key=lambda t: max(data[t]["Trail-Best"], data[t]["Trail-External"]) / data[t]["no-pf"], reverse=True)
    return elig[:n]


# ───────────────────────────── Figure 11 ─────────────────────────────
def analyze_head(results, sfx):
    data = load_ipc(results, ["no-pf"] + CONFIG_ORDER + NP_ORDER, sfx)
    top = head_top(data, 200)
    pivot = {c: [] for c in CONFIG_ORDER}
    for c in CONFIG_ORDER:
        for f in FAM_ORDER:
            v = [data[t][c] / data[t]["no-pf"] for t in top if fam(t) == f]
            pivot[c].append(gm(v) if v else np.nan)
    g = {c: gm([data[t][c] / data[t]["no-pf"] for t in top]) for c in CONFIG_ORDER}
    topn = [t for t in top if all(c in data[t] for c in NP_ORDER)]
    gn = {c: gm([data[t][c] / data[t]["no-pf"] for t in topn]) for c in NP_ORDER} if topn else {}
    return pivot, g, gn, len(top), len(topn)


def figure11(a):
    rows = []
    for lab, res, sfx in (("2 MB", a.head2mb, "-nuca2mb"), ("8 MB", a.head8mb, "")):
        if res and os.path.isdir(res): rows.append((lab,) + analyze_head(res, sfx))
    if not rows: sys.exit("figure11: no results")
    md = ["# Figure 11: speedup over No-TLB-Prefetcher (geomean, top-200 workloads)", ""]
    md.append("| configuration | " + " | ".join(f"{r[0]} LLC" for r in rows) + " |")
    md.append("|---|" + "---:|" * len(rows))
    for c in CONFIG_ORDER: md.append(f"| {c} | " + " | ".join(pct(r[2][c]) for r in rows) + " |")
    for c in NP_ORDER: md.append(f"| {c} | " + " | ".join(pct(r[3][c]) if c in r[3] else "-" for r in rows) + " |")
    md.append(""); md.append("workloads: " + ", ".join(f"{r[0]}: {r[4]} (+Next-Page: {r[5]})" for r in rows))
    write_md(a.md, "\n".join(md))

    n = len(CONFIG_ORDER); bw = 0.8 / n; YT = [0.90, 0.95, 1.00, 1.05, 1.10, 1.15, 1.20, 1.25]

    def style(ax, show_x, xlabels, ylab):
        ax.axhline(1.0, color="#DC2F2F", ls="--", lw=0.8, alpha=0.8, zorder=2)
        ax.set_yticks(YT); ax.set_ylim(0.88, 1.26)
        if ylab: ax.set_ylabel("Speedup over\nNo-TLB-Prefetcher", fontsize=12)
        else: ax.tick_params(labelleft=False)
        spines(ax); ax.tick_params(labelsize=11)
        ax.grid(axis="both", lw=0.7, alpha=0.5, zorder=0); ax.set_axisbelow(True)

    def draw(ax, vals, xlabels, ylab, show_x, bg=None):
        if bg: ax.set_facecolor(bg)
        xp = np.arange(len(xlabels))
        for i, c in enumerate(CONFIG_ORDER):
            v = vals[c] if isinstance(vals[c], list) else [vals[c]]
            ax.bar(xp + i * bw, v, bw, label=c, color=CC[c], edgecolor="black", linewidth=0.8, zorder=3)
            if xlabels == ["GMEAN"]:
                ax.text(i * bw, v[0] + 0.004, f"{(v[0] - 1) * 100:+.1f}%", rotation=90, ha="center", va="bottom",
                        fontsize=6.5, fontweight="bold", color="#2c3e50", zorder=6)
        ax.set_xticks(xp + bw * (n - 1) / 2)
        ax.set_xticklabels(xlabels if show_x else [], fontsize=11)
        style(ax, show_x, xlabels, ylab)

    def draw_np(ax, g, show_x):
        ax.set_facecolor("#dfe7ef")
        for i, c in enumerate(NP_ORDER):
            if c not in g: continue
            ax.bar(i * bw, g[c], bw, color=CC[NP_BASE[c]], edgecolor="black", linewidth=0.8, hatch="///", zorder=3)
            ax.text(i * bw, g[c] + 0.004, f"{(g[c] - 1) * 100:+.1f}%", rotation=90, ha="center", va="bottom",
                    fontsize=6.5, fontweight="bold", color="#2c3e50", zorder=6)
        ax.set_xticks([bw * (len(NP_ORDER) - 1) / 2]); ax.set_xlim(-bw * 0.9, bw * (len(NP_ORDER) - 1) + bw * 0.9)
        ax.set_xticklabels(["GMEAN\n+Next-Page"] if show_x else [], fontsize=11)
        style(ax, show_x, None, False)

    w3 = (len(NP_ORDER) + 1.8) * bw
    fig, axes = plt.subplots(len(rows), 3, figsize=(12.8, 1.85 * len(rows)), squeeze=False,
                             gridspec_kw={"width_ratios": [len(FAM_ORDER), 1, w3], "hspace": 0.10, "wspace": 0.04})
    for k, (lab, piv, g, gn, _, _) in enumerate(rows):
        last = k == len(rows) - 1
        draw(axes[k, 0], piv, FAM_ORDER, True, last)
        draw(axes[k, 1], g, ["GMEAN"], False, last, bg="#ebebeb")
        draw_np(axes[k, 2], gn, last)
        axes[k, 0].text(0.012, 0.93, f"{lab} LLC per core", transform=axes[k, 0].transAxes, fontsize=11,
                        fontweight="bold", va="top", ha="left",
                        bbox=dict(boxstyle="round,pad=0.3", facecolor="white", edgecolor="black", linewidth=0.9, alpha=0.95))
    h, l = axes[0, 0].get_legend_handles_labels()
    for c in NP_ORDER:
        h.append(Patch(facecolor=CC[NP_BASE[c]], edgecolor="black", hatch="///")); l.append(c)
    fig.legend(h, l, fontsize=9.5, ncol=6, loc="upper center", bbox_to_anchor=(0.5, 1.03),
               framealpha=0.9, edgecolor="gray", borderpad=0.4, handlelength=1.4, columnspacing=0.8)
    plt.tight_layout(rect=[0, 0, 1, 0.90 if len(rows) == 2 else 0.82])
    save(fig, a.out)


# ───────────────────────────── Figure 13 ─────────────────────────────
CORE_GHZ = 5.1          # perf_model/core/frequency


def figure13(a, n=50):
    labels = ["no-pf"] + [c for c in CONFIG_ORDER if c != "Perfect L2 TLB"]
    data = defaultdict(dict)
    for lab in ["no-pf"] + CONFIG_ORDER:
        sch = SCHEME[lab] + "-nuca2mb"
        for t in workloads(a.head2mb, sch):
            s = stats(os.path.join(a.head2mb, f"{sch}_{t}"))
            if s: data[t][lab] = s
    ip = {t: {c: s["instr"] / s["cyc"] for c, s in d.items()} for t, d in data.items()}
    top = head_top(ip, n)
    nw = lambda s: s["walks"] - s.get("pfaults", 0)     # completed demand walks (a faulting walk is counted twice)
    A, used = {}, {}
    for c in labels:
        wpki, lpki, lpw, u = [], [], [], []
        for t in top:
            s = data[t][c]; ki = s["instr"] / 1000.0
            cyc = s.get("walklat", 0) * 1e-6 * CORE_GHZ    # fs -> cycles
            wpki.append(nw(s) / ki); lpki.append(cyc / ki)
            if nw(s) > 0: lpw.append(cyc / nw(s))
            h, e = s.get("l2_hit_pf"), s.get("l2_evict_pf")
            if c != "no-pf" and h is not None and e is not None and h + e > 0: u.append(h / (h + e) * 100)
        A[c] = (mean(wpki), mean(lpki), mean(lpw)); used[c] = mean(u)
    disp = {"no-pf": "No-TLB-Prefetcher"}
    md = [f"# Figure 13: demand page table walks, 2 MB LLC, top-{len(top)} workloads", "",
          "| configuration | demand PTWs / KI | PTW latency / KI (cycles) | latency / PTW (cycles) | prefetched PTE blocks used |",
          "|---|---:|---:|---:|---:|"]
    for c in labels:
        md.append(f"| {disp.get(c, c)} | {A[c][0]:.1f} | {A[c][1]:.0f} | {A[c][2]:.1f} | "
                  + ("-" if c == "no-pf" else f"{used[c]:.0f}%") + " |")
    write_md(a.md, "\n".join(md))

    order = labels[1:]; x = np.arange(len(labels)); cols = ["#d9d9d9"] + [CC[c] for c in order]
    fig, axes = plt.subplots(4, 1, figsize=(7.6, 7.0), sharex=True, gridspec_kw={"hspace": 0.18})
    for ax, (j, ylab, dec) in zip(axes[:3], [(0, "Demand PTWs per\n Kilo Instructions", 1),
                                              (1, "Total PTW Latency per\n Kilo Instructions (cycles)", 0),
                                              (2, "Latency per\nPTW (cycles)", 1)]):
        vals = np.array([A[c][j] for c in labels])
        bars = ax.bar(x, vals, color=cols, edgecolor="black", linewidth=0.8, width=0.72, zorder=3)
        bars[0].set_hatch("///")
        ax.axhline(vals[0], color="black", ls="--", lw=0.8, zorder=2)
        for xi, v in zip(x, vals):
            ax.text(xi, v + vals.max() * 0.035, f"{v:.{dec}f}", ha="center", va="bottom", fontsize=7, fontweight="bold",
                    zorder=6, bbox=dict(boxstyle="round,pad=0.1", facecolor="white", edgecolor="none", alpha=0.85))
        for xi, c in zip(x[1:], order):
            ax.text(xi, vals[xi] * 0.5, f"{(vals[xi] / vals[0] - 1) * 100:+.0f}%", ha="center", va="center",
                    rotation=90, fontsize=6.5, fontweight="bold", color="black" if c == "Next-Page" else "white")
        ax.set_ylabel(ylab, fontsize=9); ax.set_ylim(0, vals.max() * 1.30)
    axC = axes[3]; C_USED, C_WASTED = "#55a868", "#d4694e"
    u = np.array([used[c] for c in order]); w = 100 - u
    axC.bar(x[1:], u, color=C_USED, edgecolor="black", linewidth=0.8, width=0.72, zorder=3)
    axC.bar(x[1:], w, bottom=u, color=C_WASTED, edgecolor="black", linewidth=0.8, width=0.72, zorder=3)
    for xi, uu, ww in zip(x[1:], u, w):
        if uu >= 7: axC.text(xi, uu / 2, f"{uu:.0f}%", ha="center", va="center", fontsize=7, fontweight="bold", color="white")
        if ww >= 7: axC.text(xi, uu + ww / 2, f"{ww:.0f}%", ha="center", va="center", fontsize=7, fontweight="bold", color="white")
    axC.text(0, 50, "N/A", ha="center", va="center", fontsize=7, color="#555")
    axC.set_ylabel("Prefetched\nPTE Blocks (%)", fontsize=9); axC.set_ylim(0, 100); axC.set_yticks([0, 25, 50, 75, 100])
    axC.legend(handles=[Patch(facecolor=C_USED, edgecolor="black", label="Used"),
                        Patch(facecolor=C_WASTED, edgecolor="black", label="Evicted before use")],
               ncol=2, loc="center left", bbox_to_anchor=(0.62, 0.1), frameon=True, framealpha=0.9, fontsize=7, handlelength=1.1)
    for ax, tag in zip(axes, ["(a)", "(b)", "(c)", "(d)"]):
        ax.text(0.011, 0.84, tag, ha="left", transform=ax.transAxes, fontweight="bold", fontsize=11, zorder=10,
                bbox=dict(boxstyle="round,pad=0.15", facecolor="white", edgecolor="none", alpha=0.85))
        spines(ax); ax.tick_params(length=4, labelsize=9)
        ax.grid(axis="y", lw=0.6, color="#cccccc", zorder=0); ax.grid(axis="x", visible=False); ax.set_axisbelow(True)
    names = {"Next-Page": "Next\nPage", "Trail-External": "Trail\nExternal", "Trail-Best": "Trail\nBest"}
    axC.set_xticks(x); axC.set_xticklabels(["No-TLB\nPrefetcher"] + [names.get(c, c) for c in order], fontsize=8)
    axC.set_xlim(-0.6, len(labels) - 0.4)
    save(fig, a.out)


# ───────────────────────────── Figure 16 ─────────────────────────────
MC_TGT = 50_000_000
MC_IC_RE = re.compile(r"\[c0=(\d+), c1=(\d+), c2=(\d+), c3=(\d+)\]")
MC_CK_RE = re.compile(r"\[c0=(\d+)ns, c1=(\d+)ns, c2=(\d+)ns, c3=(\d+)ns\]")
MC_GROUPS = [[("asp", "IP-Stride"), ("asp-stride", "Next-Page"), ("asp-dp", "DP"), ("asp-atp", "ATP"),
              ("asp-recency", "Recency"), ("asp-berti", "Berti"), ("asp-l2tlb64k", "L2 TLB 64K"), ("trail", "Trail-Best")],
             [("asp-recency-stride", "Recency+\nNext-Page"), ("trail-stride", "Trail-Best+\nNext-Page")],
             [("perfect", "Perfect L2 TLB")]]
MC_COLOR = {"Recency+\nNext-Page": CC["Recency"], "Trail-Best+\nNext-Page": CC["Trail-Best"]}


def cross_times(path):
    """Per-core time (ns) at which each core crossed MC_TGT instructions, from heartbeat.txt."""
    ic, ck = [], []
    for l in open(path):
        if "PERCORE-ICOUNT" in l and "agg=" in l:
            m = MC_IC_RE.search(l)
            if m: ic.append([int(x) for x in m.groups()])
        elif "PERCORE-CLOCK" in l:
            m = MC_CK_RE.search(l)
            if m: ck.append([int(x) for x in m.groups()])
    n = min(len(ic), len(ck)); out = []
    for c in range(4):
        t = None
        for i in range(1, n):
            if ic[i][c] >= MC_TGT:
                i0, i1, t0, t1 = ic[i - 1][c], ic[i][c], ck[i - 1][c], ck[i][c]
                t = t0 + (t1 - t0) * ((MC_TGT - i0) / (i1 - i0)) if i1 > i0 else t1
                break
        if t is None: return None
        out.append(t)
    return out


def figure16(a):
    schemes = ["nopf"] + [s for g in MC_GROUPS for s, _ in g]
    data = defaultdict(dict)
    for hb in glob.glob(os.path.join(a.multicore, "*", "mc-v4-*_4core_*", "heartbeat.txt")):
        d = os.path.basename(os.path.dirname(hb))
        m = re.match(r"mc-v4-(.+?)_4core_(.+)$", d)
        if not m or m.group(1) not in schemes: continue
        ct = cross_times(hb)
        if ct: data[m.group(2)][m.group(1)] = ct
    mixes = [m for m in data if "nopf" in data[m]]
    res = {}
    for g in MC_GROUPS:
        for s, lab in g:
            sp = [data[m]["nopf"][c] / data[m][s][c] for m in mixes if s in data[m] for c in range(4)]
            if sp: res[lab] = ((hm(sp) - 1) * 100, sum(1 for m in mixes if s in data[m]))
    md = [f"# Figure 16: four-core equal-work harmonic-mean speedup over No-TLB-Prefetcher ({len(mixes)} mixes)", "",
          "| configuration | speedup | mixes |", "|---|---:|---:|"]
    for g in MC_GROUPS:
        for _, lab in g:
            if lab in res: md.append(f"| {lab.replace('+' + chr(10), '+').replace(chr(10), ' ')} | {res[lab][0]:+.2f}% | {res[lab][1]} |")
    write_md(a.md, "\n".join(md))

    labels = [lab for g in MC_GROUPS for _, lab in g if lab in res]
    vals = [res[l][0] for l in labels]
    colors = [MC_COLOR.get(l, CC.get(l, "#b0b0b0")) for l in labels]
    fig, (axt, ax) = plt.subplots(2, 1, figsize=(8.6, 2.1), sharex=True, gridspec_kw={"height_ratios": [1, 3.2], "hspace": 0.06})
    x = range(len(labels))
    for p in (axt, ax):
        for xi, v, c, l in zip(x, vals, colors, labels):
            p.bar(xi, v, color=c, edgecolor="black", linewidth=1.0, width=0.62, zorder=3,
                  hatch="///" if "+\nNext-Page" in l else None)
        spines(p); p.tick_params(labelsize=10)
        p.grid(axis="y", linewidth=0.4, alpha=0.3, zorder=0); p.grid(axis="x", visible=False); p.set_axisbelow(True)
        p.margins(x=0.03)
    ylo_top = max(13, int(max(v for l, v in zip(labels, vals) if l != "Perfect L2 TLB")) + 3)
    ax.set_ylim(0, ylo_top); ax.set_yticks(list(range(0, ylo_top, 2)))
    ptop = max(vals)
    axt.set_ylim(max(ylo_top, 5 * math.floor(ptop / 5)), 5 * math.ceil(ptop / 5 + 0.2))
    ax.axhline(0, color="#DC2F2F", linestyle="--", linewidth=0.8, alpha=0.8, zorder=2)
    pos = 0
    for g in MC_GROUPS[:-1]:
        pos += sum(1 for _, l in g if l in res)
        for p in (axt, ax): p.axvline(pos - 0.5, color="black", ls=":", lw=1.0, zorder=1)
    axt.spines["bottom"].set_visible(False); ax.spines["top"].set_visible(False)
    axt.tick_params(axis="x", which="both", bottom=False, labelbottom=False)
    d = 0.012; kw = dict(color="black", clip_on=False, linewidth=1.2)
    axt.plot((-d, +d), (-d * 3.2, +d * 3.2), transform=axt.transAxes, **kw)
    axt.plot((1 - d, 1 + d), (-d * 3.2, +d * 3.2), transform=axt.transAxes, **kw)
    ax.plot((-d, +d), (1 - d, 1 + d), transform=ax.transAxes, **kw)
    ax.plot((1 - d, 1 + d), (1 - d, 1 + d), transform=ax.transAxes, **kw)
    for xi, v in zip(x, vals):
        p = axt if v > ylo_top else ax
        p.text(xi, v + (0.15 if p is ax else 0.25), f"{v:+.1f}%", ha="center", va="bottom", fontsize=7.5,
               fontweight="bold", color="#c0392b")
    ax.set_xticks(list(x)); ax.set_xticklabels(labels, fontsize=8.5, rotation=30, ha="right")
    ax.set_ylabel("Equal-work Harmonic-mean IPC \nSpeedup over No-TLB-Prefetcher (%)", fontsize=8.5)
    ax.yaxis.set_label_coords(-0.065, 0.62)
    plt.tight_layout()
    save(fig, a.out)


# ───────────────────────────── Figure 18 ─────────────────────────────
def figure18(a):
    top200 = [l.strip() for l in open(a.top200) if l.strip() and not l.startswith("#")]
    points = sorted([(int(k), v) for k, v in (p.split("=", 1) for p in a.mtps)] + [(2400, a.head2mb)])
    rng = np.random.default_rng(0)

    def ci(v, stat, nboot=2000):         # 95% bootstrap CI over workloads
        v = np.asarray(v); idx = rng.integers(0, len(v), (nboot, len(v)))
        return tuple(np.percentile(stat(v[idx]), [2.5, 97.5]))
    gstat = lambda m: np.exp(np.log(m).mean(axis=1)); astat = lambda m: m.mean(axis=1)
    tl = lambda s: s["tlat"] / s["macc"]
    rows = []
    for mtps, res in points:
        if not res or not os.path.isdir(res): continue
        S = {t: tuple(stats(os.path.join(res, f"{c}-nuca2mb_{t}")) for c in ("v4-noprefetch", "v4-trail", "v4-asp-recency"))
             for t in top200}
        ok = [t for t, v in S.items() if all(v)]
        if not ok: continue
        ip = lambda s: s["instr"] / s["cyc"]
        top = sorted(ok, key=lambda t: ip(S[t][1]) / ip(S[t][0]), reverse=True)[:200]
        row = {"mtps": mtps, "n": len(top)}
        for k, name in ((1, "Trail-Best"), (2, "Recency")):
            sp = [ip(S[t][k]) / ip(S[t][0]) for t in top]
            tr = [(1 - tl(S[t][k]) / tl(S[t][0])) * 100 for t in top]
            row[name] = (gm(sp),) + ci(sp, gstat); row[name + " tlat"] = (mean(tr),) + ci(tr, astat)
        rows.append(row)
    if not rows: sys.exit("figure18: no results")
    md = ["# Figure 18: DRAM bandwidth sensitivity, 2 MB LLC, top-200 workloads", "",
          "| MT/s | workloads | Recency speedup | Trail-Best speedup | Recency transl. latency reduction | Trail-Best transl. latency reduction |",
          "|---:|---:|---:|---:|---:|---:|"]
    for r in rows:
        md.append(f"| {r['mtps']} | {r['n']} | {pct(r['Recency'][0])} | {pct(r['Trail-Best'][0])} | "
                  f"{r['Recency tlat'][0]:.1f}% | {r['Trail-Best tlat'][0]:.1f}% |")
    write_md(a.md, "\n".join(md))

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(6.0, 2.0)); x = list(range(len(rows)))
    for name, mk in (("Recency", "^"), ("Trail-Best", "o")):
        col = CC[name]
        for ax, key, scale in ((ax1, name, 1), (ax2, name + " tlat", 1)):
            v = np.array([r[key] for r in rows])
            ax.fill_between(x, v[:, 1] * scale, v[:, 2] * scale, color=col, alpha=0.18, linewidth=0, zorder=1)
            ax.plot(x, v[:, 0] * scale, marker=mk, linewidth=2, markersize=5, color=col, zorder=3)
    ax1.axhline(1.0, color="#DC2F2F", linestyle="--", linewidth=0.8, alpha=0.8, zorder=2)
    ax1.set_ylabel("Speedup over\nNo-TLB-Prefetcher", fontsize=9, labelpad=6)
    ax2.set_ylabel("Total Translation\nLatency Reduction (%)", fontsize=9, labelpad=6)
    lo = min(min(r["Recency"][1], r["Trail-Best"][1]) for r in rows); hi = max(r["Trail-Best"][2] for r in rows)
    ax1.set_ylim(math.floor(min(lo, 1.0) * 100) / 100, math.ceil(hi * 100) / 100 + 0.01)
    ax1.yaxis.set_major_formatter(mticker.FormatStrFormatter("%.2f"))
    rlo = min(min(r["Recency tlat"][1], r["Trail-Best tlat"][1]) for r in rows)
    rhi = max(max(r["Recency tlat"][2], r["Trail-Best tlat"][2]) for r in rows)
    ax2.set_ylim(math.floor(rlo / 2) * 2, math.ceil(rhi / 2) * 2); ax2.yaxis.set_major_locator(mticker.MultipleLocator(2))
    ax2.yaxis.set_major_formatter(mticker.FormatStrFormatter("%.0f"))
    for ax in (ax1, ax2):
        ax.set_xlabel("DRAM Bandwidth (MTPS)", fontsize=10)
        ax.set_xticks(x); ax.set_xticklabels([str(r["mtps"]) for r in rows], fontsize=9)
        spines(ax); ax.tick_params(labelsize=10)
        ax.grid(axis="y", linewidth=0.4, alpha=0.3, zorder=0); ax.grid(axis="x", visible=False); ax.set_axisbelow(True)
        ax.margins(x=0.04)
    fig.legend(handles=[Line2D([0], [0], color=CC["Recency"], marker="^", lw=2, ms=5, label="Recency"),
                        Line2D([0], [0], color=CC["Trail-Best"], marker="o", lw=2, ms=5, label="Trail-Best")],
               loc="upper center", bbox_to_anchor=(0.38, 0.58), ncol=1, frameon=True, framealpha=0.9,
               edgecolor="gray", fontsize=9, handlelength=2.0)
    fig.tight_layout(w_pad=1.5)
    save(fig, a.out)


# ───────────────────────────── Figure 19 ─────────────────────────────
ABL = [("Global Deltas\n(no PC table)", "abl", "v4-trail-abl-global"),
       ("+PC Table\n(64-entry)", "abl", "v4-trail-abl-pc64"),
       ("+Virtualized \nPC Table", "abl", "v4-trail-abl-pc64vt"),
       ("+Stride Detection\n(full Trail)", "head", "v4-trail"),
       ("Trail-Best\n+Next-Page", "head", "v4-trail-stride"),
       ("Recency\n+Next-Page", "head", "v4-asp-recency-stride")]
ABL_COLORS = ["#c7e0d4", "#8fc1aa", "#4f8f73", CC["Trail-Best"], CC["Trail-Best"], CC["Recency"]]
ABL_HATCH = [None, None, None, None, "///", "///"]


def figure19(a, n=50, sfx="-nuca2mb"):
    top200 = [l.strip() for l in open(a.top200) if l.strip() and not l.startswith("#")]
    root = {"abl": a.abl, "head": a.head2mb}
    run = lambda kind, c, t: ipc(os.path.join(root[kind], f"{c}{sfx}_{t}"))
    base = lambda t: ipc(os.path.join(a.head2mb, f"v4-noprefetch{sfx}_{t}"))
    elig = [t for t in top200 if base(t) and all(run(k, c, t) for _, k, c in ABL[:4])]
    top = sorted(elig, key=lambda t: run("head", "v4-trail", t) / base(t), reverse=True)[:n]
    sp = []
    for _, k, c in ABL:
        r = [run(k, c, t) / base(t) for t in top if run(k, c, t)]
        sp.append(gm(r) if r else float("nan"))
    md = [f"# Figure 19: TRAIL components, 2 MB LLC, top-{len(top)} workloads by full-TRAIL speedup", "",
          "| design | speedup |", "|---|---:|"]
    md += [f"| {l.replace(chr(10), ' ').replace('  ', ' ')} | {pct(v)} |" for (l, _, _), v in zip(ABL, sp)]
    write_md(a.md, "\n".join(md))

    fig, ax = plt.subplots(figsize=(8.6, 1.5))
    bars = ax.bar(range(len(sp)), sp, color=ABL_COLORS, edgecolor="black", linewidth=1.5, width=0.55, zorder=3, hatch=ABL_HATCH)
    ax.axvline(3.5, color="black", ls=":", lw=1.0, zorder=1)
    ax.axhline(1.0, color="#DC2F2F", ls="--", lw=0.8, alpha=0.8, zorder=2)
    for b, v in zip(bars, sp):
        ax.text(b.get_x() + b.get_width() / 2, v + 0.003, f"{(v - 1) * 100:+.1f}%", ha="center", va="bottom", fontsize=10,
                fontweight="bold", color="#c0392b", bbox=dict(boxstyle="round,pad=0.15", fc="white", ec="#c0392b", lw=1.0, alpha=0.95))
    ax.set_ylim(0.99, np.nanmax(sp) + 0.045); ax.yaxis.set_major_formatter(mticker.FormatStrFormatter("%.2f"))
    ax.set_xticks(range(len(sp))); ax.set_xticklabels([l for l, _, _ in ABL], fontsize=10, linespacing=1.1)
    ax.set_ylabel("Speedup over \nNo-TLB-Prefetcher", fontsize=12)
    spines(ax); ax.tick_params(labelsize=10)
    ax.grid(axis="y", lw=0.4, alpha=0.3, zorder=0); ax.grid(axis="x", visible=False); ax.set_axisbelow(True)
    save(fig, a.out)


# ─────────────────────────── Tables 4 and 5 ──────────────────────────
TABLE_CONF = {2: "cb2-ci3-ct2", 3: "cb3-ci7-ct4", 4: "cb4-ci15-ct8"}


def table_cell(results, cfg, head8mb):
    """Speedup of one configuration over its own top-200 non-srv workloads."""
    pool = [t for t in workloads(results, cfg) if not t.startswith("srv")]
    sp = []
    for t in pool:
        x, b = ipc(os.path.join(results, f"{cfg}_{t}")), ipc(os.path.join(head8mb, f"v4-noprefetch_{t}"))
        if x and b: sp.append(x / b)
    sp.sort(reverse=True)
    return (gm(sp[:200]), min(len(sp), 200)) if sp else (float("nan"), 0)


def table(a, which):
    if which == "table4":
        title = "Table 4: in-PTE TRAIL speedup over No-TLB-Prefetcher (8 MB LLC), deltas x delta bits"
        cols = [str(b) for b in range(14, 19)]; rows = range(1, 5)
        cell = lambda r, c: table_cell(a.results, f"trail-pteg-no{r}-ob{c}", a.head8mb)
        hdr = "deltas \\ delta bits"
    else:
        title = "Table 5: Trail-External speedup over No-TLB-Prefetcher (8 MB LLC), deltas x confidence bits"
        cols = ["2", "3", "4"]; rows = range(1, 9)
        cell = lambda r, c: table_cell(a.results, f"trail-sc-pl-noff{r}-{TABLE_CONF[int(c)]}", a.head8mb)
        hdr = "deltas \\ confidence bits"
    grid = {(r, c): cell(r, c) for r in rows for c in cols}
    ns = sorted({v[1] for v in grid.values()})
    md = [f"# {title}", "", f"| {hdr} | " + " | ".join(cols) + " |", "|---|" + "---:|" * len(cols)]
    md += [f"| {r} | " + " | ".join(pct(grid[(r, c)][0]) for c in cols) + " |" for r in rows]
    md += ["", f"Each cell: geomean over that configuration's top-200 non-srv workloads (workloads per cell: {ns})."]
    write_md(a.md, "\n".join(md))

    vals = np.array([[(grid[(r, c)][0] - 1) * 100 for c in cols] for r in rows])
    fig, ax = plt.subplots(figsize=(1.1 * len(cols) + 1.2, 0.36 * len(rows) + 0.9))
    im = ax.imshow(vals, cmap="Greens", aspect="auto", vmin=np.nanmin(vals) - 0.5, vmax=np.nanmax(vals) + 0.3)
    for i in range(len(rows)):
        for j in range(len(cols)):
            ax.text(j, i, f"+{vals[i, j]:.2f}%" if vals[i, j] >= 0 else f"{vals[i, j]:.2f}%", ha="center", va="center", fontsize=9)
    ax.set_xticks(range(len(cols))); ax.set_xticklabels(cols)
    ax.set_yticks(range(len(rows))); ax.set_yticklabels([str(r) for r in rows])
    ax.set_xlabel("delta bits" if which == "table4" else "confidence bits"); ax.set_ylabel("deltas")
    spines(ax)
    save(fig, a.out)


# ─────────────────────────────── main ────────────────────────────────
def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("what", choices=["figure11", "figure13", "figure16", "figure18", "figure19", "table4", "table5"])
    p.add_argument("--head2mb"); p.add_argument("--head8mb"); p.add_argument("--multicore"); p.add_argument("--abl")
    p.add_argument("--results"); p.add_argument("--top200"); p.add_argument("--mtps", nargs="*", default=[])
    p.add_argument("--out", required=True); p.add_argument("--md")
    a = p.parse_args()
    {"figure11": figure11, "figure13": figure13, "figure16": figure16, "figure18": figure18, "figure19": figure19,
     "table4": lambda a: table(a, "table4"), "table5": lambda a: table(a, "table5")}[a.what](a)


if __name__ == "__main__":
    main()
