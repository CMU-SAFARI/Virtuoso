/* fault/anon_read: first-touch read of anonymous pages.  Linux typically
   satisfies these with the shared zero page (no real allocation), so this
   is a useful baseline for the cost of a read-fault vs write-fault. */
#include "../common/bench.h"

static const char *bench_name(void) { return "fault.anon_read"; }

static void bench_kernel(bench_cfg_t *cfg) {
    size_t sz = cfg->size_bytes ? cfg->size_bytes : 4096 * 64;
    volatile char *buf = (volatile char*)mmap(NULL, sz, PROT_READ,
                             MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (buf == MAP_FAILED) { perror("mmap"); return; }
    size_t pagesz = (size_t)sysconf(_SC_PAGESIZE);
    volatile char sink = 0;
    for (size_t off = 0; off < sz; off += pagesz) {
        sink ^= buf[off];
        cfg->ops++;
        cfg->bytes_touched += 1;
    }
    (void)sink;
    munmap((void*)buf, sz);
}

BENCH_MAIN
