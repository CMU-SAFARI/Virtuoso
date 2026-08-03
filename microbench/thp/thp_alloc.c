/* thp/thp_alloc: allocate 2MB-aligned anonymous region and touch pages
   at 2MB stride.  Under MimicOS's ReserveTHP allocator, touching a 4K
   sub-page promotes the enclosing 2MB region when utilization crosses
   the configured threshold. */
#include "../common/bench.h"

static const char *bench_name(void) { return "thp.thp_alloc"; }

#ifndef HUGEPAGE_SIZE
#define HUGEPAGE_SIZE (2UL * 1024 * 1024)
#endif

static void bench_kernel(bench_cfg_t *cfg) {
    /* Default: 16 huge pages worth of region. */
    size_t sz = cfg->size_bytes ? cfg->size_bytes : HUGEPAGE_SIZE * 16;
    /* Round up to HUGEPAGE_SIZE. */
    sz = (sz + HUGEPAGE_SIZE - 1) & ~(HUGEPAGE_SIZE - 1);
    char *buf = (char*)mmap(NULL, sz, PROT_READ|PROT_WRITE,
                             MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (buf == MAP_FAILED) { perror("mmap"); return; }
    for (size_t off = 0; off < sz; off += HUGEPAGE_SIZE) {
        buf[off] = (char)(off & 0xff);
        cfg->ops++;
        cfg->bytes_touched += 1;
    }
    munmap(buf, sz);
}

BENCH_MAIN
