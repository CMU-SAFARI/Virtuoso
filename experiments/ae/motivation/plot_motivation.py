#!/usr/bin/env python3
"""plot_motivation.py — build the motivation figures from the per-workload JSONs
(analyze_dump.py) plus, for Figure 6, four example dumps directly. Default font.

    plot_motivation.py --json-dir <dir> --dumps-dir <dir> --out-dir <dir>

Figures (paper numbers):
  figure4_pc_delta_pairs.pdf     Unique (PC, delta) pairs per workload
  figure5_topk_coverage.pdf      Top-k successor coverage (global vs PC-conditioned)
  figure6_transitions_4wl.pdf    Fraction of transitions per source region, 4 workloads
  figure8_granularity.pdf        Successor-region granularity sweep (top-4 / top-8)
  figure9_delta_representable.pdf % of representable delta occurrences vs bit-width
"""
import argparse, csv, glob, gzip, io, json, math, os, sys
from collections import defaultdict, Counter

KS = list(range(1, 9)); BITS = list(range(4, 22, 2))
SHIFT_LABEL = {0:"4KB",3:"32KB",6:"256KB",9:"2MB",12:"16MB"}

# ─────────────────────────────── aggregate figures ───────────────────────────
def load_json(json_dir):
    rows = []
    for p in glob.glob(os.path.join(json_dir, "*.json")):
        try: rows.append(json.load(open(p)))
        except Exception: pass
    return rows

def top200(rows):
    r = [d for d in rows if d.get("cov_pc")]
    r.sort(key=lambda d: d["cov_pc"][3], reverse=True)
    return r[:200]

def mean(xs): return sum(xs)/len(xs) if xs else float("nan")

def spines(ax):
    for s in ("top","right"): ax.spines[s].set_visible(False)
    for s in ("left","bottom"): ax.spines[s].set_color("black")

# ─────────────────────────────── Figure 6 (dumps) ────────────────────────────
def _open(path):
    return io.TextIOWrapper(gzip.open(path, "rb")) if path.endswith(".gz") else open(path, newline="")

def load_dump_pc_regions(path, region_shift=3):
    last = {}; data = defaultdict(lambda: defaultdict(Counter))
    with _open(path) as f:
        for row in csv.DictReader(f):
            try: pc = row["EIP"]; vpn = int(row["PTW_Address"]) >> 12
            except (KeyError, ValueError): continue
            region = vpn >> region_shift
            if pc in last and last[pc] != region:
                data[pc][last[pc]][region - last[pc]] += 1
            last[pc] = region
    return data

def find_pc(data, suffix):
    for pc in data:
        if pc.endswith(suffix): return pc
    return None

def draw_panel(ax, data, pc, title, max_regions=14, top_k=8, sort_by_id=False):
    import numpy as np
    regions = data.get(pc, {})
    if sort_by_id:
        sr = sorted(regions.items(), key=lambda x: x[0])
    else:
        sr = sorted(regions.items(), key=lambda x: sum(x[1].values()), reverse=True)
    sr = sr[:max_regions]
    cand = Counter()
    for _, dc in sr:
        for d, c in dc.most_common(top_k): cand[d] += c
    top_deltas = [d for d, _ in cand.most_common(top_k)]
    sr = [(s, dc) for s, dc in sr if sum(dc.values()) and
          sum(dc.get(d, 0) for d in top_deltas)/sum(dc.values()) >= 0.40]
    labels = []; bar = {d: [] for d in top_deltas}; other = []
    for s, dc in sr:
        tot = sum(dc.values()); labels.append(f"0x{s:x}")
        for d in top_deltas: bar[d].append(dc.get(d, 0)/tot*100 if tot else 0)
        other.append((tot - sum(dc.get(d, 0) for d in top_deltas))/tot*100 if tot else 0)
    import matplotlib.pyplot as plt
    x = np.arange(len(labels)); bottom = np.zeros(len(labels)); cmap = plt.cm.tab10
    for i, d in enumerate(top_deltas):
        v = np.array(bar[d]); sign = "+" if d >= 0 else ""
        ax.bar(x, v, 0.75, bottom=bottom, color=cmap(i % 10), edgecolor="white", linewidth=0.3,
               label=f"$\\Delta${sign}{d}"); bottom += v
    ov = np.array(other)
    if len(ov) and ov.max() > 1:
        ax.bar(x, ov, 0.75, bottom=bottom, color="#d9d9d9", edgecolor="white", linewidth=0.3, label="other")
    ax.set_xticks(x); ax.set_xticklabels(labels, rotation=55, ha="right", fontsize=5.5)
    ax.set_ylim(0, 105); ax.set_xlabel("Source Region", fontsize=7); ax.set_title(title, fontsize=8, fontweight="bold", pad=4)
    ax.legend(fontsize=5, ncol=2, loc="upper right", framealpha=0.85, borderpad=0.3, handlelength=1.0, columnspacing=0.5)

def figure6(dumps_dir, out_dir):
    import matplotlib.pyplot as plt
    def dump(w):
        for ext in (".csv.gz", ".csv"):
            p = os.path.join(dumps_dir, w + ext)
            if os.path.exists(p): return p
        return None
    specs = [("compute_int_315", "4026bc", 14, True), ("sierra.a.4_0009", None, 12, False),
             ("delta_0000", "a294b6", 8, False), ("605.mcf_s-1644b", "4033f0", 12, False)]
    paths = [dump(w) for w, *_ in specs]
    if any(p is None for p in paths):
        print("  figure6 skipped — example dumps not all present"); return
    fig, axes = plt.subplots(1, 4, figsize=(7.16, 2.3))
    letters = "abcd"
    for ax, (w, suf, mr, sbi), p, L in zip(axes, specs, paths, letters):
        data = load_dump_pc_regions(p)
        if suf: pc = find_pc(data, suf)
        else:
            tot = Counter({pc: sum(sum(dc.values()) for dc in regs.values()) for pc, regs in data.items()})
            pc = tot.most_common(1)[0][0] if tot else None
        if pc is None: continue
        draw_panel(ax, data, pc, f"({L}) {w}", max_regions=mr, top_k=8, sort_by_id=sbi)
        if ax is not axes[0]: ax.set_ylabel("")
        else: ax.set_ylabel("Fraction of transitions (%)", fontsize=7)
    fig.tight_layout(w_pad=0.8)
    for ext in ("pdf", "png"):
        fig.savefig(os.path.join(out_dir, f"figure6_transitions_4wl.{ext}"), bbox_inches="tight", pad_inches=0.1)
    plt.close(fig); print("  figure6 (4-workload transitions) written")

# ────────────────────────────────── main ─────────────────────────────────────
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json-dir", required=True); ap.add_argument("--out-dir", required=True)
    ap.add_argument("--dumps-dir", default=None)
    a = ap.parse_args()
    rows = load_json(a.json_dir)
    if not rows: print(f"no JSONs in {a.json_dir}", file=sys.stderr); sys.exit(1)
    os.makedirs(a.out_dir, exist_ok=True); sel = top200(rows)
    print(f"loaded {len(rows)} workloads ({len(sel)} in top-200)")

    import matplotlib; matplotlib.use("Agg")
    import matplotlib.pyplot as plt, matplotlib.ticker as mtick, numpy as np
    plt.rcParams.update({"savefig.dpi": 600, "axes.linewidth": 1.2, "font.size": 11,
                         "axes.grid": True, "grid.alpha": 0.3, "grid.linewidth": 0.5})
    def save(fig, name):
        for ext in ("pdf", "png"): fig.savefig(os.path.join(a.out_dir, f"{name}.{ext}"), bbox_inches="tight")
        plt.close(fig)

    # Figure 5 — top-k successor coverage (global orange / PC-cond blue)
    g = [mean([d["cov_global"][j] for d in sel]) for j in range(len(KS))]
    p = [mean([d["cov_pc"][j] for d in sel]) for j in range(len(KS))]
    fig, ax = plt.subplots(figsize=(4.6, 3.2))
    ax.plot(KS, p, "o-", color="#1f77b4", lw=2, ms=6, label="PC-conditioned")
    ax.plot(KS, g, "s-", color="#e08a1e", lw=2, ms=6, label="Global")
    ax.fill_between(KS, g, p, color="#1f77b4", alpha=0.10)
    ax.set_xlabel("Number of successor slots $k$"); ax.set_ylabel("Fraction of TLB misses covered")
    ax.set_xticks(KS); ax.set_ylim(0, 1.02); ax.yaxis.set_major_formatter(mtick.PercentFormatter(1.0, decimals=0))
    spines(ax); ax.legend(loc="lower right", fontsize=10); fig.tight_layout(); save(fig, "figure5_topk_coverage")

    # Figure 8 — destination-granularity sweep (top-4 / top-8)
    shifts = [0, 3, 6, 9, 12]
    m4 = [mean([d[f"cov4_s{s}"] for d in sel if f"cov4_s{s}" in d])*100 for s in shifts]
    m8 = [mean([d[f"cov8_s{s}"] for d in sel if f"cov8_s{s}" in d])*100 for s in shifts]
    x = np.arange(len(shifts)); w = 0.34
    fig, ax = plt.subplots(figsize=(6.4, 2.6))
    ax.bar(x-w/2, m4, w, color="#5dade2", edgecolor="black", lw=0.8, label="Top-4 successors", zorder=3)
    ax.bar(x+w/2, m8, w, color="#1a5276", edgecolor="black", lw=0.8, label="Top-8 successors", zorder=3)
    for xi,v in zip(x-w/2, m4): ax.text(xi, v+1, f"{v:.0f}", ha="center", va="bottom", fontsize=7)
    for xi,v in zip(x+w/2, m8): ax.text(xi, v+1, f"{v:.0f}", ha="center", va="bottom", fontsize=7)
    ax.set_xticks(x); ax.set_xticklabels([SHIFT_LABEL[s] for s in shifts])
    ax.set_xlabel("Successor region granularity"); ax.set_ylabel("Transitions\ncovered (%)")
    ax.set_ylim(0, 105); ax.grid(axis="y"); spines(ax); ax.legend(fontsize=9, loc="lower left")
    fig.tight_layout(); save(fig, "figure8_granularity")

    # Figure 4 — CDF of unique (PC, delta) pairs per workload
    vals = sorted(d["n_unique_pc_delta"] for d in rows if d.get("n_unique_pc_delta", 0) > 0)
    ecdf = [(i + 1) / len(vals) * 100 for i in range(len(vals))]
    fig, ax = plt.subplots(figsize=(4.8, 3.0))
    ax.plot(vals, ecdf, color="#1f77b4", lw=2.4)
    ax.set_xscale("log")
    ax.set_xlabel("Unique (PC, $\\Delta$) pairs per workload"); ax.set_ylabel("Workloads (CDF, %)")
    ax.set_ylim(0, 100); ax.grid(axis="both", which="both"); spines(ax)
    if vals:
        med = vals[len(vals) // 2]
        ax.axvline(med, color="#c0392b", ls="--", lw=1.2)
        ax.text(med, 6, f" median {med:,}", color="#c0392b", fontsize=9, ha="left")
    fig.tight_layout(); save(fig, "figure4_pc_delta_pairs")

    # Figure 9 — % of representable delta occurrences (freq-weighted)
    gw = [mean([d["global_wt"][j] for d in sel])*100 for j in range(len(BITS))]
    pw = [mean([d["pc_wt"][j] for d in sel])*100 for j in range(len(BITS))]
    fig, ax = plt.subplots(figsize=(5.4, 2.6))
    ax.plot(BITS, gw, "s-", color="tab:orange", lw=2, ms=6, label="Global $\\Delta$")
    ax.plot(BITS, pw, "^-", color="tab:green", lw=2, ms=6, label="Per-PC $\\Delta$")
    ax.fill_between(BITS, gw, pw, color="tab:green", alpha=0.12)
    ax.axvline(18, color="darkgreen", ls=":", lw=1.5, alpha=0.7); ax.axhline(95, color="gray", ls=":", lw=1, alpha=0.5)
    ax.set_xlabel("Delta bit-width $b_{off}$ (signed)"); ax.set_ylabel("% of Representable\nDelta Occurrences")
    ax.set_xticks(BITS); ax.set_ylim(0, 103); ax.grid(axis="both"); spines(ax); ax.legend(fontsize=9, loc="lower right")
    fig.tight_layout(); save(fig, "figure9_delta_representable")

    # Figure 6 — fraction of transitions per source region, 4 example workloads
    if a.dumps_dir:
        try: figure6(a.dumps_dir, a.out_dir)
        except Exception as e: print(f"  figure6 error: {e}")
    else:
        print("  figure6 skipped (pass --dumps-dir to enable)")

    print(f"wrote figures to {a.out_dir}")

if __name__ == "__main__":
    main()
