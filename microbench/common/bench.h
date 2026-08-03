/* Microbenchmark harness for Virtuoso/MimicOS live-spawn (Stage B, Apr 16 2026).
 *
 * Each bench is a small binary with a single measured region.  The harness:
 *   - parses common flags (--size, --iters, --id, --out),
 *   - enters the MimicOS per-thread ROI barrier (SimThreadRoiStart(id)),
 *   - runs the bench's kernel,
 *   - leaves the barrier, and
 *   - emits one JSONL summary line to the output stream.
 *
 * Benches should only implement `bench_kernel()` and `bench_name()`; main()
 * is provided inline in this header (BENCH_MAIN macro). */
#ifndef VIRTUOSO_MICROBENCH_BENCH_H
#define VIRTUOSO_MICROBENCH_BENCH_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/mman.h>
#include <sys/time.h>

#include "sim_api.h"

/* Shared config passed into the bench kernel. */
typedef struct {
    size_t   size_bytes;    /* e.g. region size, buffer size */
    uint64_t iters;         /* number of iterations the kernel should run */
    int      id;            /* ROI participant handle (unique per process) */
    FILE    *out;           /* summary sink (stdout by default) */
    /* Free-form bench-private counters the kernel fills in: */
    uint64_t ops;           /* ops performed (e.g. faults, accesses) */
    uint64_t cycles;        /* rdtsc cycle delta across the measured region */
    uint64_t bytes_touched; /* for bandwidth reporting */
} bench_cfg_t;

/* Each bench provides these two symbols. */
static const char *bench_name(void);
static void        bench_kernel(bench_cfg_t *cfg);

static inline uint64_t bench_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline void bench_pin_to_cpu(int cpu) {
#ifdef __linux__
    /* Optional: benches can opt out by defining BENCH_NO_PIN. */
    (void)cpu;
#endif
}

static inline void bench_parse_common(int argc, char **argv, bench_cfg_t *cfg) {
    cfg->size_bytes    = 16 * 4096;    /* 16 pages */
    cfg->iters         = 1;
    cfg->id            = 0;
    cfg->out           = stdout;
    cfg->ops           = 0;
    cfg->cycles        = 0;
    cfg->bytes_touched = 0;

    static struct option longopts[] = {
        {"size",  required_argument, 0, 's'},
        {"iters", required_argument, 0, 'n'},
        {"id",    required_argument, 0, 'i'},
        {"out",   required_argument, 0, 'o'},
        {0,0,0,0},
    };
    int c;
    while ((c = getopt_long(argc, argv, "s:n:i:o:", longopts, NULL)) != -1) {
        switch (c) {
        case 's': cfg->size_bytes = (size_t)strtoull(optarg, NULL, 0); break;
        case 'n': cfg->iters      = strtoull(optarg, NULL, 0);         break;
        case 'i': cfg->id         = atoi(optarg);                      break;
        case 'o': {
            FILE *f = fopen(optarg, "w");
            if (!f) { perror("fopen --out"); exit(1); }
            cfg->out = f;
            break;
        }
        default:
            fprintf(stderr, "usage: %s [--size B] [--iters N] [--id X] [--out path]\n", argv[0]);
            exit(1);
        }
    }
}

static inline void bench_emit_summary(const bench_cfg_t *cfg) {
    double cycles_per_op = cfg->ops ? (double)cfg->cycles / (double)cfg->ops : 0.0;
    double bytes_per_cyc = cfg->cycles ? (double)cfg->bytes_touched / (double)cfg->cycles : 0.0;
    fprintf(cfg->out,
        "{\"bench\":\"%s\",\"size\":%zu,\"iters\":%" PRIu64
        ",\"ops\":%" PRIu64 ",\"cycles\":%" PRIu64
        ",\"bytes_touched\":%" PRIu64
        ",\"cycles_per_op\":%.2f,\"bytes_per_cycle\":%.4f}\n",
        bench_name(),
        cfg->size_bytes, cfg->iters,
        cfg->ops, cfg->cycles,
        cfg->bytes_touched,
        cycles_per_op, bytes_per_cyc);
    fflush(cfg->out);
}

#ifndef BENCH_MAIN_NO_PIN_INCLUDE
#  include <inttypes.h>
#endif

#define BENCH_MAIN                                             \
    int main(int argc, char **argv) {                          \
        bench_cfg_t cfg;                                       \
        bench_parse_common(argc, argv, &cfg);                  \
        SimSetThreadName(bench_name());                        \
        /* Enter the per-thread ROI barrier.  When the kernel  \
           has set SimRoiExpect(N) and N threads join, perf    \
           model turns on. */                                  \
        SimThreadRoiStart(cfg.id);                             \
        uint64_t t0 = bench_rdtsc();                           \
        bench_kernel(&cfg);                                    \
        uint64_t t1 = bench_rdtsc();                           \
        cfg.cycles = t1 - t0;                                  \
        /* Emit the summary BEFORE leaving the ROI barrier:    \
           last-leave triggers TraceManager::stop() on the     \
           Sniper side, which tears down the PIN child before  \
           its stdout buffer flushes. */                       \
        bench_emit_summary(&cfg);                              \
        if (cfg.out != stdout) { fclose(cfg.out); cfg.out = NULL; } \
        SimThreadRoiEnd(cfg.id);                               \
        return 0;                                              \
    }

#endif /* VIRTUOSO_MICROBENCH_BENCH_H */
