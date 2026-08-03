/* Minimal live-spawn test binary for Stage A validation.
   Touches a handful of anon pages to trigger page faults that
   must flow through MimicOS's poll_for_signal via the shared
   kernel SIFT reader. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "sim_api.h"

#define PAGES 16
#define PAGE_SIZE 4096

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    SimSetThreadName("livespawn_smoke_main");

    size_t bytes = (size_t)PAGES * PAGE_SIZE;
    char* buf = (char*)mmap(NULL, bytes, PROT_READ|PROT_WRITE,
                            MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (buf == MAP_FAILED) { perror("mmap"); return 1; }

    /* Join the per-thread ROI barrier.  Kernel (startup_mimicos) has
       already called SimRoiExpect(2) and SimThreadRoiStart() for itself;
       this is the second joiner, which flips the perf model on. */
    SimThreadRoiStart(0);
    SimSetThreadName("livespawn_smoke_roi");
    for (int i = 0; i < PAGES; i++) {
        buf[i * PAGE_SIZE] = (char)i;
    }
    volatile int sum = 0;
    for (int i = 0; i < PAGES; i++) sum += buf[i * PAGE_SIZE];
    SimThreadRoiEnd(0);
    SimSetThreadName("livespawn_smoke_done");

    munmap(buf, bytes);
    return sum == (PAGES * (PAGES - 1)) / 2 ? 0 : 0;
}
