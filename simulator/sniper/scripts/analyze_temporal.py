#!/usr/bin/env python3
"""
Deep analysis of cache behavior differences.
"""

import os
from collections import defaultdict

def analyze_address_patterns(trace_path, name):
    """Analyze temporal access patterns."""
    
    # Track unique cache lines seen
    unique_lines = set()
    unique_lines_over_time = []
    
    # Track per-set history for conflict detection  
    set_history = defaultdict(list)  # set_idx -> list of cache lines
    
    conflicts_over_time = []
    total_conflicts = 0
    
    NUCA_ASSOC = 16
    SAMPLE_INTERVAL = 5000
    
    with open(trace_path, 'r') as f:
        header = f.readline()
        access_num = 0
        
        for line in f:
            parts = line.strip().split(',')
            if len(parts) < 5:
                continue
            
            pa = int(parts[1], 16)
            pa_set = int(parts[3])
            
            # Track unique lines
            cache_line = pa & ~63
            unique_lines.add(cache_line)
            
            # Track conflicts (simplified LRU model)
            set_hist = set_history[pa_set]
            if cache_line not in set_hist:
                # Miss - check if we'd evict something
                if len(set_hist) >= NUCA_ASSOC:
                    total_conflicts += 1
                set_hist.append(cache_line)
                if len(set_hist) > NUCA_ASSOC * 4:  # Keep bounded
                    set_hist.pop(0)
            else:
                # Hit - move to MRU
                set_hist.remove(cache_line)
                set_hist.append(cache_line)
            
            access_num += 1
            if access_num % SAMPLE_INTERVAL == 0:
                unique_lines_over_time.append(len(unique_lines))
                conflicts_over_time.append(total_conflicts)
    
    print(f"\n=== {name} ===")
    print(f"Total accesses: {access_num}")
    print(f"Final unique lines: {len(unique_lines)}")
    print(f"Total conflicts (simplified): {total_conflicts}")
    
    # Show growth of unique lines
    print("\nUnique cache lines over time:")
    for i, (ul, cf) in enumerate(zip(unique_lines_over_time[::4], conflicts_over_time[::4])):
        time_k = (i+1) * SAMPLE_INTERVAL * 4 // 1000
        print(f"  {time_k:3d}K accesses: {ul:6d} unique lines, {cf:6d} conflicts")
    
    return unique_lines, total_conflicts

def compare_unique_addresses():
    """Compare unique addresses between the two runs."""
    
    _sniper_root = os.environ.get("SNIPER_ROOT", os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    no_trans_path = os.path.join(_sniper_root, "results/addr_trace_debug/no_translation/telemetry/address_trace.csv")
    reservethp_path = os.path.join(_sniper_root, "results/addr_trace_debug/reservethp/telemetry/address_trace.csv")
    
    print("=" * 60)
    print("TEMPORAL CACHE BEHAVIOR ANALYSIS")
    print("=" * 60)
    
    # Read both traces
    no_trans_vas = []
    no_trans_pas = []
    reservethp_vas = []
    reservethp_pas = []
    
    with open(no_trans_path, 'r') as f:
        f.readline()  # header
        for line in f:
            parts = line.strip().split(',')
            if len(parts) >= 2:
                no_trans_vas.append(int(parts[0], 16))
                no_trans_pas.append(int(parts[1], 16))
    
    with open(reservethp_path, 'r') as f:
        f.readline()  # header
        for line in f:
            parts = line.strip().split(',')
            if len(parts) >= 2:
                reservethp_vas.append(int(parts[0], 16))
                reservethp_pas.append(int(parts[1], 16))
    
    # Verify VAs are the same
    print("\n=== VA Comparison ===")
    matching_vas = sum(1 for a, b in zip(no_trans_vas, reservethp_vas) if a == b)
    print(f"Matching VAs: {matching_vas} / {len(no_trans_vas)}")
    
    if matching_vas != len(no_trans_vas):
        print("WARNING: VAs differ between runs!")
        # Show first differences
        for i, (a, b) in enumerate(zip(no_trans_vas, reservethp_vas)):
            if a != b:
                print(f"  Difference at {i}: {hex(a)} vs {hex(b)}")
                if i > 5:
                    break
    
    # Compare PA distributions
    print("\n=== PA Comparison ===")
    no_trans_pa_set = set(pa & ~63 for pa in no_trans_pas)
    reservethp_pa_set = set(pa & ~63 for pa in reservethp_pas)
    
    print(f"NO_TRANSLATION unique PAs: {len(no_trans_pa_set)}")
    print(f"RESERVETHP unique PAs: {len(reservethp_pa_set)}")
    
    common_pas = no_trans_pa_set & reservethp_pa_set
    print(f"Common PAs: {len(common_pas)}")
    
    # Analyze runs
    no_trans_lines, no_trans_conf = analyze_address_patterns(no_trans_path, "NO_TRANSLATION")
    reservethp_lines, reservethp_conf = analyze_address_patterns(reservethp_path, "RESERVETHP")

if __name__ == "__main__":
    compare_unique_addresses()
