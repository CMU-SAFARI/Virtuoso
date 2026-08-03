/* thread/three_thread_anon_write: spawn 2 worker pthreads for a total of
   3 app threads.  Exercises Stage 3 oversubscription: on N=2 cores with
   round-robin initial placement (parent=core0, workers=core1, core0), one
   core ends up with 2 runnable app threads — the quantum-based preemption
   is what lets the second one run. */

#define _GNU_SOURCE
#include <pthread.h>
#include "../common/bench.h"

static const char *bench_name(void) { return "thread.three_thread_anon_write"; }

static void bench_kernel(bench_cfg_t *cfg) {
    /* Larger default than the 2-thread variant so the quantum fires
       multiple times per thread. */
    size_t sz = cfg->size_bytes ? cfg->size_bytes : 4096 * 256;
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
    int         thread_id;
    const char *label;
} thread_arg_t;

static void *worker(void *arg) {
    thread_arg_t *ta = (thread_arg_t*)arg;
    char tname[64];
    snprintf(tname, sizeof(tname), "%s-%s", bench_name(), ta->label);
    SimSetThreadName(tname);
    fprintf(stderr, "[bench %s] SimThreadRoiStart(%d)\n", ta->label, ta->thread_id);
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

    thread_arg_t main_arg  = { .cfg = base, .thread_id = 100 + base.id, .label = "main"   };
    thread_arg_t worker1   = { .cfg = base, .thread_id = 200 + base.id, .label = "wrk1"   };
    thread_arg_t worker2   = { .cfg = base, .thread_id = 300 + base.id, .label = "wrk2"   };

    pthread_t t1, t2;
    if (pthread_create(&t1, NULL, worker, &worker1) != 0) { perror("pthread_create"); return 1; }
    if (pthread_create(&t2, NULL, worker, &worker2) != 0) { perror("pthread_create"); return 1; }

    worker(&main_arg);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    if (base.out != stdout) { fclose(base.out); }
    return 0;
}
