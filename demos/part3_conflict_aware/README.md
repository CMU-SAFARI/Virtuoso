# Part 3: Conflict-Aware Memory Allocation for Page Table Walks

**ASPLOS 2026 Workshop: Hardware/OS Co-Design for Memory Management with Virtuoso**

## Overview

When the OS allocates pages for the page table structure itself (the internal
nodes and leaf PTEs of the radix tree), those physical pages may end up in the
**same DRAM bank** as the data pages the application is actively accessing.
During a page table walk (PTW), the memory controller must issue **row
activations** to read each level of the table.  If a page-table page and a
recently-accessed data page share the same bank, the row buffer must be
swapped -- creating a **row buffer conflict** that adds 10--20 ns per level.

In this exercise you will implement a **conflict-aware allocator** that checks
the DRAM bank mapping before placing page-table pages and avoids banks that
are "hot" with recent data traffic.

## Problem Statement

Consider a simplified DRAM address mapping:

```
  Physical address bits:
    [... | bank (4 bits) | row | column | byte offset]

  bank = (ppn >> 6) & 0xF   (16 banks, 64-page rows)
```

With a standard allocator, the buddy system hands out page-table pages in
allocation order.  There is no awareness of which DRAM banks are under
pressure from data accesses.  The result: **avoidable row buffer conflicts**
during PTWs.

## Solution

A conflict-aware allocator wraps the existing ReserveTHP allocator and
overrides `handle_page_table_allocations()`.  Before accepting a page from
the buddy, it checks the DRAM bank:

1. Compute `bank = (candidate_ppn >> 6) & 0xF`.
2. Look up whether `bank` is in the "hot set" (banks with recent data traffic).
3. If hot, try the **next** free page from the buddy.
4. If no conflict-free page is found after a bounded number of attempts, fall
   back to the original allocation.

The hot-bank tracker is a simple shift-register / counter array updated on
every data allocation.

## Prerequisites

* Virtuoso built and `VIRTUOSO_ROOT` set
* A SIFT trace (default: `rnd.sift`)
* Familiarity with C++ templates and the Virtuoso allocator interface

## Directory Contents

```
part3_conflict_aware/
  README.md                      # This file
  conflict_aware_allocator.h     # Complete reference implementation
  conflict_aware_policy.h        # Sniper policy (stats + logging)
  skeleton.h                     # Skeleton with TODOs for attendees
  run_demo.sh                    # Runs baseline + conflict-aware, compares stats
```

## Step 1 -- Understand the Baseline

Run the baseline ReserveTHP allocator and look at DRAM statistics:

```bash
cd demos/part3_conflict_aware
bash run_demo.sh
```

The script prints DRAM row activations and walker cache-hierarchy statistics
from `sim.stats`.  Note the number of DRAM accesses during page table walks.

## Step 2 -- Implement the Conflict-Aware Allocator

Open `skeleton.h`.  It contains the allocator class with key methods stubbed
out.  Look for `// TODO` comments.  The three main pieces you need to fill in:

1. **`computeBank(ppn)`** -- Given a physical page number, return the DRAM
   bank index.
2. **`recordDataAccess(ppn)`** -- Update the hot-bank tracker when a data
   page is allocated.
3. **`handle_page_table_allocations(bytes)`** -- Override the base allocator.
   Request a page from the buddy, check its bank, retry if hot, and fall back
   after `MAX_RETRIES` attempts.

The reference implementation is in `conflict_aware_allocator.h` -- resist the
temptation to peek until you have tried it yourself.

## Step 3 -- Integrate and Run

To integrate your allocator into Virtuoso:

1. Copy your completed `skeleton.h` (or the reference `conflict_aware_allocator.h`)
   into the Virtuoso source tree:
   ```bash
   cp conflict_aware_allocator.h \
     ${VIRTUOSO_ROOT}/simulator/sniper/include/memory_management/physical_memory_allocators/
   ```

2. Add a new config that uses your allocator (or use the `-g` override to
   change the allocator type at runtime).

3. Rebuild Sniper and rerun:
   ```bash
   cd ${VIRTUOSO_ROOT}/simulator/sniper && make
   bash demos/part3_conflict_aware/run_demo.sh
   ```

## Step 4 -- Compare Results

The `run_demo.sh` script compares:

| Metric | Baseline | Conflict-Aware | Direction |
|---|---|---|---|
| DRAM row activations (PTW) | higher | lower | improvement |
| DRAM accesses (total) | same | same | neutral |
| Page table walks | same | same | neutral |
| IPC | baseline | slightly higher | improvement |

The conflict-aware allocator should reduce PTW-related DRAM row activations
because page-table pages land in banks that are not contending with data.

## Step 5 -- Discussion: Tradeoffs

1. **Memory utilization**: Skipping candidate pages wastes some physical
   memory capacity.  How much capacity is lost for 16 banks with 4 hot?

2. **Fragmentation interaction**: The buddy allocator already has
   fragmentation from THP demotion.  Does conflict avoidance make
   fragmentation worse?

3. **Hot-bank tracking cost**: In a real system, how would you track hot
   banks?  Performance counters?  A hardware bloom filter?

4. **Dynamic vs static mapping**: The DRAM bank mapping is
   microarchitecture-specific.  How portable is this approach?

5. **Multi-level page tables**: At which page-table levels does the conflict
   matter most?  (Hint: upper levels are often cached; leaf-level PTEs are
   the bottleneck.)

## Key Takeaways

* **DRAM bank conflicts between page-table and data pages** are a real source
  of translation overhead, especially for PTW-heavy workloads.
* A **simple conflict-aware allocator** (< 150 lines of C++) can reduce
  these conflicts with minimal memory overhead.
* Virtuoso's modular allocator interface makes it easy to prototype and
  evaluate new allocation policies without modifying the rest of the
  simulator.
* The design space involves trading **memory utilization** for **reduced
  contention** -- a classic OS/architecture co-design problem.
