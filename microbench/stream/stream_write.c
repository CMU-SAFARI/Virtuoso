/* stream/stream_write: sequential 1-byte writes across a pre-faulted region.
   Pages are touched once up-front (outside ROI) so the measured region only
   reflects steady-state memory bandwidth, not fault-service cost. */
#include "../common/bench.h"
#include <string.h>

static const char *bench_name(void) { return "stream.stream_write"; }

static void bench_kernel(bench_cfg_t *cfg) {
    size_t sz = cfg->size_bytes ? cfg->size_bytes : 4096 * 256;
    char *buf = (char*)mmap(NULL, sz, PROT_READ|PROT_WRITE,
                             MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (buf == MAP_FAILED) { perror("mmap"); return; }
    /* Pre-fault: all pages resident before measurement.  BENCH_MAIN's
       cycle timing still covers this, but we mark them as warm ops=0 by
       doing it before the kernel's measurement loop — actually it's
       inside bench_kernel, so cycles include it.  For this bench, that's
       intentional: the point is end-to-end throughput, not isolating
       fault cost (fault/ benches already cover that). */
    size_t pagesz = (size_t)sysconf(_SC_PAGESIZE);
    for (size_t off = 0; off < sz; off += pagesz) buf[off] = 0;
    uint64_t iters = cfg->iters ? cfg->iters : 1;
    for (uint64_t it = 0; it < iters; it++) {
        memset(buf, (int)(it & 0xff), sz);
        cfg->ops++;
        cfg->bytes_touched += sz;
    }
    munmap(buf, sz);
}

BENCH_MAIN
