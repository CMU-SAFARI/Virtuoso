#!/usr/bin/env python3
"""Table 6 — Trail-External / side-car payload sweep (slots x conf), speedup vs ASP over top-200.
Usage: parse_table6.py --results-dir DIR --top200 top200_trail_workloads.txt [--md OUT.md]"""
import argparse, math, os, sys
sys.path.insert(0, os.path.dirname(__file__))
from _common import load, gm, read_list, speedup_vs

PAPER={1:[0.75,0.74,0.74],2:[1.38,1.45,1.48],3:[1.35,1.87,1.92],4:[1.33,2.02,2.19],
       5:[1.33,1.96,2.36],6:[1.32,1.92,2.43],7:[1.29,1.88,2.48],8:[1.29,1.88,2.49]}
CONF={2:"cb2-ci3-ct2",3:"cb3-ci7-ct4",4:"cb4-ci15-ct8"}

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
    out("# Table 6 — Side-car payload (slots x conf), speedup vs ASP over top-200")
    out(""); out("Format: **ours** (paper)"); out("")
    out("| slots | conf2 | conf3 | conf4 |")
    out("|--:|--|--|--|")
    for s in range(1,9):
        cells=[]
        for i,c in enumerate([2,3,4]):
            cfgd=load(a.results_dir, f"trail-sc-pl-noff{s}-{CONF[c]}")
            sp,n=speedup_vs(cfgd, asp, top)
            cells.append(f"+{sp:.2f} ({PAPER[s][i]:.2f})" if not math.isnan(sp) else "--")
        out(f"| {s} | "+" | ".join(cells)+" |")
    out(""); out(f"(baseline={a.baseline}; n_top200_matched={len([t for t in top if t in asp])})")
    if a.md: open(a.md,"w").write("\n".join(lines)+"\n"); print(f"[wrote {a.md}]")

if __name__=="__main__": main()
