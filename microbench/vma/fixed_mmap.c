/* vma/fixed_mmap: MAP_FIXED mmap at a chosen address, touch pages, then
   mprotect + munmap.  Because the address is in args[0], Sniper's
   AddressSpaceRegistry can observe the mapping without needing the
   (currently unrecorded) real syscall return value. */
#include "../common/bench.h"
#include <sys/mman.h>

static const char *bench_name(void) { return "vma.fixed_mmap"; }

static void bench_kernel(bench_cfg_t *cfg) {
    size_t sz = cfg->size_bytes ? cfg->size_bytes : 4096 * 32;
    /* Pick a hint in a high region unlikely to collide. */
    void *hint = (void*)0x50000000ULL;
    char *buf = (char*)mmap(hint, sz,
                             PROT_READ|PROT_WRITE,
                             MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
    if (buf == MAP_FAILED) { perror("mmap MAP_FIXED"); return; }
    size_t pagesz = (size_t)sysconf(_SC_PAGESIZE);
    for (size_t off = 0; off < sz; off += pagesz) {
        buf[off] = (char)(off & 0xff);
        cfg->ops++;
        cfg->bytes_touched += 1;
    }
    /* Flip prot on half to exercise mprotect observability. */
    if (sz >= pagesz * 2) {
        mprotect(buf, sz / 2, PROT_READ);
    }
    munmap(buf, sz);
}

BENCH_MAIN
