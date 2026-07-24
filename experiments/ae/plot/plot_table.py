#!/usr/bin/env python3
"""plot_table.py — render a parsed markdown table as a PDF/PNG image.

    plot_table.py --md ae_out/table5.md --out ae_out/table5.pdf [--title "..."]

Used for the grid suites (Table 5 / Table 6) so the reviewer gets a rendered
table image, not just markdown. Default matplotlib font.
"""
import argparse, sys

def read_md(path):
    header, rows, title = None, [], None
    for line in open(path):
        s = line.rstrip("\n")
        if s.startswith("#") and title is None:
            title = s.lstrip("# ").strip(); continue
        if not s.strip().startswith("|"):
            continue
        cells = [c.strip() for c in s.strip().strip("|").split("|")]
        if set("".join(cells)) <= set("-: "):          # separator row
            continue
        if header is None: header = cells
        else: rows.append(cells)
    return title, header, rows

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--md", required=True); ap.add_argument("--out", required=True)
    ap.add_argument("--title", default=None)
    a = ap.parse_args()
    title, header, rows = read_md(a.md)
    if not header or not rows:
        print(f"no table found in {a.md}", file=sys.stderr); sys.exit(1)
    ncol = max(len(header), max(len(r) for r in rows))
    header += [""] * (ncol - len(header))
    rows = [r + [""] * (ncol - len(r)) for r in rows]

    import matplotlib; matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    plt.rcParams.update({"savefig.dpi": 300})
    fig, ax = plt.subplots(figsize=(min(2.0 + 1.5*ncol, 14), 0.5 + 0.34*(len(rows)+1)))
    ax.axis("off")
    ttl = a.title or title
    if ttl: ax.set_title(ttl, fontsize=12, fontweight="bold", pad=10)
    tbl = ax.table(cellText=rows, colLabels=header, cellLoc="center", loc="center")
    tbl.auto_set_font_size(False); tbl.set_fontsize(10); tbl.scale(1, 1.4)
    for (r, c), cell in tbl.get_celld().items():
        cell.set_edgecolor("black"); cell.set_linewidth(1.0)
        if r == 0:                                   # header row
            cell.set_facecolor("#1b4332"); cell.get_text().set_color("white"); cell.get_text().set_fontweight("bold")
        elif c == 0:                                 # first column (row labels)
            cell.set_facecolor("#ebebeb"); cell.get_text().set_fontweight("bold")
        else:
            cell.set_facecolor("white")
    fig.savefig(a.out, bbox_inches="tight")
    print(f"[wrote {a.out}]  {len(rows)}x{ncol} table")

if __name__ == "__main__":
    main()
