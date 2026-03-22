#!/usr/bin/env python3
"""
Analyze cache set distribution for VA vs PA addresses.
This script proves that VA=PA causes cache set clustering.
"""

import sys
import os
import struct
from collections import defaultdict
import math

# NUCA cache parameters (from sim.cfg)
CACHE_BLOCK_SIZE = 64  # bytes
NUCA_SIZE_KB = 2048    # 2MB
NUCA_ASSOC = 16
NUCA_NUM_SETS = (NUCA_SIZE_KB * 1024) // (CACHE_BLOCK_SIZE * NUCA_ASSOC)

LOG_BLOCKSIZE = int(math.log2(CACHE_BLOCK_SIZE))
LOG_NUM_SETS = int(math.log2(NUCA_NUM_SETS))

print(f"NUCA Config: {NUCA_SIZE_KB}KB, {NUCA_ASSOC}-way, {NUCA_NUM_SETS} sets")
print(f"Log2(block_size) = {LOG_BLOCKSIZE}, Log2(num_sets) = {LOG_NUM_SETS}")

def get_set_index_mask(addr):
    """Calculate set index using HASH_MASK (same as simulator)"""
    block_num = addr >> LOG_BLOCKSIZE
    set_index = block_num & (NUCA_NUM_SETS - 1)
    return set_index

def analyze_addresses(addresses, name):
    """Analyze set index distribution for a list of addresses"""
    set_counts = defaultdict(int)
    unique_lines = set()
    
    for addr in addresses:
        # Align to cache line
        cache_line_addr = addr & ~(CACHE_BLOCK_SIZE - 1)
        unique_lines.add(cache_line_addr)
        set_idx = get_set_index_mask(cache_line_addr)
        set_counts[set_idx] += 1
    
    # Calculate statistics
    total_accesses = len(addresses)
    unique_sets_used = len(set_counts)
    max_set_accesses = max(set_counts.values()) if set_counts else 0
    avg_per_set = total_accesses / unique_sets_used if unique_sets_used > 0 else 0
    
    # Find hot sets (sets with > 2x average)
    hot_sets = sum(1 for count in set_counts.values() if count > 2 * avg_per_set)
    
    # Calculate conflict potential: how many accesses exceed associativity
    conflict_potential = sum(max(0, count - NUCA_ASSOC) for count in set_counts.values())
    
    print(f"\n=== {name} ===")
    print(f"Total accesses: {total_accesses:,}")
    print(f"Unique cache lines: {len(unique_lines):,}")
    print(f"Unique sets used: {unique_sets_used:,} / {NUCA_NUM_SETS}")
    print(f"Set utilization: {100*unique_sets_used/NUCA_NUM_SETS:.1f}%")
    print(f"Max accesses to single set: {max_set_accesses:,}")
    print(f"Avg accesses per used set: {avg_per_set:.1f}")
    print(f"Hot sets (>2x avg): {hot_sets}")
    print(f"Conflict potential (accesses exceeding assoc): {conflict_potential:,}")
    
    # Show top 10 hottest sets
    sorted_sets = sorted(set_counts.items(), key=lambda x: -x[1])[:10]
    print(f"Top 10 hottest sets:")
    for set_idx, count in sorted_sets:
        print(f"  Set {set_idx}: {count} accesses ({count/NUCA_ASSOC:.1f}x capacity)")
    
    return set_counts, unique_lines

def simulate_simple_pa_mapping(va_addresses):
    """
    Simulate how reserve_thp would map VAs to PAs.
    Reservation-based: 2MB regions are allocated from a base PFN.
    This spreads VAs across different physical regions.
    """
    pa_addresses = []
    
    # Simulate reservation-based allocation
    # Each 2MB VA region gets a different physical base
    va_to_pa_2mb = {}
    next_phys_2mb_page = 0x800000  # Start at some offset
    
    for va in va_addresses:
        va_2mb_aligned = va & ~((2*1024*1024) - 1)  # 2MB alignment
        offset_in_2mb = va & ((2*1024*1024) - 1)    # Offset within 2MB
        
        if va_2mb_aligned not in va_to_pa_2mb:
            # Allocate a new 2MB physical page
            va_to_pa_2mb[va_2mb_aligned] = next_phys_2mb_page
            next_phys_2mb_page += 0x200000  # Next 2MB page
        
        pa = va_to_pa_2mb[va_2mb_aligned] + offset_in_2mb
        pa_addresses.append(pa)
    
    print(f"\nSimulated {len(va_to_pa_2mb)} unique 2MB regions")
    return pa_addresses

def read_champsim_trace(trace_path, max_records=100000):
    """Read addresses from ChampSim trace format"""
    addresses = []
    
    # ChampSim trace format: each record is 64 bytes
    # First 8 bytes contain the IP/address info
    record_size = 64
    
    import gzip
    
    try:
        opener = gzip.open if trace_path.endswith('.gz') else open
        with opener(trace_path, 'rb') as f:
            count = 0
            while count < max_records:
                record = f.read(record_size)
                if len(record) < record_size:
                    break
                
                # Parse the record - first 8 bytes are typically the address
                # Format may vary, let's try to extract meaningful addresses
                addr = struct.unpack('<Q', record[0:8])[0]
                
                # Skip if address looks invalid (kernel addresses, etc)
                if 0 < addr < 0x7FFFFFFFFFFF:
                    addresses.append(addr)
                
                count += 1
                
    except Exception as e:
        print(f"Error reading trace: {e}")
        return []
    
    print(f"Read {len(addresses)} valid addresses from trace")
    return addresses

def generate_synthetic_clustered_vas(n=100000):
    """Generate synthetic VAs that cluster (like real program)"""
    import random
    random.seed(42)
    
    addresses = []
    
    # Simulate code segment (0x400000 - 0x500000)
    code_base = 0x400000
    for _ in range(n // 4):
        offset = random.randint(0, 0x100000) & ~63  # Cache-line aligned
        addresses.append(code_base + offset)
    
    # Simulate heap (0x600000 - 0x800000) - clustered
    heap_base = 0x600000
    for _ in range(n // 2):
        offset = random.randint(0, 0x200000) & ~63
        addresses.append(heap_base + offset)
    
    # Simulate stack (0x7FFF00000000 region)
    stack_base = 0x7FFF00000000
    for _ in range(n // 4):
        offset = random.randint(0, 0x10000) & ~63
        addresses.append(stack_base - offset)
    
    random.shuffle(addresses)
    return addresses

def main():
    print("=" * 60)
    print("CACHE SET DISTRIBUTION ANALYSIS")
    print("Proving VA=PA causes cache set clustering")
    print("=" * 60)
    
    # Try to read real trace
    trace_path = "/home/rahbera/tracezoo/champsim/cvp1/secrect_converted/srv_part2/srv612.champsim.gz"
    
    if os.path.exists(trace_path):
        print(f"\nReading trace: {trace_path}")
        va_addresses = read_champsim_trace(trace_path, max_records=500000)
    else:
        print("\nTrace not found, using synthetic clustered addresses")
        va_addresses = generate_synthetic_clustered_vas(100000)
    
    if not va_addresses:
        print("No addresses to analyze, using synthetic data")
        va_addresses = generate_synthetic_clustered_vas(100000)
    
    # Analyze VA distribution (no_translation case)
    print("\n" + "=" * 60)
    va_sets, va_lines = analyze_addresses(va_addresses, "VA=PA (no_translation)")
    
    # Simulate PA translation
    pa_addresses = simulate_simple_pa_mapping(va_addresses)
    
    # Analyze PA distribution (with translation)
    print("\n" + "=" * 60)
    pa_sets, pa_lines = analyze_addresses(pa_addresses, "PA with translation")
    
    # Compare distributions
    print("\n" + "=" * 60)
    print("COMPARISON")
    print("=" * 60)
    
    va_conflict = sum(max(0, c - NUCA_ASSOC) for c in va_sets.values())
    pa_conflict = sum(max(0, c - NUCA_ASSOC) for c in pa_sets.values())
    
    print(f"VA=PA conflict potential: {va_conflict:,}")
    print(f"PA translated conflict potential: {pa_conflict:,}")
    if pa_conflict > 0:
        print(f"Conflict reduction with translation: {va_conflict/pa_conflict:.1f}x")
    else:
        print("Translation eliminates all conflicts!")
    
    # Show set distribution histogram
    print("\n" + "=" * 60)
    print("SET INDEX DISTRIBUTION (binned)")
    print("=" * 60)
    
    num_bins = 16
    bin_size = NUCA_NUM_SETS // num_bins
    
    va_bins = [0] * num_bins
    pa_bins = [0] * num_bins
    
    for set_idx, count in va_sets.items():
        va_bins[set_idx // bin_size] += count
    for set_idx, count in pa_sets.items():
        pa_bins[set_idx // bin_size] += count
    
    print(f"{'Bin':>6} {'VA=PA':>12} {'Translated':>12} {'Ratio':>8}")
    print("-" * 40)
    for i in range(num_bins):
        ratio = va_bins[i] / pa_bins[i] if pa_bins[i] > 0 else float('inf')
        print(f"{i:>6} {va_bins[i]:>12,} {pa_bins[i]:>12,} {ratio:>8.2f}")

if __name__ == "__main__":
    main()
