#!/usr/bin/env python3
"""
Revelator 4-core head-to-head parser.

Uses the EQUAL-WORK per-core heartbeat-crossing method for per-core IPC. Do NOT
use sim.stats global cycle_count: it overshoots by however far the last core ran
past the stop point, which systematically flatters whichever config finishes its
cores at the most uneven times.

Each run dir has a heartbeat.txt with periodic lines:
  [PERCORE-ICOUNT] agg=.. global_time=..ns [c0=.., c1=.., ...]
  [PERCORE-CLOCK] [c0=..ns, c1=..ns, ...]
We interpolate each core's own clock at its crossing of the target instruction
count and take IPC_i = target / clk_i(target); frequency cancels in ratios.

    parse_multicore.py --results-dir DIR [--target auto|N] [--md OUT.md]

--target auto (the default) picks the largest instruction count every run in the
suite actually reached, so a short `--icount` smoke run still parses.

DIR should contain the <config>_4core_<mix> run subdirectories.
"""
import os, re, sys, math, argparse
from collections import defaultdict

# (display name, config name in clist_multicore.yaml)
SCHEMES = [("ReserveTHP (baseline)", "fd-4c-frag1.0"),
           ("Revelator 4KB",         "rev4kb-4c")]
BASE = "fd-4c-frag1.0"

IC = re.compile(r"\[PERCORE-ICOUNT\].*?\[([^\]]+)\]")
CK = re.compile(r"\[PERCORE-CLOCK\]\s*\[([^\]]+)\]")
RUNDIR = re.compile(r"^(.+?)_(\d+)core_(.+)$")

def vals(s):
    return [float(x.split("=")[1].replace("ns", "")) for x in s.split(",")]

def read_heartbeat(path):
    """-> (icounts, clocks) as parallel lists of per-core samples."""
    icounts, clocks = [], []
    with open(path) as f:
        for line in f:
            m = IC.search(line)
            if m:
                icounts.append(vals(m.group(1))); continue
            m = CK.search(line)
            if m:
                clocks.append(vals(m.group(1)))
    n = min(len(icounts), len(clocks))
    return icounts[:n], clocks[:n]

def max_reachable(icounts):
    """Largest instruction count EVERY core in this run reached."""
    if not icounts:
        return 0
    return min(max(s[c] for s in icounts) for c in range(len(icounts[0])))

def equalwork_ipc(icounts, clocks, target):
    if len(icounts) < 2:
        return None
    ncore = len(icounts[0]); ipc = []
    for c in range(ncore):
        prev = None; hit = None
        for k in range(len(icounts)):
            ic, ck = icounts[k][c], clocks[k][c]
            if ic >= target:
                hit = (prev, (ic, ck)); break
            prev = (ic, ck)
        if hit is None:
            return None
        pre, (ic1, ck1) = hit
        if pre is None:
            clk = ck1 * (target / ic1) if ic1 > 0 else None
        else:
            ic0, ck0 = pre
            frac = (target - ic0) / (ic1 - ic0) if ic1 > ic0 else 0
            clk = ck0 + frac * (ck1 - ck0)
        if not clk or clk <= 0:
            return None
        ipc.append(target / clk)
    return ipc

def gm(xs):
    return math.exp(sum(math.log(x) for x in xs) / len(xs)) if xs else float('nan')

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results-dir", required=True)
    ap.add_argument("--target", default="auto", help="instructions/core, or 'auto'")
    ap.add_argument("--md", default=None)
    ap.add_argument("--top200", default=None, help=argparse.SUPPRESS)
    a = ap.parse_args()

    known = {cfg for _, cfg in SCHEMES}
    raw = {}          # (mix, cfg) -> (icounts, clocks)
    skipped = 0
    for root, dirs, _ in os.walk(a.results_dir):
        for d in dirs:
            m = RUNDIR.match(d)
            if not m or m.group(1) not in known:
                continue
            hb = os.path.join(root, d, "heartbeat.txt")
            if not os.path.exists(hb):
                skipped += 1; continue
            ic, ck = read_heartbeat(hb)
            if len(ic) < 2:
                skipped += 1; continue
            raw[(m.group(3), m.group(1))] = (ic, ck)

    if not raw:
        print(f"ERROR: no parseable <config>_4core_<mix>/heartbeat.txt under {a.results_dir}")
        raise SystemExit(1)

    # equal work across EVERY run, so no config is credited for running longer
    if a.target == "auto":
        target = min(max_reachable(ic) for ic, _ in raw.values())
        if target <= 0:
            print("ERROR: no run has a complete per-core heartbeat.", file=sys.stderr)
            raise SystemExit(1)
    else:
        target = int(a.target)

    data = defaultdict(dict)
    for (mix, cfg), (ic, ck) in raw.items():
        v = equalwork_ipc(ic, ck, target)
        if v is None:
            skipped += 1; continue
        data[mix][cfg] = v
    mixes = [m for m in data if BASE in data[m] and len(data[m]) >= 2]

    percore = defaultdict(list); agg = defaultdict(list); hm = defaultdict(list)
    for mix in mixes:
        bl = data[mix][BASE]; N = len(bl)
        for _, cfg in SCHEMES:
            ip = data[mix].get(cfg)
            if not ip or len(ip) != N:
                continue
            percore[cfg].append(sum(ip[i] / bl[i] for i in range(N)) / N)
            agg[cfg].append(sum(ip) / sum(bl))
            hm[cfg].append((N / sum(1 / x for x in ip)) / (N / sum(1 / x for x in bl)))

    lines = []
    out = lambda s: (lines.append(s), print(s))
    out(f"# Revelator 4-core head-to-head — {len(mixes)} mixes "
        f"(unparsed runs={skipped}), equal work @ {target/1e6:.0f}M instr/core")
    out("")
    out("| scheme | STP | aggIPC | HM-IPC | mixes > baseline |")
    out("|--|--:|--:|--:|--:|")
    M = len(mixes)
    for name, cfg in SCHEMES:
        if not agg[cfg]:
            out(f"| {name} | n/a | n/a | n/a | 0/{M} |"); continue
        npos = sum(1 for x in agg[cfg] if x > 1.0)
        out(f"| {name} | {(gm(percore[cfg])-1)*100:+.2f}% | {(gm(agg[cfg])-1)*100:+.2f}% "
            f"| {(gm(hm[cfg])-1)*100:+.2f}% | {npos}/{M} |")

    if agg["rev4kb-4c"]:
        s = sorted(agg["rev4kb-4c"])
        out("")
        out(f"Revelator aggIPC speedup: min={(s[0]-1)*100:+.2f}%  "
            f"median={(s[len(s)//2]-1)*100:+.2f}%  max={(s[-1]-1)*100:+.2f}%")
    if a.target == "auto":
        out("")
        out(f"Target chosen automatically as the largest instruction count every run reached "
            f"({target/1e6:.1f}M/core). Pass --target 50000000 to pin it.")

    if a.md:
        open(a.md, "w").write("\n".join(lines) + "\n")
        print(f"\n[wrote {a.md}]")

if __name__ == "__main__":
    main()
