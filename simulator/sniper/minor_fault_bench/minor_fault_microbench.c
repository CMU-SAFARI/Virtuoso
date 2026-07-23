// Microbenchmark to tune ratio of minor page faults vs. work
// (memory accesses + computation), with command-line arguments.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <time.h>
#include <stdint.h>

#define PAGE_SIZE 4096

static inline void do_compute_work(unsigned long iters, volatile uint64_t *sink) {
    uint64_t x = *sink;
    for (unsigned long i = 0; i < iters; i++) {
        x = x * 1664525u + 1013904223u;   // simple arithmetic loop
    }
    *sink = x;
}

void access_memory(
        char *mem,
        int num_pages,
        int num_fault_pages,
        const char *pattern,
        int extra_accesses_per_page,
        unsigned long compute_iters_per_page)
{
    if (num_fault_pages > num_pages) {
        fprintf(stderr, "num_fault_pages (%d) > num_pages (%d)\n",
                num_fault_pages, num_pages);
        exit(EXIT_FAILURE);
    }

    volatile uint64_t compute_sink = 1;

    for (int i = 0; i < num_fault_pages; i++) {
        int page_index;

        if (strcmp(pattern, "sequential") == 0) {
            page_index = i;
        } else if (strcmp(pattern, "random") == 0) {
            page_index = rand() % num_pages;
        } else {
            fprintf(stderr, "Unknown pattern: %s (use sequential|random)\n", pattern);
            exit(EXIT_FAILURE);
        }

        char *page_base = mem + (size_t)page_index * PAGE_SIZE;

        // 1) Minor page fault (first touch)
        page_base[0]++;

        // 2) Extra memory accesses inside the same page (no new faults)
        for (int a = 0; a < extra_accesses_per_page; a++) {
            size_t offset = (size_t)(a * 64) & (PAGE_SIZE - 1); // hit different cache lines
            page_base[offset]++;
        }

        // 3) Extra computation per fault
        do_compute_work(compute_iters_per_page, &compute_sink);
    }

    // Prevent optimization
    if (compute_sink == 0) fprintf(stderr, "error\n");
}

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr,
            "Usage: %s <num-pages> <num-fault-pages> <extra-accesses> <compute-iters> <pattern>\n"
            "Example: %s 1024 256 64 10000 sequential\n",
            argv[0], argv[0]);
        exit(EXIT_FAILURE);
    }

    int num_pages             = atoi(argv[1]);
    int num_fault_pages       = atoi(argv[2]);
    int extra_accesses        = atoi(argv[3]);
    unsigned long compute_iters = strtoul(argv[4], NULL, 10);
    const char *pattern       = argv[5];

    size_t region_size = (size_t)num_pages * PAGE_SIZE;

    printf("=== Benchmark Configuration ===\n");
    printf("Pages allocated:      %d\n", num_pages);
    printf("Faulting pages:       %d\n", num_fault_pages);
    printf("Extra accesses/page:  %d\n", extra_accesses);
    printf("Compute iters/page:   %lu\n", compute_iters);
    printf("Pattern:              %s\n", pattern);
    printf("Region size:          %zu bytes\n\n", region_size);

    // Allocate memory (will fault on first touch)
    char *mem = mmap(NULL, region_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    srand((unsigned int)time(NULL));

    access_memory(mem,
                  num_pages,
                  num_fault_pages,
                  pattern,
                  extra_accesses,
                  compute_iters);

    printf("Benchmark completed.\n");

    // Clean up
    munmap(mem, region_size);

    return 0;
}
