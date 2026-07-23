/* cow/cow_touch: map a region read-only (MAP_PRIVATE), fault in shared
   read, then force COW by toggling PROT_WRITE + writing each page.
   Measures the kernel's copy-on-write fault path separately from the
   initial first-touch path. */
#include "../common/bench.h"
#include <sys/mman.h>

static const char *bench_name(void) { return "cow.cow_touch"; }

static void bench_kernel(bench_cfg_t *cfg) {
    size_t sz = cfg->size_bytes ? cfg->size_bytes : 4096 * 64;
    size_t pagesz = (size_t)sysconf(_SC_PAGESIZE);
    /* Initial mapping read-only → zero-page shared. */
    char *buf = (char*)mmap(NULL, sz, PROT_READ,
                             MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (buf == MAP_FAILED) { perror("mmap"); return; }
    volatile char sink = 0;
    for (size_t off = 0; off < sz; off += pagesz) sink ^= buf[off];
    (void)sink;
    /* Transition to writable → each write page-faults as COW. */
    if (mprotect(buf, sz, PROT_READ|PROT_WRITE) != 0) {
        perror("mprotect");
        munmap(buf, sz);
        return;
    }
    for (size_t off = 0; off < sz; off += pagesz) {
        buf[off] = (char)(off & 0xff);
        cfg->ops++;
        cfg->bytes_touched += 1;
    }
    munmap(buf, sz);
}

BENCH_MAIN
