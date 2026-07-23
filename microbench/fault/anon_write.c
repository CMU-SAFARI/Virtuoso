/* fault/anon_write: first-touch write to N pages of an anonymous mmap.
   Every page-granularity write triggers a minor page fault that MimicOS
   serves from its physical allocator. */
#include "../common/bench.h"

static const char *bench_name(void) { return "fault.anon_write"; }

static void bench_kernel(bench_cfg_t *cfg) {
    size_t sz = cfg->size_bytes ? cfg->size_bytes : 4096 * 64;
    char *buf = (char*)mmap(NULL, sz, PROT_READ|PROT_WRITE,
                             MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (buf == MAP_FAILED) { perror("mmap"); return; }
    size_t pagesz = (size_t)sysconf(_SC_PAGESIZE);
    for (size_t off = 0; off < sz; off += pagesz) {
        buf[off] = (char)(off & 0xff);
        cfg->ops++;
        cfg->bytes_touched += 1;
    }
    munmap(buf, sz);
}

BENCH_MAIN
