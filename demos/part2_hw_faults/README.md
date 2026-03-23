# Part 2: Accelerating Page Faults in Hardware with Virtuoso

**ASPLOS 2026 Workshop: Hardware/OS Co-Design for Memory Management with Virtuoso**

## Overview

In Part 1 you measured the cost of OS-handled minor page faults on a real
system: 2-5 us per fault, thousands of instructions.  In this exercise you
will use the **Virtuoso** simulation framework to prototype a *hardware page
fault handler* that resolves faults without the full OS trap overhead.

## Background: Why are page faults expensive?

A traditional page fault goes through this path:

```
  TLB Miss -> PTW -> PTE not present -> #PF exception
    -> save registers, switch to kernel stack      
    -> VMA lookup (maple tree / red-black tree)       
    -> allocate physical page (buddy allocator)      
    -> zero the page (4096 bytes memset)              
    -> install PTE, flush TLB                         
    -> restore registers, return to user             
                                            Total: ~2000-5000 ns
```

The **hardware page fault handler** shortens this by pre-allocating a pool
of physical pages and installing PTEs directly in hardware:

```
  TLB Miss -> PTW -> PTE not present -> HW Fault Handler
    -> check tag array for free page                  (~10 ns)
    -> allocate from pool, install PTE                (~40 ns)
    -> continue (NO TRAP!)                   Total:   ~50 ns
```

The OS still allocates the page (it must maintain its data structures), but
the latency charged to the application is just the tag array lookup time.

## How it works in Virtuoso

The `mmu_hw_fault` MMU design extends the baseline MMU with a **delegated
memory pool**:

| Component | Description |
|---|---|
| **Tag Array** | Tracks which virtual pages are mapped in the pool (40K entries) |
| **Delegated Pool** | 160 MB of physical pages reserved for HW allocation |
| **Fault Logic** | On page fault: if pool has a free slot, charge 50ns; otherwise fall back to OS trap |
| **Fallback** | When the pool is full, faults go through the normal OS exception handler |

Both the baseline and HW faults MMU:
- Perform identical TLB lookups and page table walks
- Always call the OS exception handler to allocate pages and install PTEs
- Always retry the PTW after fault handling to find the now-present page
- The **only difference** is the fault latency charged: 50ns (HW pool) vs 2000 cycles (OS trap)

## Prerequisites

- Virtuoso built (`cd simulator/sniper && make -j4`)
- A SIFT trace (the demo uses `rnd.sift`)
- Python 3.8+ with `matplotlib` and `numpy`

## Directory Contents

```
part2_hw_faults/
  README.md       This file
  run_demo.sh     Runs baseline + hw_faults, prints comparison table
  compare.py      Parses results, generates 4 comparison plots
```

## Step 1: Run the demo

```bash
cd demos/part2_hw_faults
bash run_demo.sh /path/to/traces/rnd.sift
```

This runs two simulations (2M instructions each):
1. **Baseline**: `mmu_base` with `page_fault_latency=2000` cycles
2. **HW Faults**: `mmu_hw_fault` with 160MB pool, 50ns HW fault latency

Both charge the same OS fault latency (2000 cycles) for faults the HW pool
cannot handle, ensuring a fair comparison.

You can also specify a custom instruction count:

```bash
bash run_demo.sh /path/to/traces/rnd.sift 5000000
```

## Step 2: View results

The script prints a comparison table. For plots:

```bash
python3 compare.py results_baseline results_hwfaults --out-dir plots/
```

This generates:
1. **`ipc_comparison.png`** -- IPC bar chart
2. **`fault_latency.png`** -- Stacked bar: HW fault latency vs OS fault latency
3. **`translation_breakdown.png`** -- Stacked bar: TLB + PTW + Fault breakdown
4. **`fault_coverage.png`** -- Pie chart: fraction of faults handled by HW pool

## Expected results

With the `rnd` trace (random access pattern, 37K page faults in 2M instructions):

| Metric | Baseline | HW Faults | Delta |
|---|---|---|---|
| IPC | 0.07 | 0.09 | +29% |
| Page faults | 37,032 | 37,032 | same |
| HW-handled faults | - | 26,517 (72%) | - |
| OS-handled faults | 37,032 | 10,515 (28%) | - |
| Total fault latency | 14.5T | 5.4T | -62% |

The 160MB pool covers 72% of the unique pages, reducing total fault latency
by 62% and improving IPC by 29%.

## Step 3: Walk through the code

The HW fault handler source:

```
simulator/sniper/common/core/memory_subsystem/
  parametric_dram_directory_msi/mmu_designs/mmu_hw_faults.h
  parametric_dram_directory_msi/mmu_designs/mmu_hw_faults.cc
```

### `HWFaultHandler` class (mmu_hw_faults.h)

- **`canHandleFault(addr)`** -- Checks if the pool has a free slot for
  this virtual page (tag array lookup, modular indexing)
- **`recordMapping(addr)`** -- Records that a page was mapped through the
  pool (updates the tag array)
- Pool size and tag array are allocated in the constructor
- Move semantics handle ownership of the raw `tag_array` pointer

### `performAddressTranslation()` (mmu_hw_faults.cc)

The fault handling loop (same structure as baseline `mmu.cc`):

```
do {
    ptw_result = performPTW(address, ...)     // Walk the page table
    if (page_fault) {
        hw_can_handle = hw_fault_handler.canHandleFault(address)
        exception_handler->handle_page_fault(...)  // ALWAYS install PTE
        if (hw_can_handle)
            charge hw_fault_latency (50ns)
        else
            charge os_fault_latency (2000 cycles)
    }
} while (page_fault)    // Retry PTW -- page is now present
```

The key design: the exception handler **always** runs (to maintain OS state),
but the latency charged depends on whether the HW pool could have handled it.

## Step 4: Discussion

1. **Pool size vs coverage**: With 40K entries (160MB), the pool covers 72%
   of faults. A larger pool covers more but wastes physical memory.

2. **Pool refill**: When full, all faults fall back to the OS. A production
   design needs background refill (OS replenishes during idle periods).

3. **Security**: The HW pool bypasses OS access-control checks. How would
   you ensure only authorized pages are allocated?

4. **Workload sensitivity**: The `rnd` trace has high fault rates. Try with
   different traces to see how coverage varies with access patterns.

## Key Takeaways

- Hardware can handle page faults **without the full OS trap overhead**,
  reducing per-fault latency from ~2000 cycles to ~50ns.
- The improvement depends on **pool coverage** -- what fraction of the
  working set fits in the delegated pool.
- Virtuoso makes it straightforward to prototype and evaluate such designs:
  the entire change is a new MMU class plugged into the existing infrastructure.
- Both baseline and HW faults perform **identical page table walks** and
  **identical exception handling** -- the only difference is the latency model.
