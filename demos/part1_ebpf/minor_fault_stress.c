// SPDX-License-Identifier: GPL-2.0
//
// minor_fault_stress.c - Synthetic workload that generates a continuous
// stream of minor anonymous page faults for benchmarking with the eBPF
// page fault instrumentation tool (instrument.bpf.c / instrument.c).
//
// How it works:
//   1. mmap() a large anonymous region (MAP_PRIVATE | MAP_ANONYMOUS).
//   2. Disable THP via madvise(MADV_NOHUGEPAGE) so all faults are 4 KiB.
//   3. Touch one byte per page (write) to trigger a minor fault on each page.
//      - On first access, the kernel allocates a zero-filled physical frame
//        and maps it into the process page table (minor fault, no disk I/O).
//   4. After touching all pages, call madvise(MADV_DONTNEED) on the region.
//      - This discards all page table entries and physical frames, so the
//        next pass will fault again on every page.
//   5. Repeat until Ctrl-C (SIGINT/SIGTERM).
//
// This creates an ideal workload for measuring page fault latency because:
//   - All faults are minor (zero-fill), so disk I/O is never involved.
//   - Faults are deterministic: exactly one fault per page per cycle.
//   - The fault rate is controllable via region size and sleep interval.
//
// Build:
//   gcc -O2 -Wall -Wextra -o minor_fault_stress minor_fault_stress.c
//
// Usage:
//   ./minor_fault_stress <bytes> [passes_per_cycle] [sleep_us]
//
// Arguments:
//   bytes            - Size of the anonymous region (e.g., 268435456 for 256 MiB)
//   passes_per_cycle - How many times to sweep the region before MADV_DONTNEED (default: 1)
//                      Only the first pass faults; subsequent passes hit cached PTEs.
//   sleep_us         - Microseconds to sleep between cycles (default: 0)
//
// Example:
//   ./minor_fault_stress $((256*1024*1024)) 1 0
//   # 256 MiB region, 1 pass per cycle, no sleep = ~65536 faults per cycle
//
// Tips:
//   - Disable THP system-wide for cleanest results:
//       echo never | sudo tee /sys/kernel/mm/transparent_hugepage/enabled
//   - Pin to a CPU for stable PMU counter reads:
//       taskset -c 0 ./minor_fault_stress $((256*1024*1024))


#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>


// Returns current monotonic time in nanoseconds (for cycle timing).
static inline uint64_t nsec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}


// Signal handler: set flag to break out of the main loop on Ctrl-C.
static volatile sig_atomic_t g_stop = 0;
static void on_sig(int sig) { (void)sig; g_stop = 1; }


// Print error message and exit. If err != 0, append strerror.
static void die(int err, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    if (err) fprintf(stderr, ": %s\n", strerror(err));
    exit(1);
}


int main(int argc, char **argv) {
    if (argc < 2) {
    fprintf(stderr, "Usage: %s <bytes> [passes_per_cycle] [sleep_us]\n", argv[0]);
    fprintf(stderr, " bytes can be decimal or C-style (e.g., $((256*1024*1024)))\n");
    return 1;
    }


    // Parse command-line arguments.
    size_t bytes   = strtoull(argv[1], NULL, 0);  // region size in bytes
    int    passes  = (argc > 2) ? atoi(argv[2]) : 1;  // sweeps before DONTNEED
    int    sleep_us = (argc > 3) ? atoi(argv[3]) : 0;  // inter-cycle sleep
    if (passes < 1) passes = 1;

    // Query system page size (typically 4096 on x86_64).
    long pgsz = sysconf(_SC_PAGESIZE);
    if (pgsz <= 0) die(errno, "sysconf(_SC_PAGESIZE) failed");

    // Round up to page boundary.
    size_t pages   = (bytes + (size_t)pgsz - 1) / (size_t)pgsz;
    size_t map_len = pages * (size_t)pgsz;

    // Allocate anonymous private region. No physical memory is committed yet;
    // the kernel will allocate frames on demand (one minor fault per page).
    char *p = mmap(NULL, map_len, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) die(errno, "mmap(%zu bytes) failed", map_len);

    // Disable transparent huge pages so every fault is exactly 4 KiB.
    // Without this, the kernel may coalesce adjacent pages into 2 MiB THPs,
    // reducing the number of observable faults.
    (void)madvise(p, map_len, MADV_NOHUGEPAGE);


    fprintf(stderr,
    "minor_fault_app: region=%zu MiB pages=%zu pgsz=%ld passes_per_cycle=%d sleep_us=%d\n",
    map_len >> 20, pages, pgsz, passes, sleep_us);
    fprintf(stderr, "Press Ctrl-C to stop. Each cycle ends with MADV_DONTNEED to re-fault next pass.\n");


    // Install signal handlers for graceful shutdown.
    signal(SIGINT,  on_sig);
    signal(SIGTERM, on_sig);

    volatile unsigned char sink = 0;  // prevent compiler from optimizing away reads
    unsigned long long cycles = 0;

    // === Main fault-generation loop ===
    // Each cycle:
    //   1. Touch every page (write one byte at offset 0 of each 4 KiB page).
    //      On the first touch after MADV_DONTNEED, this triggers a minor fault:
    //      the kernel calls handle_mm_fault() -> do_anonymous_page(), which
    //      allocates a zero-filled frame and installs a PTE.
    //   2. Call MADV_DONTNEED to discard all PTEs and frames, resetting the
    //      region so the next cycle faults again.
    while (!g_stop) {
        uint64_t t0 = nsec();

        for (int pass = 0; pass < passes && !g_stop; pass++) {
            for (size_t i = 0; i < pages; i++) {
                size_t off = i * (size_t)pgsz;
                // Write to the first byte of each page. If the PTE is absent
                // (after MADV_DONTNEED), this triggers a minor page fault.
                p[off] = (unsigned char)(pass + (int)i);
                sink ^= p[off];  // read back to prevent dead-store elimination
            }
        }

        uint64_t t1 = nsec();

        // Discard all pages in the region. The kernel unmaps PTEs and frees
        // physical frames. Next cycle’s touches will fault again.
        if (madvise(p, map_len, MADV_DONTNEED) != 0) {
            perror("madvise(DONTNEED)");
        }

        cycles++;
        double dt_ms = (double)(t1 - t0) / 1e6;
        fprintf(stderr,
                "cycle=%llu passes=%d pages=%zu touched=%zuKi time=%6.2f ms sink=%u\n",
                cycles, passes, pages, pages, dt_ms, (unsigned)sink);

        if (sleep_us > 0) usleep((useconds_t)sleep_us);
    }

    munmap(p, map_len);
    return 0;
}