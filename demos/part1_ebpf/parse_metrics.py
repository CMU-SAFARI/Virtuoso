#!/usr/bin/env python3
import re
import sys
from pathlib import Path
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.font_manager as fm



def parse_lines(text: str):
    insn = []
    cycles = []
    # Regexes for the fields we need
    re_insn = re.compile(r'\binsn=(\d+)\b')
    re_cycles = re.compile(r'cycles≈(\d+)')
    # Fallback cycles via latency (ns) and freq GHz if cycles≈ is absent.
    re_latency_ns = re.compile(r'\blatency=(\d+)\s*ns')
    re_freq = re.compile(r'core=([\d\.]+)\s*GHz')

    # Try to find frequency from the header if we need it
    freq_ghz = None
    for line in text.splitlines():
        m = re_freq.search(line)
        if m:
            try:
                freq_ghz = float(m.group(1))
            except ValueError:
                pass

    for line in text.splitlines():
        # extract instructions
        mi = re_insn.search(line)
        if mi:
            insn.append(int(mi.group(1)))

        # Prefer cycles≈ if present
        mc = re_cycles.search(line)
        if mc:
            cycles.append(int(mc.group(1)))
            continue

        # Otherwise compute cycles from latency and frequency if both available
        ml = re_latency_ns.search(line)
        if ml and freq_ghz is not None:
            lat_ns = int(ml.group(1))
            cyc = int(round(lat_ns * freq_ghz))  # cycles ≈ latency(ns) * freq(GHz)
            cycles.append(cyc)

    return np.array(insn, dtype=np.int64), np.array(cycles, dtype=np.int64)


def ecdf(data: np.ndarray):
    if data.size == 0:
        return np.array([]), np.array([])
    x = np.sort(data)
    y = np.arange(1, len(x) + 1) / len(x)
    return x, y


def setup_matplotlib():
    font_path = "/home/kanellok/.local/share/fonts/Montserrat[wght].ttf"
    fm.fontManager.addfont(font_path)
    prop = fm.FontProperties(fname=font_path)
    family_name = prop.get_name()
    print("Matplotlib font family name:", family_name)

    for f in fm.fontManager.ttflist:
        if "Montserrat" in f.name:
            print(f.name, f.weight)
    plt.rcParams.update({
        "figure.figsize": (3.33, 1.1),
        "figure.dpi": 800,
        "savefig.dpi": 800,

        # Fonts
        "font.size": 7.0,
        "font.weight": "semibold",
        "font.family": "Roboto",
        "axes.labelsize": 7.0,
        "legend.fontsize": 6.0,

        # Axis & ticks
        "axes.linewidth": 0.9,
        "xtick.direction": "out",
        "ytick.direction": "out",
        "xtick.major.size": 3,
        "ytick.major.size": 3,

        # Grid (lighter)
        "axes.grid": True,
        "grid.color": "#cccccc",
        "grid.alpha": 0.4,
        "grid.linewidth": 0.3,
    })

def plot_cdf(x, y, xlabel, title, outfile=None):
    # title argument is intentionally unused (no title for paper figs)
    setup_matplotlib()
    fig, ax = plt.subplots()

    # Main ECDF line
    colors = {
        "ecdf":    "#1f77b4",   # deep blue
        "p90":     "#ff7f0e",   # orange
        "p99":     "#d62728",   # red
    }
    ax.step(x, y, where="post", linewidth=1.6, color=colors["ecdf"])  # ECDF


    # Basic statistics
    mean_val = np.mean(x)
    median_val = np.median(x)  # unused, but kept if you want it later

    ax.axvline(
        mean_val,
        linestyle="--",
        linewidth=0.9,
        color="black",
        label=f"Mean = {int(round(mean_val))}",
    )

    # 90th & 99th percentiles
    p90 = np.percentile(x, 90)
    p99 = np.percentile(x, 99)
    p9999 = np.percentile(x, 99.9)

    ax.axvline(
        p90,
        linestyle="-.",
        linewidth=0.9,
        color=colors["p90"],
        label=f"90th = {int(round(p90))}",
    )
    ax.axvline(
        p99,
        linestyle="-",
        linewidth=0.9,
        color=colors["p99"],
        label=f"99th = {int(round(p99))}",
    )

    # Labels
    ax.set_xlabel(xlabel, fontweight="bold")
    ax.set_ylabel("CDF", fontweight="bold")

    # X/Y limits
    ax.set_xlim(left=0.0, right=p9999 * 1.1)

    yticks = np.linspace(0.0, 1.0, 6)
    ax.set_yticks(yticks)
    ax.set_yticklabels([f"{t * 100:.0f}" for t in yticks])

    ax.margins(x=0.02)

    # --------- auto legend placement ------------
    # Decide which side the vertical lines mostly occupy
    x_min, x_max = ax.get_xlim()
    # cluster around the three main markers
    cluster_pos = np.mean([mean_val, p90, p99])
    cluster_norm = (cluster_pos - x_min) / (x_max - x_min)

    if cluster_norm > 0.5:
        # Markers are on the right half → put legend on the left
        legend_loc = "lower left"
        legend_anchor = (0.35, 0.05)  # axes fraction coordinates
    else:
        # Markers on the left → put legend on the right
        legend_loc = "lower right"
        legend_anchor = (0.97, 0.05)

    legend = ax.legend(
        loc=legend_loc,
        bbox_to_anchor=legend_anchor,
        bbox_transform=ax.transAxes,
        frameon=True,
        framealpha=0.85,
        borderpad=0.15,
        labelspacing=0.25,
        handlelength=1.2,
        handletextpad=0.4,
    )
    # (optional) slightly smaller legend text independent of global rc
    for text in legend.get_texts():
        text.set_fontsize(6.0)

    def fmt_k(x, _):
        if x >= 1000:
            return f"{int(x/1000)}K"
        return str(int(x))

    ax.xaxis.set_major_formatter(plt.FuncFormatter(fmt_k))
    fig.tight_layout(pad=0.9)

    if outfile:
        fig.savefig(outfile, dpi=800)  # no bbox_inches, rcParam is None now
    else:
        plt.show()

def main():
    import argparse
    p = argparse.ArgumentParser(
        description="Parse pgfault trace and plot CDFs of instructions and fault latency (cycles)."
    )
    p.add_argument("input", nargs="?", help="Input file (if omitted, read stdin).")
    p.add_argument("--out-prefix", default="pgflt_cdf", help="Prefix for saved figures.")
    p.add_argument("--no-show", action="store_true", help="Do not display plots (only save).")
    args = p.parse_args()

    # Read input text
    if args.input:
        text = Path(args.input).read_text(encoding="utf-8", errors="ignore")
    else:
        text = sys.stdin.read()

    insn, cycles = parse_lines(text)

    if insn.size == 0 and cycles.size == 0:
        print("No data parsed. Check input format.")
        sys.exit(1)

    # Compute ECDFs
    xi, yi = ecdf(insn) if insn.size else (np.array([]), np.array([]))
    xc, yc = ecdf(cycles) if cycles.size else (np.array([]), np.array([]))

    # Plot and save
    out1 = f"{args.out_prefix}_insn_cdf.png"
    out2 = f"{args.out_prefix}_cycles_cdf.png"

    if xi.size:
        plot_cdf(xi, yi, xlabel=" Instructions executed during a minor page fault", title="CDF of instructions", outfile=out1)
    if xc.size:
        plot_cdf(
            xc,
            yc,
            xlabel="Page fault latency (cycles)",
            title="CDF of page fault latency",
            outfile=out2,
        )

    if not args.no_show:
        plt.show()

    print(f"Saved: {out1}" if xi.size else "No instruction data to plot.")
    print(f"Saved: {out2}" if xc.size else "No cycles data to plot.")


if __name__ == "__main__":
    main()
