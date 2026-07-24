#!/usr/bin/env python3
"""analyze_dump.py — per-workload PTW-dump analysis for the motivation figures.

Reads ONE page-table-walk dump (ptw_dump CSV, optionally .gz) and, in a single
pass, computes the per-workload metrics behind motivation Figures 1-4:

  Fig 1  top-k successor coverage        cov_global[k], cov_pc[k]   (k=1..8)
  Fig 2  successor-region granularity    cov4_s{shift}, cov8_s{shift}, ntrans_s{shift}
  Fig 3  unique (PC, delta) pairs         n_unique_pc_delta, n_unique_pc, n_unique_regions, ...
  Fig 4  representable delta occurrences  global_wt[b], pc_wt[b]     (b=4,6,..,20)

Writes a single JSON file with all of the above.

    analyze_dump.py --dump <ptw_dump.csv[.gz]> --workload <name> --out <name.json>
"""
import argparse, csv, gzip, io, json, sys
from collections import defaultdict, Counter

REGION_SHIFT = 3                       # 32 KB successor regions (8 x 4 KB pages)
KS = list(range(1, 9))                 # top-k
DST_SHIFTS = list(range(0, 13))        # 4 KB .. 16 MB destination granularity
BITS = list(range(4, 22, 2))           # delta bit-widths

def _open(path):
    return io.TextIOWrapper(gzip.open(path, "rb")) if path.endswith(".gz") else open(path, newline="")

def load(path):
    vpns, pcs = [], []
    with _open(path) as f:
        for row in csv.DictReader(f):
            try:
                vpns.append(int(row["PTW_Address"])); pcs.append(row["EIP"].strip())
            except (ValueError, KeyError):
                continue
    return vpns, pcs

def fits(delta, b):
    return -(1 << (b - 1)) <= delta <= (1 << (b - 1)) - 1

def analyze(vpns, pcs):
    regions = [v >> REGION_SHIFT for v in vpns]
    n = len(regions)
    out = {}
    if n < 3:
        return None

    # ---- Fig 1: global (stream-adjacent) top-k coverage --------------------
    g_tr = defaultdict(lambda: defaultdict(int))
    for i in range(n - 1):
        s, d = regions[i], regions[i + 1]
        if s != d: g_tr[s][d] += 1
    g_topk = {s: [set(sorted(dd, key=dd.get, reverse=True)[:k]) for k in KS] for s, dd in g_tr.items()}
    g_hit = [0] * len(KS); g_cnt = 0
    for i in range(n - 1):
        s, d = regions[i], regions[i + 1]
        if s == d: continue
        g_cnt += 1
        tk = g_topk.get(s)
        if tk:
            for j in range(len(KS)):
                if d in tk[j]: g_hit[j] += 1
    out["cov_global"] = [h / g_cnt if g_cnt else 0.0 for h in g_hit]

    # ---- Fig 1 + 3 + 4: PC-conditioned per-PC transitions ------------------
    pc_tr = defaultdict(lambda: defaultdict(int)); prev = {}
    for reg, pc in zip(regions, pcs):
        if pc in prev and prev[pc] != reg: pc_tr[(prev[pc], pc)][reg] += 1
        prev[pc] = reg
    pc_topk = {key: [set(sorted(dd, key=dd.get, reverse=True)[:k]) for k in KS] for key, dd in pc_tr.items()}
    pc_hit = [0] * len(KS); pc_cnt = 0; prev = {}
    for reg, pc in zip(regions, pcs):
        if pc in prev and prev[pc] != reg:
            pc_cnt += 1; tk = pc_topk.get((prev[pc], pc))
            if tk:
                for j in range(len(KS)):
                    if reg in tk[j]: pc_hit[j] += 1
        prev[pc] = reg
    out["cov_pc"] = [h / pc_cnt if pc_cnt else 0.0 for h in pc_hit]

    # ---- Fig 3: unique (PC, delta) pairs -----------------------------------
    pc_delta = Counter(); prev = {}
    for reg, pc in zip(regions, pcs):
        if pc in prev and prev[pc] != reg: pc_delta[(pc, reg - prev[pc])] += 1
        prev[pc] = reg
    out["n_unique_pc_delta"] = len(pc_delta)
    out["total_pc_trans"] = sum(pc_delta.values())
    out["max_pair_count"] = max(pc_delta.values()) if pc_delta else 0
    out["n_unique_pc"] = len({k[0] for k in pc_delta})
    out["n_unique_regions"] = len(set(regions))

    # ---- Fig 4: representable delta occurrences (freq-weighted) -------------
    g_delta = Counter()
    for i in range(n - 1):
        s, d = regions[i], regions[i + 1]
        if s != d: g_delta[d - s] += 1
    p_delta = Counter()
    for (_, delta), c in pc_delta.items():
        p_delta[delta] += c
    g_tot = sum(g_delta.values()); p_tot = sum(p_delta.values())
    out["n_transitions"] = g_tot; out["n_pc_transitions"] = p_tot
    out["global_wt"] = [sum(c for dl, c in g_delta.items() if fits(dl, b)) / g_tot if g_tot else 0.0 for b in BITS]
    out["pc_wt"] = [sum(c for dl, c in p_delta.items() if fits(dl, b)) / p_tot if p_tot else 0.0 for b in BITS]

    # ---- Fig 2: destination-granularity sweep (src fixed at 32 KB) ----------
    for shift in DST_SHIFTS:
        dst = [v >> shift for v in vpns]
        tr = defaultdict(lambda: defaultdict(int)); psrc = {}
        for sreg, dreg, pc in zip(regions, dst, pcs):
            if pc in psrc and psrc[pc] != sreg: tr[(psrc[pc], pc)][dreg] += 1
            psrc[pc] = sreg
        t4 = {k: set(sorted(dd, key=dd.get, reverse=True)[:4]) for k, dd in tr.items()}
        t8 = {k: set(sorted(dd, key=dd.get, reverse=True)[:8]) for k, dd in tr.items()}
        h4 = h8 = tot = 0; psrc = {}
        for sreg, dreg, pc in zip(regions, dst, pcs):
            if pc in psrc and psrc[pc] != sreg:
                tot += 1; key = (psrc[pc], pc)
                if key in t4 and dreg in t4[key]: h4 += 1
                if key in t8 and dreg in t8[key]: h8 += 1
            psrc[pc] = sreg
        out[f"cov4_s{shift}"] = h4 / tot if tot else 0.0
        out[f"cov8_s{shift}"] = h8 / tot if tot else 0.0
        out[f"ntrans_s{shift}"] = tot
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", required=True); ap.add_argument("--workload", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    vpns, pcs = load(a.dump)
    res = analyze(vpns, pcs)
    if res is None:
        print(f"skip {a.workload}: too few PTWs", file=sys.stderr); sys.exit(0)
    res["workload"] = a.workload
    json.dump(res, open(a.out, "w"))
    print(f"[ok] {a.workload}: {res['n_transitions']} transitions -> {a.out}")

if __name__ == "__main__":
    main()
