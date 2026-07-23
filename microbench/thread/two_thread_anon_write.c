/* thread/two_thread_anon_write: spawn one pthread and have both the main
   thread and the child thread do first-touch writes to separate anon mmaps.
   Tests the "live app spawns threads" code path through PIN + Sniper.

   Each thread:
   - SimThreadRoiStart(id)            // id is per-thread (main=100+cfg.id, child=200+cfg.id)
   - writes every page of its buffer
   - bench_emit_summary
   - SimThreadRoiEnd(id)

   NOTE: uses a custom main (does not use BENCH_MAIN) because BENCH_MAIN assumes
   a single thread of execution. */

#define _GNU_SOURCE
#include <pthread.h>
#include "../common/bench.h"

static const char *bench_name(void) { return "thread.two_thread_anon_write"; }

static void bench_kernel(bench_cfg_t *cfg) {
    size_t sz = cfg->size_bytes ? cfg->size_bytes : 4096 * 32;
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

typedef struct {
    bench_cfg_t cfg;
    int         thread_id;   /* ROI handle */
    const char *label;
} thread_arg_t;

static void *worker(void *arg) {
    thread_arg_t *ta = (thread_arg_t*)arg;
    char tname[64];
    snprintf(tname, sizeof(tname), "%s-%s", bench_name(), ta->label);
    SimSetThreadName(tname);
    fprintf(stderr, "[bench %s] about to SimThreadRoiStart(%d)\n", ta->label, ta->thread_id);
    SimThreadRoiStart(ta->thread_id);
    uint64_t t0 = bench_rdtsc();
    bench_kernel(&ta->cfg);
    uint64_t t1 = bench_rdtsc();
    ta->cfg.cycles = t1 - t0;
    bench_emit_summary(&ta->cfg);
    SimThreadRoiEnd(ta->thread_id);
    return NULL;
}

int main(int argc, char **argv) {
    bench_cfg_t base;
    bench_parse_common(argc, argv, &base);

    /* Two participants: main + one spawned child. */
    thread_arg_t main_arg  = { .cfg = base, .thread_id = 100 + base.id, .label = "main"  };
    thread_arg_t child_arg = { .cfg = base, .thread_id = 200 + base.id, .label = "child" };

    /* IMPORTANT: the MimicOS boot only calls SimRoiExpect(1) when
       num_live > 0 in the INI, which is the default with this bench.
       With SimRoiExpect(1), the barrier fires when the FIRST
       SimThreadRoiStart lands — whichever thread gets there first
       enables the perf model for everyone. */

    pthread_t child_tid;
    if (pthread_create(&child_tid, NULL, worker, &child_arg) != 0) {
        perror("pthread_create");
        return 1;
    }

    /* Run main thread's workload concurrently. */
    worker(&main_arg);

    pthread_join(child_tid, NULL);

    if (base.out != stdout) { fclose(base.out); }
    return 0;
}
