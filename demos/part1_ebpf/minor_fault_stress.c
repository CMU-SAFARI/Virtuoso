// SPDX-License-Identifier: GPL-2.0
// minor_fault_app.c — continuously generate **minor anonymous page faults**
// Strategy: touch one byte per 4 KiB page, then MADV_DONTNEED the region
// so the next pass faults again (zero-fill, minor).
//
// Build: gcc -O2 -Wall -Wextra -o minor_fault_app minor_fault_app.c
// Run: ./minor_fault_app <bytes> [passes_per_cycle] [sleep_us]
// Example: ./minor_fault_app $((256*1024*1024)) 1 0
// # 256 MiB, one sweep per cycle, no sleep — steady stream of faults
//
// Tips for clean 4 KiB-fault behavior:
// * Disable THP for this process: we call madvise(MADV_NOHUGEPAGE).
// If THP is enabled system-wide, you might still see 2 MiB faults.
// * Use taskset/cpuset if you want a fixed CPU for PMU reads.


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


static inline uint64_t nsec(void) {
struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}


static volatile sig_atomic_t g_stop = 0;
static void on_sig(int sig){ (void)sig; g_stop = 1; }


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


    size_t bytes = strtoull(argv[1], NULL, 0);
    int passes = (argc > 2) ? atoi(argv[2]) : 1;
    int sleep_us = (argc > 3) ? atoi(argv[3]) : 0;
    if (passes < 1) passes = 1;


    long pgsz = sysconf(_SC_PAGESIZE);
    if (pgsz <= 0) die(errno, "sysconf(_SC_PAGESIZE) failed");


    size_t pages = (bytes + (size_t)pgsz - 1) / (size_t)pgsz;
    size_t map_len = pages * (size_t)pgsz;


    char *p = mmap(NULL, map_len, PROT_READ | PROT_WRITE,
    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) die(errno, "mmap(%zu bytes) failed", map_len);


    // Ensure we get 4 KiB pages (avoid THP): best-effort.
    (void)madvise(p, map_len, MADV_NOHUGEPAGE);


    fprintf(stderr,
    "minor_fault_app: region=%zu MiB pages=%zu pgsz=%ld passes_per_cycle=%d sleep_us=%d\n",
    map_len >> 20, pages, pgsz, passes, sleep_us);
    fprintf(stderr, "Press Ctrl-C to stop. Each cycle ends with MADV_DONTNEED to re-fault next pass.\n");


    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);


    volatile unsigned char sink = 0; // keep compiler honest
    unsigned long long cycles = 0;


    while (!g_stop) {
        uint64_t t0 = nsec();
        for (int pass = 0; pass < passes && !g_stop; pass++) {
        // Touch exactly one byte per page to trigger a minor fault on first touch
        // after MADV_DONTNEED.
            for (size_t i = 0; i < pages; i++) {
                size_t off = i * (size_t)pgsz;
                p[off] = (unsigned char)(pass + (int)i); // write → alloc + map
                sink ^= p[off];
            }
        }
        
        uint64_t t1 = nsec();


        // Drop all anonymous pages: next iteration will refault (minor, zero-fill).
        if (madvise(p, map_len, MADV_DONTNEED) != 0) {
            // If this fails, it’s usually due to weird mappings/limits; not fatal.
            perror("madvise(DONTNEED)");
        }


        cycles++;
        double dt_ms = (double)(t1 - t0) / 1e6;
        fprintf(stderr,
        "cycle=%llu passes=%d pages=%zu touched=%zuKi times=%6.2f ms sink=%u\n",
        cycles, passes, pages, pages, dt_ms, (unsigned)sink);


        if (sleep_us > 0) usleep((useconds_t)sleep_us);
    }


    munmap(p, map_len);
    return 0;
}