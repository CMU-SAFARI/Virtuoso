#!/usr/bin/env python3
"""plot_utilsweep.py — Revelator speedup vs memory occupancy, one line per variant.

    plot_utilsweep.py --md ae_out/utilsweep.md --out ae_out/utilsweep.pdf

Reads the markdown table parse_utilsweep.py produced (rows = variants, columns =
memory occupancy) and draws one line per variant. Style: black spines, y-grid
only, no title/legend box clutter.
"""
import argparse, re, sys

def read_md_table(path):
    header, rows = None, []
    for line in open(path):
        s = line.strip()
        if not s.startswith("|"):
            continue
        cells = [c.strip() for c in s.strip("|").split("|")]
        if set("".join(cells)) <= set("-: "):        # separator row
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
    a = ap.parse_args()

    header, rows = read_md_table(a.md)
    if not rows or not header or len(header) < 2:
        print(f"no usable table in {a.md}", file=sys.stderr); sys.exit(1)

    xlabels = header[1:]
    xs = [to_num(h) for h in xlabels]
    if any(x is None for x in xs):                   # non-numeric headers -> positional
        xs = list(range(len(xlabels)))

    series = []
    for r in rows:
        vals = [to_num(c) if c else None for c in r[1:len(header)]]
        if any(v is not None for v in vals):
            series.append((r[0], vals))
    if not series:
        print(f"no numeric data in {a.md}", file=sys.stderr); sys.exit(1)

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib import font_manager
    for fam in ("Roboto", "DejaVu Sans"):
        if any(fam in f.name for f in font_manager.fontManager.ttflist):
            plt.rcParams["font.family"] = fam; break
    plt.rcParams.update({"font.size": 11, "axes.linewidth": 1.2})

    fig, ax = plt.subplots(figsize=(5.4, 3.4))
    markers = ["o", "s", "^", "D", "v"]
    for i, (label, vals) in enumerate(series):
        pts = [(x, v) for x, v in zip(xs, vals) if v is not None]
        if not pts:
            continue
        px, pv = zip(*pts)
        ax.plot(px, pv, marker=markers[i % len(markers)], linewidth=1.6,
                markersize=5, label=label, zorder=3)

    ax.axhline(0, color="black", linewidth=1.0, zorder=2)
    ax.set_xticks(xs); ax.set_xticklabels(xlabels)
    ax.set_xlabel("memory occupancy")
    ax.set_ylabel("IPC speedup vs ReserveTHP (%)")
    ax.grid(axis="y", linestyle="-", linewidth=0.6, alpha=0.4, zorder=0)
    for s in ("top", "right"): ax.spines[s].set_visible(False)
    for s in ("left", "bottom"): ax.spines[s].set_color("black")
    ax.legend(frameon=False, fontsize=9)
    fig.tight_layout()
    fig.savefig(a.out, dpi=600, bbox_inches="tight")
    print(f"[wrote {a.out}]")

if __name__ == "__main__":
    main()
