# Part 1: Measuring Memory Management Overheads with eBPF

**ASPLOS 2026 Workshop: Hardware/OS Co-Design for Memory Management with Virtuoso**

## Overview

In this hands-on exercise you will use eBPF to instrument the Linux kernel's
page fault handler (`handle_mm_fault`) on a live system.  The goal is to
quantify how much time the OS spends servicing minor page faults and to
understand where that time goes (TLB refill, page zeroing, LLC misses, etc.).

The numbers you collect here motivate **Part 2**, where we prototype a
hardware page fault handler that bypasses the OS entirely.

## Prerequisites

| Requirement | Minimum version |
|---|---|
| Linux kernel | 5.15+ (BTF enabled) |
| `libbpf-dev` | 0.6+ |
| `clang` | 14+ |
| `bpftool` | matching kernel version |
| Python 3 | 3.8+ (with `numpy`, `matplotlib`) |

Install on Ubuntu/Debian:

```bash
sudo apt install -y libbpf-dev clang llvm bpftool linux-tools-$(uname -r) \
    python3-numpy python3-matplotlib
```

## Directory Contents

```
part1_ebpf/
  instrument.bpf.c        # eBPF kernel-side program (kprobe on handle_mm_fault)
  instrument.c             # Userspace loader (ring buffer, perf counters, aggregation)
  vmlinux.h                # BTF-generated kernel type header
  minor_fault_stress.c     # Workload: mmap + touch + MADV_DONTNEED loop
  Makefile                 # Build rules
  analyze.py               # Post-processing: histograms, breakdown, summary stats
  parse_metrics.py         # CDF plotting helper (instructions & cycles)
  accumulate_fault_latency.py  # Accumulate total fault latency from trace
```

## Step 1 -- Build

```bash
cd demos/part1_ebpf
make
```

This compiles:
* `instrument.bpf.o` -- the eBPF bytecode (clang, BPF target)
* `instrument` -- the userspace loader (links libbpf, libelf, libz)
* `minor_fault_stress` -- the stress-test binary

## Step 2 -- Generate Page Faults

In **terminal 1**, launch the stress test.  It maps 256 MiB of anonymous
memory, touches every page (4 KiB stride), then calls `MADV_DONTNEED` so
the next pass triggers minor faults again.

```bash
# 256 MiB region, 1 pass per cycle, no sleep between cycles
./minor_fault_stress $((256*1024*1024)) 1 0
```

You should see output like:

```
minor_fault_app: region=256 MiB pages=65536 pgsz=4096 passes_per_cycle=1 sleep_us=0
cycle=1 passes=1 pages=65536 touched=65536Ki times= 45.12 ms sink=0
cycle=2 ...
```

Leave this running.

## Step 3 -- Trace with eBPF

In **terminal 2**, run the eBPF tracer as root.  The `--aggregate 1000` flag
prints a summary line every 1000 page fault events.

```bash
sudo ./instrument --aggregate 1000
```

Each per-event line shows:
* `latency` -- wall-clock time inside `handle_mm_fault` (nanoseconds)
* `llc` -- LLC (last-level cache) misses during the fault
* `insn` -- retired instructions during the fault
* `reason` -- fault classification (ANON, FILE, COW, SWAP, or UNKNOWN)
* `E_est` -- analytical energy estimate

The aggregation summary gives min/max/mean latency, mean LLC misses, and a
per-reason histogram.

Capture the output to a file for analysis:

```bash
sudo ./instrument --aggregate 1000 2>&1 | tee ebpf_trace.log
# Press Ctrl-C after 10-20 seconds
```

## Step 4 -- Analyze the Output

Run the analysis script on the captured trace:

```bash
python3 analyze.py ebpf_trace.log --out-dir plots/
```

This produces:
1. **Latency histogram** (`latency_histogram.png`) -- distribution of per-fault
   latencies with percentile markers.
2. **Fault reason breakdown** (`reason_breakdown.png`) -- pie/bar chart showing
   the fraction of ANON vs FILE vs COW vs SWAP faults.
3. **LLC misses per fault** (`llc_per_fault.png`) -- histogram of LLC misses
   observed during each fault.
4. **Summary statistics** printed to stdout (mean, median, p90, p99, total).

You can also use the existing helper scripts:

```bash
# CDF of fault latency in cycles and instructions
python3 parse_metrics.py ebpf_trace.log --out-prefix pgflt_cdf

# Total accumulated fault latency
python3 accumulate_fault_latency.py ebpf_trace.log
```

## Step 5 -- Discussion

### What do the numbers mean?

| Metric | Typical range (minor fault, 4 KiB) | Notes |
|---|---|---|
| Latency | 2--5 us | Kernel path: VMA lookup, PTE install, TLB flush |
| LLC misses | 0--3 | Zero-fill page may miss in LLC; PTE write also |
| Instructions | 2000--8000 | Kernel fault handler code path |

**Major faults** (page backed by disk/swap) are 10--1000x slower because they
involve I/O.  Our stress test generates only **minor anonymous faults**
(first-touch zero-fill), which are the cheapest kind -- and they *still* cost
several microseconds.

### Where does the time go?

1. **Trap overhead**: saving/restoring register state, switching to kernel mode.
2. **VMA lookup**: the kernel walks `mm->mmap` (or the maple tree) to find
   the faulting VMA.
3. **Page zeroing**: `clear_highpage()` zeroes 4096 bytes.
4. **PTE installation**: updating the page table entry and flushing the TLB.

### Why does this matter?

Τhe aggregate cost of page faults can consume
5--15% of total execution time.  This motivates **hardware-assisted page fault
handling** (Part 2), where simple faults are resolved without trapping into the
OS.

## Key Takeaways

* OS page fault handling has **non-trivial overhead** even for the simplest
  (minor anonymous) faults: 2--5 us per fault, 2000--8000 instructions.
* **LLC misses during PTW and page zeroing** dominate the latency.
* Millions of faults per second are common in emerging workloads, so the
  aggregate overhead is significant.
* These observations motivate **hardware acceleration** of page fault
  handling, which we explore in Part 2 using the Virtuoso simulation framework.
