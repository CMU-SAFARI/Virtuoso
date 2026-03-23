# Part 2: Accelerating Page Faults in Hardware with Virtuoso

**ASPLOS 2026 Workshop: Hardware/OS Co-Design for Memory Management with Virtuoso**

## Overview

In Part 1 you measured the cost of OS-handled minor page faults: 2--5 us per
fault, thousands of instructions, and multiple LLC misses.  In this exercise
you will use the **Virtuoso** simulation framework to prototype a *hardware
page fault handler* that resolves simple faults without trapping into the OS
kernel.

Virtuoso extends the Sniper multi-core simulator with a full MMU model,
including TLB hierarchies, page table walkers, page tables, and physical
memory allocators.  The `hw_fault` MMU design adds a **HWFaultHandler** --
a hardware-managed pool of pre-allocated physical pages -- that can satisfy
demand-paging faults in tens of nanoseconds instead of microseconds.

## Architecture

```
Traditional Page Fault Flow:
  TLB Miss -> PTW -> Not Present -> #PF Trap -> Kernel -> Allocate -> Return

HW Fault Handler Flow:
  TLB Miss -> PTW -> Not Present -> HW Fault Handler -> Allocate from Pool -> Continue
                                    (NO TRAP if pool has pages!)
```

The key hardware components are:

| Component | Description |
|---|---|
| **Tag Array** | Tracks which pages in the delegated pool are allocated vs free |
| **Delegated Pool** | A contiguous region of physical memory reserved at boot |
| **Translation Logic** | On a page fault, checks the tag array; if a free page exists, installs the PTE and continues without trapping |
| **Fallback Path** | If the pool is exhausted, falls back to the normal OS exception handler |

## Prerequisites

* Virtuoso built and `VIRTUOSO_ROOT` environment variable set
* A SIFT trace (the demo uses `rnd.sift` from the traces directory)
* Python 3.8+ with `matplotlib` and `numpy`

## Directory Contents

```
part2_hw_faults/
  README.md       # This file
  run_demo.sh     # Automated script: runs baseline + hw_faults, prints comparison
  compare.py      # Post-processing: reads sim.stats, generates comparison plots
```

## Step 1 -- Run the Baseline Simulation

The baseline uses the standard MMU (`mmu_base`) with the ReserveTHP allocator
and zero fragmentation (all memory is 4 KiB pages).

```bash
cd demos/part2_hw_faults
bash run_demo.sh
```

Or manually:

```bash
SNIPER_ROOT="${VIRTUOSO_ROOT}/simulator/sniper"
"${SNIPER_ROOT}/run-sniper" \
    -c "${SNIPER_ROOT}/config/address_translation_schemes/reservethp.cfg" \
    -d results_baseline \
    -n 1 \
    -s stop-by-icount:2000000 \
    -g perf_model/reserve_thp/target_fragmentation=0.0 \
    --traces="${VIRTUOSO_TRACES}/rnd.sift"
```

## Step 2 -- Run the HW Faults Simulation

The HW faults variant uses `mmu_hwfault` -- the same TLB/PTW hierarchy but
with a hardware fault handler that intercepts page faults before they reach
the OS.

```bash
"${SNIPER_ROOT}/run-sniper" \
    -c "${SNIPER_ROOT}/config/address_translation_schemes/reservethp.cfg" \
    -d results_hwfaults \
    -n 1 \
    -s stop-by-icount:2000000 \
    -g perf_model/mmu/type=hw_fault \
    -g perf_model/mmu/count_page_fault_latency=true \
    -g perf_model/reserve_thp/target_fragmentation=0.0 \
    --traces="${VIRTUOSO_TRACES}/rnd.sift"
```

The key difference is `-g perf_model/mmu/type=hw_fault`, which swaps in the
`MemoryManagementUnitHWFault` class.

## Step 3 -- Compare Results

The `run_demo.sh` script prints a side-by-side comparison table.  You can
also run the comparison script manually:

```bash
python3 compare.py results_baseline results_hwfaults --out-dir plots/
```

Metrics to compare:

| Metric | Baseline (mmu_base) | HW Faults (mmu_hw_fault) |
|---|---|---|
| IPC | lower | higher |
| Page faults | N | N (same workload) |
| Avg fault latency | higher (OS trap) | lower (HW pool) |
| Total translation time | higher | lower |

## Step 4 -- Walk Through the Code

Open the HW fault handler source code:

```
simulator/sniper/common/core/memory_subsystem/
  parametric_dram_directory_msi/mmu_designs/mmu_hw_faults.h
  parametric_dram_directory_msi/mmu_designs/mmu_hw_faults.cc
```

Key classes and methods:

### `HWFaultHandler` (mmu_hw_faults.h, lines 21--73)

* **`tag_array`** -- An array of `IntPtr` values, one per page in the
  delegated pool.  `-1` means the slot is free.
* **`translateAddress(addr)`** -- Checks if the page is already mapped in the
  pool (tag match).
* **`handleFault(addr)`** -- If `translateAddress` returns miss and the pool
  has a free slot, installs the mapping and returns the physical page number.

### `MemoryManagementUnitHWFault` (mmu_hw_faults.cc)

* **`performAddressTranslation()`** -- The main entry point.  After a TLB
  miss triggers a PTW and the walk discovers a not-present PTE, the code
  calls `hw_fault_handler.handleFault()`.  If the pool satisfies the fault,
  no OS exception handler latency is charged.
* **`instantiateHWFaultHandler()`** -- Reads the delegated memory range from
  the config and creates the `HWFaultHandler` object.

### MMU Factory (mmu_factory.h)

The factory recognises `type == "hw_fault"` and instantiates
`MemoryManagementUnitHWFault`.

## Step 5 -- Discussion: Design Tradeoffs

1. **Pool size**: A larger pool handles more faults in hardware but wastes
   physical memory that the OS cannot reclaim.  What is a good pool size for
   a 4 GiB system?

2. **Pool refill**: When the pool runs out, all faults fall back to the OS.
   A production design would need a background refill mechanism (e.g., the OS
   replenishes the pool during idle periods).

3. **Security**: The HW pool bypasses OS access-control checks.  How would
   you ensure that the hardware only maps pages the process is allowed to
   access?

4. **Multi-process**: Each process needs its own pool (or the hardware must
   tag entries with an ASID).  How does this affect area/complexity?

## Key Takeaways

* **Hardware can handle simple page faults without an OS trap**, avoiding
  the 2--5 us overhead measured in Part 1.
* The **delegated memory pool** pattern is simple to implement in hardware:
  a tag array plus a free-page pointer.
* Virtuoso makes it straightforward to prototype and evaluate such designs:
  the entire change is a new MMU class (~200 lines of C++) plugged into the
  existing TLB + PTW infrastructure.
* The tradeoff is between **pool size** (memory overhead) and
  **fault coverage** (fraction of faults handled in hardware).
