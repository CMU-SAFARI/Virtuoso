#!/usr/bin/env python3
"""Revelator memory-utilization sweep — speedup vs ReserveTHP at each fill level.

    parse_utilsweep.py --results-dir DIR [--md OUT.md]

Revelator's speculation is only correct while the allocator can still honour the
hash, so the question is how the win holds up as memory fills. The sweep varies
`--perf_model/revelator/target_fragmentation`, where 1.0 means 0% of memory is
already occupied and 0.2 means 80% is. Run dirs are named
`<config>-util<value>_<trace>` by create_experiments.py.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(__file__))
from _common import load, gm

VARIANTS = [("Revelator (1 hash)",   "rev-util-1h"),
            ("Revelator (3 hash)",   "rev-util-3h"),
            ("Revelator-THP",        "rev-util-thp")]
# target_fragmentation -> percent of memory already occupied
UTIL = [(1.0, 0), (0.8, 20), (0.6, 40), (0.4, 60), (0.2, 80)]
BASE = "rev-baseline"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results-dir", required=True)
    ap.add_argument("--md", default=None)
    ap.add_argument("--top200", default=None, help=argparse.SUPPRESS)
    a = ap.parse_args()

    base = load(a.results_dir, BASE)
    if not base:
        print(f"ERROR: no results for '{BASE}' under {a.results_dir} — cannot compute speedups.",
              file=sys.stderr)
        sys.exit(1)

    lines = []
    out = lambda s: (lines.append(s), print(s))
    out("# Revelator — geomean IPC speedup vs ReserveTHP as memory fills")
    out("")
    out("| variant | " + " | ".join(f"{p}% full" for _, p in UTIL) + " |")
    out("|--|" + "--:|" * len(UTIL))

    for name, cfg in VARIANTS:
        cells = []
        for frag, _pct in UTIL:
            d = load(a.results_dir, f"{cfg}-util{frag}")
            common = [t for t in d if t in base]
            cells.append(f"{(gm([d[t]/base[t] for t in common])-1)*100:+.2f}%" if common else "n/a")
        out(f"| {name} | " + " | ".join(cells) + " |")

    out("")
    out("Columns are memory occupancy (`revelator/target_fragmentation` 1.0 -> 0% full, "
        "0.2 -> 80% full). Baseline is ReserveTHP with no speculation, held fixed across columns.")

    if a.md:
        open(a.md, "w").write("\n".join(lines) + "\n")
        print(f"[wrote {a.md}]")

if __name__ == "__main__":
    main()
