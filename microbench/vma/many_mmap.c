/* vma/many_mmap: create many small anonymous mappings, touch each once,
   then unmap.  Exercises the kernel's VMA tree insertion/removal cost
   and the physical allocator's small-allocation path. */
#include "../common/bench.h"

static const char *bench_name(void) { return "vma.many_mmap"; }

static void bench_kernel(bench_cfg_t *cfg) {
    /* iters = number of mappings; size = bytes per mapping. */
    uint64_t n = cfg->iters ? cfg->iters : 64;
    size_t   sz = cfg->size_bytes ? cfg->size_bytes : 4096;
    void **ptrs = (void**)calloc((size_t)n, sizeof(void*));
    if (!ptrs) { perror("calloc"); return; }
    for (uint64_t i = 0; i < n; i++) {
        ptrs[i] = mmap(NULL, sz, PROT_READ|PROT_WRITE,
                       MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        if (ptrs[i] == MAP_FAILED) { perror("mmap"); ptrs[i] = NULL; continue; }
        ((volatile char*)ptrs[i])[0] = 1;
        cfg->ops++;
        cfg->bytes_touched += 1;
    }
    for (uint64_t i = 0; i < n; i++)
        if (ptrs[i]) munmap(ptrs[i], sz);
    free(ptrs);
}

BENCH_MAIN
