/* stride/stride_read: page-strided reads, every STRIDE bytes, across a
   pre-faulted region.  Useful for TLB / prefetcher behaviour — compare to
   stream_read for the same size. */
#include "../common/bench.h"

static const char *bench_name(void) { return "stride.stride_read"; }

static void bench_kernel(bench_cfg_t *cfg) {
    size_t sz = cfg->size_bytes ? cfg->size_bytes : 4096 * 256;
    size_t stride = 4096; /* one page */
    volatile char *buf = (volatile char*)mmap(NULL, sz,
                             PROT_READ|PROT_WRITE,
                             MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (buf == MAP_FAILED) { perror("mmap"); return; }
    /* Pre-fault so we don't mix fault cost into stride throughput. */
    for (size_t off = 0; off < sz; off += 4096) buf[off] = 0;
    uint64_t iters = cfg->iters ? cfg->iters : 16;
    volatile uint64_t sink = 0;
    for (uint64_t it = 0; it < iters; it++) {
        for (size_t off = 0; off < sz; off += stride) {
            sink ^= (uint64_t)buf[off];
            cfg->ops++;
        }
    }
    cfg->bytes_touched = cfg->ops; /* 1 byte per op */
    (void)sink;
    munmap((void*)buf, sz);
}

BENCH_MAIN
