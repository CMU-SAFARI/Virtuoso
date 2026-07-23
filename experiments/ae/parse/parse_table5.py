#!/usr/bin/env python3
"""Table 5 — In-PTE payload-budget sweep (slots x delta-bits), speedup vs ASP over top-200.
Usage: parse_table5.py --results-dir DIR --top200 top200_trail_workloads.txt [--md OUT.md]"""
import argparse, math, os, sys
sys.path.insert(0, os.path.dirname(__file__))
from _common import load, gm, read_list, speedup_vs

PAPER={1:[0.62,0.72,0.76,0.78,0.78],2:[1.48,1.55,1.59,1.60,1.61],
       3:[1.96,2.02,2.06,2.08,2.08],4:[2.27,2.33,2.36,2.38,2.38]}
DELTAS=[14,15,16,17,18]

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--results-dir", required=True)
    ap.add_argument("--top200", required=True)
    ap.add_argument("--baseline", default="v4-asp")
    ap.add_argument("--md", default=None)
    a=ap.parse_args()
    top=set(read_list(a.top200))
    asp=load(a.results_dir, a.baseline)
    lines=[]; out=lambda s:(lines.append(s), print(s))
    out("# Table 5 — In-PTE payload budget (slots x Δbits), speedup vs ASP over top-200")
    out(""); out("Format: **ours** (paper)"); out("")
    out("| slots | "+" | ".join(f"Δ{d}" for d in DELTAS)+" |")
    out("|--:|"+"|".join(["--"]*len(DELTAS))+"|")
    for s in [1,2,3,4]:
        cells=[]
        for i,d in enumerate(DELTAS):
            cfgd=load(a.results_dir, f"trail-pteg-no{s}-ob{d}")
            sp,n=speedup_vs(cfgd, asp, top)
            cells.append(f"+{sp:.2f} ({PAPER[s][i]:.2f})" if not math.isnan(sp) else "--")
        out(f"| {s} | "+" | ".join(cells)+" |")
    out(""); out(f"(baseline={a.baseline}; n_top200_matched={len([t for t in top if t in asp])})")
    if a.md: open(a.md,"w").write("\n".join(lines)+"\n"); print(f"[wrote {a.md}]")

if __name__=="__main__": main()
