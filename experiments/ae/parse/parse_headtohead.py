#!/usr/bin/env python3
"""Head-to-head — all configs, geomean speedup vs no-prefetch (top-200 by TRAIL) and vs ASP.
Works for 8MB (suffix="") or 2MB (suffix="-nuca2mb").
Usage: parse_headtohead.py --results-dir DIR --top200 FILE [--suffix -nuca2mb] [--md OUT.md]"""
import argparse, math, os, sys
sys.path.insert(0, os.path.dirname(__file__))
from _common import load, gm, read_list

ORDER=[("ASP","v4-asp"),("+Stride","v4-asp-stride"),("+Recency","v4-asp-recency"),
       ("+DP","v4-asp-dp"),("+ATP","v4-asp-atp"),("+Berti","v4-asp-berti"),
       ("TRAIL","v4-trail"),("TRAIL-sc","v4-trail-sidecar"),("Perfect-L2TLB","v4-perfect-l2tlb")]

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--results-dir", required=True)
    ap.add_argument("--top200", required=True)
    ap.add_argument("--suffix", default="")
    ap.add_argument("--md", default=None)
    a=ap.parse_args()
    sfx=a.suffix
    nopf=load(a.results_dir, "v4-noprefetch"+sfx)
    D={name:load(a.results_dir, cfg+sfx) for name,cfg in ORDER}
    top=set(read_list(a.top200))
    # rank the fixed top-200 by TRAIL speedup among present traces
    tr=D["TRAIL"]
    common=[t for t in top if t in nopf and t in tr]
    ranked=sorted(common, key=lambda t: tr[t]/nopf[t], reverse=True)[:200]
    aspg=gm([D["ASP"][t]/nopf[t] for t in ranked if t in D["ASP"]])
    lines=[]; out=lambda s:(lines.append(s), print(s))
    size="2MB" if sfx else "8MB"
    out(f"# Head-to-head {size} — speedup vs no-prefetch over top-200 ({len(ranked)} traces)")
    out(""); out("| config | vs no-pf | vs ASP |"); out("|--|--:|--:|")
    for name,_ in ORDER:
        d=D[name]; pts=[d[t]/nopf[t] for t in ranked if t in d]
        g=gm(pts)
        out(f"| {name} | {(g-1)*100:+.2f}% | {(g/aspg-1)*100:+.2f}% |")
    if a.md: open(a.md,"w").write("\n".join(lines)+"\n"); print(f"[wrote {a.md}]")

if __name__=="__main__": main()
