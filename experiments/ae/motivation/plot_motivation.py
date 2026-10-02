#!/usr/bin/env python3
"""plot_motivation.py — the paper's motivation figures.

    plot_motivation.py --json-dir <dir> --tlb-dir <dir> --out-dir <dir> [--regions <csv>]

Inputs: one JSON per workload from analyze_dump.py (--json-dir), one JSON per
workload from tlb_prefetch_sim (--tlb-dir), and the selected source regions of
Figure 3 (--regions, default data/figure3_selected_regions.csv).  Figures 2, 5,
6 and 7 average over the top-200 workloads by per-PC coverage at k=4.

  figure2_topk_coverage        Top-k most frequent deltas per region: global vs per-PC
  figure3_delta_distributions  Per-PC delta distributions of selected 32 KB source regions
  figure5_granularity          Captured transitions vs destination-region size (top-4 / top-8)
  figure6_pte_fate             Used vs unused prefetched PTEs in a 1536-entry TLB
  figure7_delta_bits           Representable delta occurrences vs delta bit-width
"""
import argparse, colorsys, csv, glob, json, os, sys
from collections import defaultdict

KS = list(range(1, 9)); BITS = list(range(4, 22, 2))
SHIFTS = [0, 3, 6, 9, 12]
SHIFT_LABEL = {0: "4KB", 3: "32KB", 6: "256KB", 9: "2MB", 12: "16MB"}
HERE = os.path.dirname(os.path.abspath(__file__))


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


def mean(xs): return sum(xs) / len(xs) if xs else float("nan")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--json-dir", required=True); ap.add_argument("--tlb-dir")
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--regions", default=os.path.join(HERE, "data", "figure3_selected_regions.csv"))
    a = ap.parse_args()
    rows = load_json(a.json_dir)
    if not rows: print(f"no JSONs in {a.json_dir}", file=sys.stderr); sys.exit(1)
    os.makedirs(a.out_dir, exist_ok=True); sel = top200(rows)
    print(f"loaded {len(rows)} workloads ({len(sel)} in top-200)")

    import matplotlib; matplotlib.use("Agg")
    import matplotlib.pyplot as plt, matplotlib.ticker as mtick, matplotlib.font_manager as fm
    import numpy as np
    from matplotlib.colors import to_hex, to_rgb
    roboto = sorted(f for f in fm.findSystemFonts() if os.path.basename(f).startswith("Roboto-") and f.endswith(".ttf"))
    for f in roboto: fm.fontManager.addfont(f)
    if any(os.path.basename(f) == "Roboto-Regular.ttf" for f in roboto): plt.rcParams["font.family"] = "Roboto"
    plt.rcParams.update({"savefig.dpi": 600, "axes.linewidth": 1.2, "grid.linewidth": 0.4, "grid.alpha": 0.3,
                         "pdf.fonttype": 42})

    def paper_axes(ax, ymajor=20, yminor=10):
        for sp in ax.spines.values(): sp.set_visible(True); sp.set_color("black"); sp.set_linewidth(1.2)
        if ymajor: ax.yaxis.set_major_locator(mtick.MultipleLocator(ymajor))
        if yminor: ax.yaxis.set_minor_locator(mtick.MultipleLocator(yminor))
        ax.tick_params(axis="both", which="major", direction="out", length=5, width=1.0, top=False, right=False, labelsize=10)
        ax.tick_params(axis="both", which="minor", direction="out", length=3, width=0.7, top=False, right=False)
        ax.grid(axis="y"); ax.set_axisbelow(True)

    def save(fig, name):
        for ext in ("pdf", "png"): fig.savefig(os.path.join(a.out_dir, f"{name}.{ext}"), bbox_inches="tight")
        plt.close(fig); print(f"  {name}")

    # Figure 2 — top-k coverage, global vs per-PC (mean ± 1 std dev over workloads)
    g = np.array([d["cov_global"] for d in sel]) * 100; p = np.array([d["cov_pc"] for d in sel]) * 100
    gm_, gs = g.mean(0), g.std(0, ddof=1); pm_, ps = p.mean(0), p.std(0, ddof=1); ks = np.array(KS)
    fig, ax = plt.subplots(figsize=(5.5, 2.5))
    ax.plot(ks, gm_, "o-", color="#e67e22", lw=2.2, ms=7, mec="white", mew=0.8, label="Global", zorder=3)
    ax.plot(ks, pm_, "s-", color="#2980b9", lw=2.2, ms=7, mec="white", mew=0.8, label="Per-PC", zorder=3)
    ax.fill_between(ks, pm_ - ps, pm_ + ps, color="#2980b9", alpha=0.15, zorder=1)
    ax.fill_between(ks, gm_ - gs, gm_ + gs, color="#e67e22", alpha=0.15, zorder=1, label="±1 std dev")
    for k in (1, 4, 8):
        j = KS.index(k); y = (pm_[j] + gm_[j]) / 2
        ax.annotate(f"+{pm_[j] - gm_[j]:.1f}", xy=(k, y), xytext=(k - 0.5, y), fontsize=10, fontweight="bold",
                    color="#c0392b", ha="left", va="center",
                    bbox=dict(boxstyle="round,pad=0.15", fc="white", ec="#c0392b", lw=1.0, alpha=0.95))
    ax.set_xlabel("Top-K most frequent deltas per virtual memory region", fontsize=12)
    ax.set_ylabel("Fraction of captured\ntransitions (%)", fontsize=12)
    ax.set_xticks(KS); ax.set_ylim(0, 100); ax.set_xlim(0.5, 8.5)
    ax.legend(loc="lower right", fontsize=9, framealpha=0.95, edgecolor="0.8"); paper_axes(ax)
    fig.tight_layout(); save(fig, "figure2_topk_coverage")
    print("    per-PC minus global (pp): " + ", ".join(f"k={k}: +{pm_[KS.index(k)] - gm_[KS.index(k)]:.1f}" for k in (1, 4, 8)))

    # Figure 3 — per-PC delta distributions of selected source regions (4 workloads)
    if os.path.exists(a.regions):
        panels = defaultdict(list)
        for row in csv.DictReader(open(a.regions, newline="")):
            panels[row["workload"]].append({k: int(v) for k, v in row.items()
                                            if k not in ("workload", "pc_hex", "source_region_hex", "top5_rate", "top6_rate")})

        def series(regions):
            counts = [{r[f"delta{i}"]: r[f"count{i}"] for i in range(1, 7) if r[f"count{i}"] > 0} for r in regions]
            deltas = sorted(set().union(*(set(c) for c in counts)))
            return {d: [100 * c.get(d, 0) / r["transitions"] for c, r in zip(counts, regions)] for d in deltas}
        all_d = sorted(set().union(*(set(series(r)) for r in panels.values())))
        light = (0.52, 0.64, 0.73)
        colors = {d: to_hex(colorsys.hls_to_rgb((0.54 + i * 0.61803398875) % 1, light[i % 3], 0.42)) for i, d in enumerate(all_d)}
        fig, axes = plt.subplots(2, 2, figsize=(9, 7), layout="constrained")
        for pi, (ax, (wl, regions)) in enumerate(zip(axes.flat, panels.items())):
            pos = list(range(len(regions))); bottom = [0.0] * len(regions)
            for d, vals in series(regions).items():
                act = [i for i, v in enumerate(vals) if v > 0]
                ax.bar([pos[i] for i in act], [vals[i] for i in act], bottom=[bottom[i] for i in act],
                       color=colors[d], edgecolor="#303030", linewidth=1.1)
                r_, g_, b_ = to_rgb(colors[d]); tc = "white" if 0.299 * r_ + 0.587 * g_ + 0.114 * b_ < 0.5 else "black"
                for x_, v, b in zip(pos, vals, bottom):
                    if v >= 5: ax.text(x_, b + v / 2, f"{d:+d}", ha="center", va="center", fontsize=8, color=tc)
                bottom = [b + v for b, v in zip(bottom, vals)]
            ax.bar(pos, [100 - b for b in bottom], bottom=bottom, color="#dedede", edgecolor="#303030", linewidth=1.1, hatch="//")
            for x_, cov in zip(pos, bottom): ax.text(x_, 103, f"{cov:.1f}%", ha="center", fontsize=9)
            ax.set_yticks(range(0, 101, 20)); ax.set_axisbelow(True)
            ax.yaxis.grid(True, color="#555555", linewidth=0.7, alpha=0.22); ax.xaxis.grid(False)
            ax.set_xticks(pos, [f"{r['source_region']:#x}\nn={r['transitions']:,}" for r in regions], rotation=25, ha="right", fontsize=8)
            sp = ax.get_subplotspec()
            ax.set(ylabel="Observed deltas (%)" if sp.is_first_col() else "",
                   xlabel="Source 32 KiB region" if sp.is_last_row() else "", ylim=(0, 113))
            ax.set_title(wl, fontweight="bold", fontsize=13, pad=8)
            ax.text(0.01, 1.02, f"({chr(ord('a') + pi)})", transform=ax.transAxes, ha="left", va="bottom", fontsize=12, fontweight="bold")
            for s in ax.spines.values(): s.set_visible(True); s.set_color("#303030"); s.set_linewidth(1.6)
        save(fig, "figure3_delta_distributions")
    else:
        print(f"  figure3 skipped (no {a.regions})")

    # Figure 5 — destination-region granularity (top-4 / top-8), mean ± 1 std dev
    col = lambda key: np.array([d[key] for d in sel if key in d]) * 100
    m4 = [col(f"cov4_s{s}").mean() for s in SHIFTS]; s4 = [col(f"cov4_s{s}").std(ddof=1) for s in SHIFTS]
    m8 = [col(f"cov8_s{s}").mean() for s in SHIFTS]; s8 = [col(f"cov8_s{s}").std(ddof=1) for s in SHIFTS]
    w = 0.34; x = np.arange(len(SHIFTS)); ek = {"linewidth": 0.7, "color": "0.3"}
    fig, ax = plt.subplots(figsize=(7.0, 2.6))
    ax.bar(x - w / 2, m4, w, color="#5dade2", edgecolor="black", linewidth=0.8, yerr=s4, capsize=2, error_kw=ek,
           label="Top-4 Most-Frequent Deltas", zorder=3)
    ax.bar(x + w / 2, m8, w, color="#1a5276", edgecolor="black", linewidth=0.8, yerr=s8, capsize=2, error_kw=ek,
           label="Top-8 Most-Frequent Deltas", zorder=3)
    for i in range(len(SHIFTS)):
        ax.text(x[i] - w / 2, m4[i] + s4[i] + 1.0, f"{m4[i]:.1f}", ha="center", va="bottom", fontsize=10, fontweight="bold", color="#2471a3")
        ax.text(x[i] + w / 2, m8[i] + s8[i] + 1.0, f"{m8[i]:.1f}", ha="center", va="bottom", fontsize=10, fontweight="bold", color="#1a5276")
    ax.set_xticks(x); ax.set_xticklabels([SHIFT_LABEL[s] for s in SHIFTS], fontsize=14, rotation=20)
    ax.set_xlabel("Size of Destination Region", fontsize=12); ax.set_ylabel("Fraction of captured \n transitions (%)", fontsize=12)
    ax.set_ylim(0, max(max(np.add(m8, s8)), max(np.add(m4, s4))) + 18)
    ax.legend(fontsize=11, loc="lower right", framealpha=0.9); paper_axes(ax)
    ax.tick_params(axis="x", which="major", length=0); ax.tick_params(axis="y", which="major", labelsize=14)
    fig.tight_layout(); save(fig, "figure5_granularity")
    print("    top-4 / top-8 (%): " + ", ".join(f"{SHIFT_LABEL[s]} {u:.1f}/{v:.1f}" for s, u, v in zip(SHIFTS, m4, m8)))

    # Figure 6 — fate of prefetched PTEs in a 1536-entry, 4-way TLB (tlb_prefetch_sim)
    tlb = {}
    for f in glob.glob(os.path.join(a.tlb_dir or "", "*.json")):
        try: d = json.load(open(f)); tlb[d["workload"]] = d
        except Exception: pass
    names = [d["workload"] for d in sel if d["workload"] in tlb]
    if names:
        def summ(shift, k):
            used, ev, end = [], [], []
            for wl in names:
                r = next((r for r in tlb[wl]["results"] if r["shift"] == shift and r["k"] == k), None)
                if r and r["pte_fetched"]:
                    f_ = r["pte_fetched"]; used.append(r["pte_used"] / f_)
                    ev.append(r["pte_evicted_unused"] / f_); end.append(r["pte_unused_at_end"] / f_)
            return mean(used) * 100, mean(ev) * 100, mean(end) * 100
        TK = [4, 8]; BW = 0.34
        fig, ax = plt.subplots(figsize=(7.0, 3.2)); x = np.arange(len(SHIFTS))
        for j, k in enumerate(TK):
            m = np.array([summ(s, k) for s in SHIFTS]); xo = x + (j - 0.5) * BW
            ax.bar(xo, m[:, 0], BW, color="#71DD9E", edgecolor="black", linewidth=0.8, zorder=3, label="Used" if j == 0 else None)
            ax.bar(xo, m[:, 1] + m[:, 2], BW, bottom=m[:, 0], color="#2A7348", edgecolor="black", linewidth=0.8, zorder=3,
                   label="Evicted unused" if j == 0 else None)
            for xi, u in zip(xo, m[:, 0]):
                ax.text(xi, u + 1.5, f"{u:.1f}", ha="center", va="bottom", fontsize=10, fontweight="bold", color="white", zorder=4)
                ax.text(xi, 101.5, f"k={k}", ha="center", va="bottom", fontsize=11)
            print(f"    k={k} used (%): " + ", ".join(f"{SHIFT_LABEL[s]} {u:.1f}" for s, u in zip(SHIFTS, m[:, 0])))
        ax.set_xticks(x); ax.set_xticklabels([SHIFT_LABEL[s] for s in SHIFTS], fontsize=14, rotation=20)
        ax.set_xlabel("Size of Destination Region", fontsize=12); ax.set_ylabel("Prefetched PTEs (%)", fontsize=12)
        ax.set_ylim(0, 100)
        ax.legend(fontsize=11, loc="lower center", bbox_to_anchor=(0.5, 1.1), ncol=2, frameon=False)
        paper_axes(ax, ymajor=25, yminor=None)
        ax.tick_params(axis="x", which="major", length=0); ax.tick_params(axis="y", which="major", labelsize=14)
        fig.tight_layout(); save(fig, "figure6_pte_fate")
    else:
        print("  figure6 skipped (no TLB-simulation results; pass --tlb-dir)")

    # Figure 7 — representable delta occurrences vs signed bit-width
    gw = [mean([d["global_wt"][j] for d in sel]) * 100 for j in range(len(BITS))]
    pw = [mean([d["pc_wt"][j] for d in sel]) * 100 for j in range(len(BITS))]
    fig, ax = plt.subplots(figsize=(5.5, 2.3))
    ax.plot(BITS, gw, "s-", color="tab:orange", lw=2, ms=6, label="Global Delta")
    ax.plot(BITS, pw, "^-", color="tab:green", lw=2, ms=6, label="Per-PC Delta")
    ax.fill_between(BITS, gw, pw, color="tab:green", alpha=0.15)
    ax.axvline(18, color="darkgreen", ls=":", lw=1.5, alpha=0.7); ax.axhline(95, color="gray", ls=":", lw=1, alpha=0.5)
    ax.set_xlabel(r"Bit-width $b_{off}$ (signed)", fontsize=13); ax.set_ylabel("% of Representable\n Delta Occurrences", fontsize=13)
    ax.set_ylim(0, 105); ax.set_xticks(BITS)
    ax.legend(fontsize=13, loc="lower right", bbox_to_anchor=(0.4, 0.45)); paper_axes(ax)
    for s in ax.spines.values(): s.set_linewidth(0.8)
    ax.tick_params(axis="both", which="major", length=6, labelsize=14); ax.xaxis.set_minor_locator(mtick.MultipleLocator(1))
    fig.tight_layout(); save(fig, "figure7_delta_bits")
    j16, j18 = BITS.index(16), BITS.index(18)
    print(f"    per-PC: 16 bits {pw[j16]:.1f}%, 18 bits {pw[j18]:.1f}%; global: 18 bits {gw[j18]:.1f}%")
    print(f"wrote figures to {a.out_dir}")


if __name__ == "__main__":
    main()
