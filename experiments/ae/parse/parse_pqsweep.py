#!/usr/bin/env python3
"""PQ-size sweep — {Berti, TRAIL} speedup vs same-size ASP over top-200, PQ in {64..1024}.
Usage: parse_pqsweep.py --results-dir DIR --top200 FILE [--md OUT.md]"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(__file__))
from _common import load, gm, read_list, speedup_vs

PQS=[64,128,256,512,1024]

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--results-dir", required=True)
    ap.add_argument("--top200", required=True)
    ap.add_argument("--md", default=None)
    a=ap.parse_args()
    top=set(read_list(a.top200))
    lines=[]; out=lambda s:(lines.append(s), print(s))
    out("# PQ-size sweep — TRAIL speedup vs same-size ASP over top-200")
    out(""); out("| PQ | TRAIL vs ASP |"); out("|--:|--:|")
    # PQ=0 (cache-only): prefetch warms PTE cache lines, no PQ install (install_pq=false)
    asp0=load(a.results_dir, "asp-pq-nopq")
    trail0=load(a.results_dir, "trail-pq-nopq")
    if asp0 and trail0:
        t0,_=speedup_vs(trail0, asp0, top)
        out(f"| 0 (cache-only) | {t0:+.2f}% |")
    for pq in PQS:
        asp=load(a.results_dir, f"asp-pq-pq{pq}")
        trail=load(a.results_dir, f"trail-pq-pq{pq}")
        tsp,_=speedup_vs(trail, asp, top)
        out(f"| {pq} | {tsp:+.2f}% |")
    if a.md: open(a.md,"w").write("\n".join(lines)+"\n"); print(f"[wrote {a.md}]")

if __name__=="__main__": main()
