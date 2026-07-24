#!/usr/bin/env python3
"""plot_claim.py — render a claim's parsed table as a camera-ready bar chart.

    plot_claim.py --md ae_out/<claim>.md --out ae_out/<claim>.pdf [--col 1]

Reads the markdown table the parser produced, takes the first data column by
default (the headline speedup), and draws one bar per row.  Style: black spines,
y-grid only, percentage labels above bars, no title/legend (paper style).
"""
import argparse, re, sys

def read_md_table(path):
    rows, header = [], None
    for line in open(path):
        line = line.strip()
        if not line.startswith("|"):
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        if set("".join(cells)) <= set("-: "):   # separator row
            continue
        if header is None:
            header = cells
        else:
            rows.append(cells)
    return header, rows

def to_num(s):
    m = re.search(r'[-+]?\d+(?:\.\d+)?', s.replace("%", ""))
    return float(m.group()) if m else None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--md", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--col", type=int, default=1, help="data column index (0=label)")
    ap.add_argument("--title", default=None)
    a = ap.parse_args()

    header, rows = read_md_table(a.md)
    if not rows:
        print(f"no table rows in {a.md}", file=sys.stderr); sys.exit(1)

    labels = [r[0] for r in rows]
    vals = [to_num(r[a.col]) if a.col < len(r) else None for r in rows]
    pairs = [(l, v) for l, v in zip(labels, vals) if v is not None]
    if not pairs:
        print(f"no numeric data in column {a.col} of {a.md}", file=sys.stderr); sys.exit(1)
    labels, vals = zip(*pairs)

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib import font_manager
    for fam in ("Roboto", "DejaVu Sans"):
        if any(fam in f.name for f in font_manager.fontManager.ttflist):
            plt.rcParams["font.family"] = fam; break
    plt.rcParams.update({"font.size": 11, "axes.linewidth": 1.2})

    fig, ax = plt.subplots(figsize=(max(4, 0.7*len(labels)), 3.2))
    xs = range(len(labels))
    bars = ax.bar(xs, vals, width=0.68, color="#3b6ea5", edgecolor="black", linewidth=0.8, zorder=3)
    ax.axhline(0, color="black", linewidth=1.0, zorder=2)
    ax.set_xticks(list(xs)); ax.set_xticklabels(labels, rotation=45, ha="right")
    ycol = header[a.col] if header and a.col < len(header) else "value"
    ax.set_ylabel(ycol)
    ax.grid(axis="y", linestyle="-", linewidth=0.6, alpha=0.4, zorder=0)
    for s in ("top", "right"): ax.spines[s].set_visible(False)
    for s in ("left", "bottom"): ax.spines[s].set_color("black")
    vmax = max(vals); off = 0.02 * (vmax if vmax > 0 else 1)
    for b, v in zip(bars, vals):
        ax.text(b.get_x()+b.get_width()/2, v + (off if v >= 0 else -off),
                f"{v:+.1f}%", ha="center", va="bottom" if v >= 0 else "top",
                rotation=90, fontsize=8)
    if a.title: ax.set_title(a.title)
    fig.tight_layout()
    fig.savefig(a.out, dpi=600, bbox_inches="tight")
    print(f"[wrote {a.out}]")

if __name__ == "__main__":
    main()
