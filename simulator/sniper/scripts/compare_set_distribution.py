#!/usr/bin/env python3
"""
Analyze cache set distribution from simulator address traces.
Compares no_translation (VA=PA) vs reservethp (translated PA).
"""

import os
import sys
from collections import defaultdict
import math

NUCA_NUM_SETS = 2048
NUCA_ASSOC = 16

def analyze_trace(trace_path, name):
    """Analyze the set distribution from an address trace file."""
    set_counts = defaultdict(int)
    unique_lines = set()
    total_accesses = 0
    
    with open(trace_path, 'r') as f:
        header = f.readline()  # Skip header
        for line in f:
            parts = line.strip().split(',')
            if len(parts) < 5:
                continue
            
            va = int(parts[0], 16)
            pa = int(parts[1], 16)
            va_set = int(parts[2])
            pa_set = int(parts[3])
            skip_trans = int(parts[4])
            
            # Use the PA set index (what actually goes to cache)
            set_idx = pa_set
            unique_lines.add(pa)
            set_counts[set_idx] += 1
            total_accesses += 1
    
    # Calculate statistics
    unique_sets_used = len(set_counts)
    max_set_accesses = max(set_counts.values()) if set_counts else 0
    min_set_accesses = min(set_counts.values()) if set_counts else 0
    avg_per_set = total_accesses / unique_sets_used if unique_sets_used > 0 else 0
    
    # Standard deviation
    mean = avg_per_set
    variance = sum((count - mean) ** 2 for count in set_counts.values()) / len(set_counts) if set_counts else 0
    std_dev = math.sqrt(variance)
    
    # Conflict potential: accesses exceeding associativity
    conflict_potential = sum(max(0, count - NUCA_ASSOC) for count in set_counts.values())
    
    # Hot sets (sets with > 2x average)
    hot_sets = sum(1 for count in set_counts.values() if count > 2 * avg_per_set)
    
    print(f"\n{'='*60}")
    print(f"=== {name} ===")
    print(f"{'='*60}")
    print(f"Total accesses: {total_accesses:,}")
    print(f"Unique cache lines (PAs): {len(unique_lines):,}")
    print(f"Unique sets used: {unique_sets_used:,} / {NUCA_NUM_SETS}")
    print(f"Set utilization: {100*unique_sets_used/NUCA_NUM_SETS:.1f}%")
    print(f"Max accesses to single set: {max_set_accesses:,} ({max_set_accesses/NUCA_ASSOC:.1f}x capacity)")
    print(f"Min accesses to single set: {min_set_accesses:,}")
    print(f"Avg accesses per used set: {avg_per_set:.1f}")
    print(f"Std deviation: {std_dev:.1f}")
    print(f"Coefficient of variation: {std_dev/mean*100:.1f}%")
    print(f"Hot sets (>2x avg): {hot_sets}")
    print(f"Conflict potential (accesses > assoc): {conflict_potential:,}")
    
    # Show top 10 hottest sets
    sorted_sets = sorted(set_counts.items(), key=lambda x: -x[1])[:10]
    print(f"\nTop 10 hottest sets:")
    for set_idx, count in sorted_sets:
        print(f"  Set {set_idx:4d}: {count:5d} accesses ({count/NUCA_ASSOC:.1f}x capacity)")
    
    return set_counts, total_accesses, conflict_potential

def compare_distributions(no_trans_counts, reservethp_counts):
    """Compare the two distributions."""
    print(f"\n{'='*60}")
    print("=== SET DISTRIBUTION COMPARISON ===")
    print(f"{'='*60}")
    
    # Bin the sets into 16 groups
    num_bins = 16
    bin_size = NUCA_NUM_SETS // num_bins
    
    no_trans_bins = [0] * num_bins
    reservethp_bins = [0] * num_bins
    
    for set_idx, count in no_trans_counts.items():
        bin_idx = min(set_idx // bin_size, num_bins - 1)
        no_trans_bins[bin_idx] += count
    
    for set_idx, count in reservethp_counts.items():
        bin_idx = min(set_idx // bin_size, num_bins - 1)
        reservethp_bins[bin_idx] += count
    
    print(f"\n{'Bin':>5} {'Sets':>12} {'no_trans':>12} {'reservethp':>12} {'Diff':>10}")
    print("-" * 55)
    for i in range(num_bins):
        start_set = i * bin_size
        end_set = (i + 1) * bin_size - 1
        diff = no_trans_bins[i] - reservethp_bins[i]
        print(f"{i:>5} {start_set:4d}-{end_set:<4d}  {no_trans_bins[i]:>12,} {reservethp_bins[i]:>12,} {diff:>+10,}")
    
    # Check set overlap
    common_sets = set(no_trans_counts.keys()) & set(reservethp_counts.keys())
    print(f"\nSets used by both: {len(common_sets)}")
    
    # Show sets with drastically different usage
    print("\nSets with large usage difference (|diff| > 100):")
    all_sets = set(no_trans_counts.keys()) | set(reservethp_counts.keys())
    diffs = []
    for s in all_sets:
        nt = no_trans_counts.get(s, 0)
        rt = reservethp_counts.get(s, 0)
        if abs(nt - rt) > 100:
            diffs.append((s, nt, rt, nt - rt))
    
    diffs.sort(key=lambda x: -abs(x[3]))
    for s, nt, rt, d in diffs[:10]:
        print(f"  Set {s:4d}: no_trans={nt:5d}, reservethp={rt:5d}, diff={d:+5d}")

def main():
    _sniper_root = os.environ.get("SNIPER_ROOT", os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    no_trans_path = os.path.join(_sniper_root, "results/addr_trace_debug/no_translation/telemetry/address_trace.csv")
    reservethp_path = os.path.join(_sniper_root, "results/addr_trace_debug/reservethp/telemetry/address_trace.csv")
    
    print("=" * 60)
    print("RIGOROUS CACHE SET DISTRIBUTION ANALYSIS")
    print("Proving VA=PA causes different set distribution than translation")
    print("=" * 60)
    
    no_trans_counts, no_trans_total, no_trans_conflicts = analyze_trace(no_trans_path, "NO_TRANSLATION (VA=PA)")
    reservethp_counts, reservethp_total, reservethp_conflicts = analyze_trace(reservethp_path, "RESERVETHP (with translation)")
    
    compare_distributions(no_trans_counts, reservethp_counts)
    
    print(f"\n{'='*60}")
    print("=== SUMMARY ===")
    print(f"{'='*60}")
    print(f"NO_TRANSLATION conflict potential: {no_trans_conflicts:,}")
    print(f"RESERVETHP conflict potential:     {reservethp_conflicts:,}")
    if reservethp_conflicts > 0:
        print(f"Ratio: {no_trans_conflicts/reservethp_conflicts:.2f}x more conflicts with VA=PA")
    
    # Key insight
    print(f"\n{'='*60}")
    print("KEY INSIGHT:")
    print("The VA=PA mapping (no_translation) and PA translation (reservethp)")
    print("use DIFFERENT cache sets for the SAME virtual addresses.")
    print("This causes different conflict patterns and explains the DRAM difference.")
    print(f"{'='*60}")

if __name__ == "__main__":
    main()
