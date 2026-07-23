#!/usr/bin/env python3
"""
Multicore prefetcher head-to-head parser (4-core, 100-mix study).

Uses the EQUAL-WORK, 50M-per-core-heartbeat-crossing method for per-core IPC.
Do NOT use sim.stats global cycle_count: it overshoots and inflates aggressive
prefetchers (TRAIL appeared +28% / beating Perfect-L2TLB; real ~+11%).

Each run dir has a heartbeat.txt with periodic lines:
  [PERCORE-ICOUNT] agg=.. global_time=..ns [c0=.., c1=.., ...]
  [PERCORE-CLOCK] [c0=..ns, c1=..ns, ...]
We interpolate each core's own clock at its first 50M-instruction crossing and
take IPC_i = 50e6 / clk_i(50M) (frequency cancels in ratios vs no-prefetch).

Usage:
  parse_multicore.py --results-dir DIR [--target 50000000] [--md OUT.md]
DIR should contain the mc-v4-<scheme>_4core_<mix> run subdirectories.
"""
import os, re, math, argparse
from collections import defaultdict

SCHEMES = ["nopf","asp","asp-dp","asp-recency","asp-atp","asp-berti",
           "asp-l2tlb32k","asp-l2tlb64k","trail","perfect"]
LABEL = {"nopf":"no-pf","asp":"ASP","asp-dp":"ASP+DP","asp-recency":"ASP+Recency",
         "asp-atp":"ASP+ATP","asp-berti":"ASP+Berti","asp-l2tlb32k":"ASP+L2TLB32k",
         "asp-l2tlb64k":"ASP+L2TLB64k","trail":"TRAIL","perfect":"Perfect-L2TLB"}
IC = re.compile(r"\[PERCORE-ICOUNT\].*?\[([^\]]+)\]")
CK = re.compile(r"\[PERCORE-CLOCK\]\s*\[([^\]]+)\]")

def vals(s):
    return [float(x.split("=")[1].replace("ns","")) for x in s.split(",")]

def equalwork_ipc(hbpath, target):
    icounts=[]; clocks=[]
    with open(hbpath) as f:
        for line in f:
            m=IC.search(line)
            if m: icounts.append(vals(m.group(1))); continue
            m=CK.search(line)
            if m: clocks.append(vals(m.group(1)))
    n=min(len(icounts),len(clocks))
    if n<2: return None
    ncore=len(icounts[0]); ipc=[]
    for c in range(ncore):
        prev=None; hit=None
        for k in range(n):
            ic=icounts[k][c]; ck=clocks[k][c]
            if ic>=target: hit=(prev,(ic,ck)); break
            prev=(ic,ck)
        if hit is None: return None
        pre,(ic1,ck1)=hit
        if pre is None:
            clk=ck1*(target/ic1) if ic1>0 else None
        else:
            ic0,ck0=pre
            frac=(target-ic0)/(ic1-ic0) if ic1>ic0 else 0
            clk=ck0+frac*(ck1-ck0)
        if not clk or clk<=0: return None
        ipc.append(target/clk)
    return ipc

def gm(xs): return math.exp(sum(math.log(x) for x in xs)/len(xs)) if xs else float('nan')

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--results-dir", required=True,
                    help="dir containing mc-v4-<scheme>_4core_<mix> run subdirs")
    ap.add_argument("--target", type=int, default=50_000_000)
    ap.add_argument("--md", default=None, help="optional markdown output path")
    a=ap.parse_args()

    # Walk recursively: MC results may nest under results/<suite>/mc-v4-*_4core_<mix>.
    data=defaultdict(dict); skipped=0
    for root,dirs,_ in os.walk(a.results_dir):
        for d in dirs:
            m=re.match(r"mc-v4-(.+?)_4core_(.+)$", d)
            if not m: continue
            hb=os.path.join(root, d, "heartbeat.txt")
            if not os.path.exists(hb): skipped+=1; continue
            ipc=equalwork_ipc(hb, a.target)
            if ipc is None: skipped+=1; continue
            data[m.group(2)][m.group(1)]=ipc
    mixes=[m for m in data if "nopf" in data[m] and len(data[m])>=8]

    percore=defaultdict(list); agg=defaultdict(list); hm=defaultdict(list); won=defaultdict(int)
    for mix in mixes:
        bl=data[mix]["nopf"]; N=len(bl); best=None; bv=0
        for s in SCHEMES:
            if s not in data[mix] or len(data[mix][s])!=N: continue
            ip=data[mix][s]
            percore[s].append(sum(ip[i]/bl[i] for i in range(N))/N)
            av=sum(ip)/sum(bl); agg[s].append(av)
            hm[s].append((N/sum(1/x for x in ip))/(N/sum(1/x for x in bl)))
            if s!="nopf" and av>bv: bv=av; best=s
        won[best]+=1

    lines=[]
    def out(s): lines.append(s); print(s)
    out(f"Multicore 4-core head-to-head — {len(mixes)} mixes (skipped runs={skipped}), "
        f"equal-work @ {a.target//1_000_000}M/core")
    out("")
    out(f"| {'scheme':<14} | {'STP':>8} | {'aggIPC':>8} | {'HM-IPC':>8} | {'>nopf':>7} | {'won':>4} |")
    out(f"|{'-'*16}|{'-'*10}|{'-'*10}|{'-'*10}|{'-'*9}|{'-'*6}|")
    M=len(mixes)
    for s in SCHEMES:
        if not agg[s]: continue
        npos=sum(1 for x in agg[s] if x>1.0)
        out(f"| {LABEL[s]:<14} | {(gm(percore[s])-1)*100:>+7.2f}% | {(gm(agg[s])-1)*100:>+7.2f}% "
            f"| {(gm(hm[s])-1)*100:>+7.2f}% | {npos:>4}/{M:<2} | {won.get(s,0):>4} |")
    tp=sum(1 for mix in mixes if "trail" in data[mix] and "perfect" in data[mix]
           and sum(data[mix]["trail"])>sum(data[mix]["perfect"]))
    out("")
    out(f"TRAIL beats Perfect-L2TLB (aggIPC) in {tp}/{len(mixes)} mixes.")
    if agg["trail"]:
        ts=sorted(agg["trail"])
        out(f"TRAIL aggIPC speedup: min={(ts[0]-1)*100:+.2f}%  "
            f"median={(ts[len(ts)//2]-1)*100:+.2f}%  max={(ts[-1]-1)*100:+.2f}%")
    if a.md:
        open(a.md,"w").write("\n".join(lines)+"\n")
        print(f"\n[wrote {a.md}]")

if __name__=="__main__":
    main()
