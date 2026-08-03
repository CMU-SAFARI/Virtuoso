#!/usr/bin/env python3
"""Revelator head-to-head — geomean IPC speedup vs the no-speculation baseline.

    parse_revelator.py --results-dir DIR [--variant base|thp] [--md OUT.md]

`base` parses the `revelator` suite (4KB Revelator), `thp` the `revelator_thp`
suite. Every config is compared against `rev-baseline` (radix + ReserveTHP, no
speculation) over the traces both actually completed, so a partially-failed
suite still produces a table — the trace count per row tells you the coverage.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(__file__))
from _common import load, gm

# (display name, config name in clist_revelator.yaml)
ORDER = {
    "base": [("ReserveTHP (baseline)", "rev-baseline"),
             ("SpOT",                  "rev-spot"),
             ("SpecTLB",               "rev-spectlb"),
             ("ASAP",                  "rev-asap"),
             ("Revelator (1 hash)",    "rev-revelator"),
             ("Revelator (3 hash)",    "rev-revelator-3h"),
             ("Revelator (oracle)",    "rev-revelator-oracle"),
             ("Oracle spec.",          "rev-oracle"),
             ("No translation",        "rev-notranslation")],
    "thp":  [("ReserveTHP (baseline)",   "rev-baseline"),
             ("ASAP",                    "rev-asap"),
             ("Revelator 4KB",           "rev-revelator"),
             ("Revelator-THP (1 hash)",  "rev-revelator-thp"),
             ("Revelator-THP (3 hash)",  "rev-revelator-thp-3h"),
             ("Oracle spec.",            "rev-oracle"),
             ("No translation",          "rev-notranslation")],
}
BASE = "rev-baseline"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results-dir", required=True)
    ap.add_argument("--variant", choices=["base", "thp"], default="base")
    ap.add_argument("--md", default=None)
    # accepted and ignored: ae_results.sh passes it to every parser
    ap.add_argument("--top200", default=None, help=argparse.SUPPRESS)
    a = ap.parse_args()

    order = ORDER[a.variant]
    D = {name: load(a.results_dir, cfg) for name, cfg in order}
    base = load(a.results_dir, BASE)
    if not base:
        print(f"ERROR: no results for the baseline config '{BASE}' under {a.results_dir} — "
              f"cannot compute speedups.", file=sys.stderr)
        sys.exit(1)

    lines = []
    out = lambda s: (lines.append(s), print(s))
    title = "Revelator (4KB)" if a.variant == "base" else "Revelator-THP"
    out(f"# {title} — geomean IPC speedup vs ReserveTHP (no speculation)")
    out("")
    out("| config | speedup | traces |")
    out("|--|--:|--:|")
    for name, _ in order:
        d = D[name]
        common = [t for t in d if t in base]
        if not common:
            out(f"| {name} | n/a | 0 |")
            continue
        g = gm([d[t] / base[t] for t in common])
        out(f"| {name} | {(g-1)*100:+.2f}% | {len(common)} |")

    # coverage note: rows parsed over different trace sets are not directly comparable
    counts = {name: len([t for t in D[name] if t in base]) for name, _ in order}
    if counts and len(set(counts.values())) > 1:
        out("")
        out(f"NOTE: rows cover different trace counts "
            f"({min(counts.values())}-{max(counts.values())}) — some jobs have no valid "
            f"sim.stats. Check ae_out/<suite>.DONE for the failed list before comparing rows.")

    if a.md:
        open(a.md, "w").write("\n".join(lines) + "\n")
        print(f"[wrote {a.md}]")

if __name__ == "__main__":
    main()
