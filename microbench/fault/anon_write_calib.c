/* fault/anon_write_calib: calibration variant of anon_write.
 *
 * Identical fault pattern to fault/anon_write.c (first-touch write to
 * anonymous mmap pages, one minor fault per page) but with a larger
 * default region so the per-phase CSV emitted by MimicOS has enough
 * samples (≈ 10 000 pages) to produce meaningful p95/p99 estimates.
 * This is the matched bench for experiments/calibration/fault_bench on
 * real Linux — same access pattern, same fault type (anon minor),
 * roughly the same sample count.
 */
#include "../common/bench.h"

static const char *bench_name(void) { return "fault.anon_write_calib"; }

static void bench_kernel(bench_cfg_t *cfg) {
    /* bench_parse_common unconditionally sets size_bytes = 16 pages, and
       MimicOS's spawn_live_application only passes '--id' (not --size),
       so we can't get a larger region from the CLI.  Hardcode 10 000
       pages here — this bench exists specifically to generate enough
       fault samples for meaningful p99 estimates. */
    size_t sz = 4096UL * 10000UL;
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
