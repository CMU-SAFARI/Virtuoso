    // instrument.c — userspace loader with aggregation
    // Aggregates either globally or per-PID and prints every N events.
    //
    // Changes vs original:
    //   * Removed all RAPL/energy PMU usage.
    //   * Added analytical energy estimation (runtime + LLC misses + retired instructions).
    //   * Filters by cgroup id (u64), not PID.
    //
    // Flags:
    //   --cgroup CGID         only trace tasks in this cgroup id
    //   --aggregate N         print a summary every N events (default: 0 = off)
    //   --per-pid             aggregate per PID (default: global)
    //   --einspj X            energy per instruction [pJ], default 3.0
    //   --emissnj X           energy per LLC miss [nJ], default 100.0
    //   --pstaticw X          static/core power [W], default 1.0
    //   --coreghz X           assume core frequency [GHz], default 2.9 (for reporting only)
// instrument.c — userspace loader with aggregation + fault reason decoding

// ... (headers unchanged) ...
#define _GNU_SOURCE
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <linux/types.h>
#include <inttypes.h>
#include <fcntl.h>
#include <stdint.h>

#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "instrument.skel.h"   // generated from instrument.bpf.o

// Keep in sync with BPF side
// CHANGED: add `reason` (enum fault_reason on BPF side)
struct event_t {
    __u32 pid;
    __u32 tid;
    __u64 cgroup;
    __u64 delta_ns;
    __u64 llc_misses;
    __u64 insn_retired;
    __u8  reason;      // 0=UNKNOWN,1=ANON,2=FILE,3=COW,4=SWAP
    int   cpu_enter;
    int   cpu_exit;
    __u8  migrated;    // 1 if cpu_enter != cpu_exit
    char  comm[16];
};

// ---- globals ----
static volatile sig_atomic_t exiting = 0;

// cgroup filter
static __u64 g_filter_cgid = 0;
static const char *g_cgroup_path = NULL;  // alternative to --cgroup: pass a path

// aggregation mode
static int g_per_pid = 0;
static unsigned g_agg_N = 0;

// energy model coefficients
static double g_einstr_pj = 3.0;      // pJ per instruction
static double g_emiss_nj  = 100.0;    // nJ per LLC miss
static double g_pstatic_w = 1.0;      // W static component
static double g_core_ghz  = 3.9;      // for reporting (cycles/IPCs)

static void on_sig(int sig) { (void)sig; exiting = 1; }

// perf_event_open wrapper
static int perf_event_open(struct perf_event_attr *attr, pid_t pid, int cpu, int group_fd, unsigned long flags) {
    return (int)syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

static int sym_available(const char *name) {
    // Fast path: check available_filter_functions if present
    FILE *f = fopen("/sys/kernel/tracing/available_filter_functions", "re");
    if (!f) f = fopen("/sys/kernel/debug/tracing/available_filter_functions", "re");
    if (!f) return 1; // if we can't check, assume yes and let attach try

    char *line = NULL; size_t n = 0; int ok = 0;
    while (getline(&line, &n, f) > 0) {
        if (strstr(line, name)) { ok = 1; break; }
    }
    free(line);
    fclose(f);
    return ok;
}

// ---------- reason decoding ----------
static const char *reason_to_str(__u8 r) {
    switch (r) {
        case 1: return "ANON";  // do_anonymous_page
        case 2: return "FILE";  // do_fault (file/shmem)
        case 3: return "COW";   // do_wp_page
        case 4: return "SWAP";  // do_swap_page
        default: return "UNKNOWN";
    }
}

#define REASON_KINDS 5  // UNKNOWN, ANON, FILE, COW, SWAP
static inline unsigned clamp_reason_index(__u8 r) {
    return (r < REASON_KINDS) ? r : 0;
}

// ------------- Aggregation -------------
struct agg {
    // identity (for per-pid mode)
    __u32 pid;
    char  comm[16];

    // counters
    unsigned long long n;

    long double sum_lat_ns;
    long double sum_llc;
    long double sum_insn;
    long double sum_e_est_J;

    // min/max latency (ns)
    __u64 min_lat_ns;
    __u64 max_lat_ns;

    // CHANGED: per-reason counts in this window
    unsigned long long reason_cnt[REASON_KINDS];

    struct agg *next; // hash chain
};

#define AGG_HASH_BITS 10
#define AGG_BUCKETS (1u << AGG_HASH_BITS)
static struct agg *agg_table[AGG_BUCKETS] = {0};

static unsigned agg_hash(__u32 pid) {
    // very simple hash
    return (pid * 2654435761u) >> (32 - AGG_HASH_BITS);
}

static struct agg *agg_get(__u32 pid, const char comm[16]) {
    if (!g_per_pid) pid = 0; // single global bucket
    unsigned h = agg_hash(pid);
    for (struct agg *a = agg_table[h]; a; a = a->next) {
        if (a->pid == pid) return a;
    }
    struct agg *a = calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->pid = pid;
    if (comm) memcpy(a->comm, comm, 16);
    a->min_lat_ns = ~0ULL;
    a->next = agg_table[h];
    agg_table[h] = a;
    return a;
}

static void agg_reset(struct agg *a) {
    a->n = 0;
    a->sum_lat_ns = a->sum_llc = a->sum_insn = a->sum_e_est_J = 0.0L;
    a->min_lat_ns = ~0ULL;
    a->max_lat_ns = 0;
    memset(a->reason_cnt, 0, sizeof(a->reason_cnt));
}

static void agg_print_and_reset(const struct agg *a, unsigned N) {
    if (a->n == 0) return;
    double mean_lat_us = (double)(a->sum_lat_ns / a->n) / 1000.0;
    double mean_llc    = (double)(a->sum_llc / a->n);
    double mean_insn   = (double)(a->sum_insn / a->n);
    double mean_e_J    = (double)(a->sum_e_est_J / a->n);

    double min_us = (double)a->min_lat_ns / 1000.0;
    double max_us = (double)a->max_lat_ns / 1000.0;

    // CHANGED: build reason histogram string
    char rhist[160];
    unsigned long long r0=a->reason_cnt[0], r1=a->reason_cnt[1], r2=a->reason_cnt[2],
                       r3=a->reason_cnt[3], r4=a->reason_cnt[4];
    double dn = (a->n > 0) ? (double)a->n : 1.0;
    snprintf(rhist, sizeof(rhist),
             "reasons {UNK=%llu(%.0f%%) ANON=%llu(%.0f%%) FILE=%llu(%.0f%%) COW=%llu(%.0f%%) SWAP=%llu(%.0f%%)}",
             r0, 100.0*r0/dn, r1, 100.0*r1/dn, r2, 100.0*r2/dn, r3, 100.0*r3/dn, r4, 100.0*r4/dn);

    if (g_per_pid)
        printf("[AGG pid=%-7u comm=%-16s N=%-6llu]  mean: latency=%9.3f us  llc=%-8.1f  insn=%-10.1f  E_est=%10.6f J  (min/max %.3f/%.3f us)  %s\n",
            a->pid, a->comm, a->n, mean_lat_us, mean_llc, mean_insn, mean_e_J, min_us, max_us, rhist);
    else
        printf("[AGG global N=%-6llu]  mean: latency=%9.3f us  llc=%-8.1f  insn=%-10.1f  E_est=%10.6f J  (min/max %.3f/%.3f us)  %s\n",
            a->n, mean_lat_us, mean_llc, mean_insn, mean_e_J, min_us, max_us, rhist);

    (void)N;
}

static int libbpf_pr(enum libbpf_print_level level, const char *fmt, va_list args) {
    return vfprintf(stderr, fmt, args);
}

static double estimate_energy_joules(const struct event_t *e) {
    // Convert to base units
    double t_sec     = (double)e->delta_ns * 1e-9;
    double e_insn_J  = (double)e->insn_retired * (g_einstr_pj * 1e-12);
    double e_llc_J   = (double)e->llc_misses   * (g_emiss_nj  * 1e-9);
    double e_static  = g_pstatic_w * t_sec;

    // Optional: derived reporting (not used in energy): cycles & IPC
    (void)g_core_ghz;

    return e_insn_J + e_llc_J + e_static;
}

static int handle_event(void *ctx, void *data, size_t len) {
    (void)ctx;
    if (len < sizeof(struct event_t)) return 0;
    const struct event_t *e = data;

    // Derived numbers (for print)
    double e_est_J = estimate_energy_joules(e);
    double t_us    = (double)e->delta_ns / 1000.0;

    // Optional reporting: cycles & IPC (frequency assumed static)
    double cycles = (double)e->delta_ns * 1e-9 * (g_core_ghz * 1e9);
    double ipc    = (cycles > 0.0) ? ((double)e->insn_retired / cycles) : 0.0;

    // CHANGED: print reason
    const char *rstr = reason_to_str(e->reason);

    printf("%-16s pid=%-7u cgid=%-12llu tid=%-7u  reason=%-7s"
           "  cpu_enter=%-3d cpu_exit=%-3d migrated=%-1u"
           "  latency=%-10" PRIu64 " ns (%6.1f us)"
           "  llc=%-8" PRIu64 "  insn=%-10" PRIu64
           "  E_est=%10.6f J  (cycles≈%.0f, IPC=%.2f)\n",
           e->comm, e->pid, e->cgroup, e->tid, rstr,
           e->cpu_enter, e->cpu_exit, e->migrated,
           (uint64_t)e->delta_ns, t_us,
           (uint64_t)e->llc_misses, (uint64_t)e->insn_retired,
           e_est_J, cycles, ipc);

    if (g_agg_N == 0) return 0; // aggregation disabled

    struct agg *a = agg_get(g_per_pid ? e->pid : 0, e->comm);
    if (!a) return 0;
    if (a->n == 0 && g_per_pid) memcpy(a->comm, e->comm, sizeof(a->comm));

    a->n++;
    a->sum_lat_ns += (long double)e->delta_ns;
    a->sum_llc    += (long double)e->llc_misses;
    a->sum_insn   += (long double)e->insn_retired;
    a->sum_e_est_J += (long double)e_est_J;

    if (e->delta_ns < a->min_lat_ns) a->min_lat_ns = e->delta_ns;
    if (e->delta_ns > a->max_lat_ns) a->max_lat_ns = e->delta_ns;

    // CHANGED: reason histogram
    a->reason_cnt[clamp_reason_index(e->reason)]++;

    if (a->n >= g_agg_N) {
        agg_print_and_reset(a, g_agg_N);
        agg_reset(a);
    }
    return 0;
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [--cgroup CGID] [--aggregate N] [--per-pid]\n"
        "              [--einspj PJ] [--emissnj NJ] [--pstaticw W] [--coreghz GHZ]\n"
        "  --cgroup CGID   Only trace this cgroup id (u64)\n"
        "  --aggregate N   Print summary every N events (global or per PID)\n"
        "  --per-pid       Aggregate separately per PID (default: global)\n"
        "  --einspj PJ     Energy per instruction in pJ (default 3.0)\n"
        "  --emissnj NJ    Energy per LLC miss in nJ (default 100.0)\n"
        "  --pstaticw W    Static/core power in Watts (default 1.0)\n"
        "  --coreghz GHZ   Assumed core frequency in GHz (default 3.9; reporting only)\n",
        prog);
}

int main(int argc, char **argv)
{
    struct instrument_bpf *skel = NULL;
    struct ring_buffer *rb = NULL;
    int err = 0;

    // args (unchanged)
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--cgroup") && i + 1 < argc) {
            g_filter_cgid = strtoull(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--cgroup-path") && i + 1 < argc) {
            g_cgroup_path = argv[++i];
        } else if (!strcmp(argv[i], "--aggregate") && i + 1 < argc) {
            g_agg_N = (unsigned)strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--per-pid")) {
            g_per_pid = 1;
        } else if (!strcmp(argv[i], "--einspj") && i + 1 < argc) {
            g_einstr_pj = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--emissnj") && i + 1 < argc) {
            g_emiss_nj = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--pstaticw") && i + 1 < argc) {
            g_pstatic_w = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--coreghz") && i + 1 < argc) {
            g_core_ghz = atof(argv[++i]);
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]); return 0;
        }
    }

    libbpf_set_print(libbpf_pr);

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    skel = instrument_bpf__open();

    // bpf_program__set_autoload(skel->progs.TP_do_anonymous_page, false);
    // bpf_program__set_autoload(skel->progs.TP_do_fault,         false);
    // bpf_program__set_autoload(skel->progs.TP_do_wp_page,       false);
    // bpf_program__set_autoload(skel->progs.TP_do_swap_page,     false);

    // // (Keep kprobes; optionally enable only what exists)
    // bpf_program__set_autoload(skel->progs.KP_do_anonymous_page, true);
    // bpf_program__set_autoload(skel->progs.KP___do_fault,        true);
    // bpf_program__set_autoload(skel->progs.KP_do_fault,          true); // fallback
    // bpf_program__set_autoload(skel->progs.KP_do_wp_page,        true);
    // bpf_program__set_autoload(skel->progs.KP_do_swap_page,      true);
    
    if (!skel) { fprintf(stderr, "Failed to open BPF skeleton\n"); return 1; }

    int ncpus = 32;
    if (ncpus < 1) { fprintf(stderr, "Failed to get CPU count\n"); return 1; }

    // Size PERF_EVENT_ARRAY maps before load
    bpf_map__set_max_entries(skel->maps.pe_llc,  ncpus);
    bpf_map__set_max_entries(skel->maps.pe_insn, ncpus);

    // Pass cgroup filter to BPF
    if (g_cgroup_path && !g_filter_cgid) {
        struct stat st;
        if (stat(g_cgroup_path, &st) == 0) {
            g_filter_cgid = (__u64)st.st_ino;
        } else {
            fprintf(stderr, "stat('%s') failed: %s\n", g_cgroup_path, strerror(errno));
            return 1;
        }
    }
    skel->rodata->target_cgid = g_filter_cgid;

    if ((err = instrument_bpf__load(skel))) {
        fprintf(stderr, "Failed to load BPF skeleton: %d\n", err);
        goto out_free;
    }


    int fd_map_llc  = bpf_map__fd(skel->maps.pe_llc);
    int fd_map_insn = bpf_map__fd(skel->maps.pe_insn);

    int *fd_llc  = calloc(ncpus, sizeof(int));
    int *fd_insn = calloc(ncpus, sizeof(int));
    if (!fd_llc || !fd_insn) { fprintf(stderr, "calloc failed\n"); err = -ENOMEM; goto out_free; }

    struct perf_event_attr attr = {0};
    attr.size = sizeof(attr);
    attr.disabled = 0;
    attr.inherit = 0;
    attr.exclude_kernel = 0;
    attr.exclude_user   = 0;
    attr.exclude_hv     = 1;
    attr.exclude_idle   = 0;
    attr.pinned = 1;
    attr.exclusive = 0;
    attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;

    for (int cpu = 0; cpu < ncpus; cpu++) {
        // LLC MISSES
        attr.type = PERF_TYPE_HARDWARE;
        attr.config = PERF_COUNT_HW_CACHE_MISSES;
        fd_llc[cpu] = perf_event_open(&attr, -1, cpu, -1, 0);
        if (fd_llc[cpu] < 0) { fprintf(stderr, "perf_event_open(LLC,cpu=%d): %s\n", cpu, strerror(errno)); err = -errno; goto out_free; }
        if (bpf_map_update_elem(fd_map_llc, &cpu, &fd_llc[cpu], BPF_ANY) != 0) { fprintf(stderr, "map pe_llc cpu=%d: %s\n", cpu, strerror(errno)); err = -errno; goto out_free; }

        // INSTRUCTIONS
        attr.type = PERF_TYPE_HARDWARE;
        attr.config = PERF_COUNT_HW_INSTRUCTIONS;
        fd_insn[cpu] = perf_event_open(&attr, -1, cpu, -1, 0);
        if (fd_insn[cpu] < 0) { fprintf(stderr, "perf_event_open(INSN,cpu=%d): %s\n", cpu, strerror(errno)); err = -errno; goto out_free; }
        if (bpf_map_update_elem(fd_map_insn, &cpu, &fd_insn[cpu], BPF_ANY) != 0) { fprintf(stderr, "map pe_insn cpu=%d: %s\n", cpu, strerror(errno)); err = -errno; goto out_free; }
    }

    if ((err = instrument_bpf__attach(skel))) { fprintf(stderr, "Failed to attach BPF programs: %d\n", err); goto out_free; }

    if (g_agg_N)
        printf("Aggregation: %s, window N=%u\n", g_per_pid ? "per-PID" : "global", g_agg_N);

    printf("Model: E = P_static*t + k_insn*insn + k_llc*misses  "
           "(P_static=%.3f W, k_insn=%.3f pJ, k_llc=%.3f nJ, core=%.2f GHz)\n",
           g_pstatic_w, g_einstr_pj, g_emiss_nj, g_core_ghz);

    if (g_filter_cgid)
        printf("Filtering to cgroup id: %llu%s%s\n",
               (unsigned long long)g_filter_cgid,
               g_cgroup_path ? " (path: " : "",
               g_cgroup_path ? g_cgroup_path : "");
    if (g_cgroup_path) printf(")\n");

    rb = ring_buffer__new(bpf_map__fd(skel->maps.events), handle_event, NULL, NULL);
    if (!rb) { err = -errno; fprintf(stderr, "ring_buffer__new failed: %d\n", err); goto out_free; }

    printf("Tracing MINOR page faults%s… latency + LLC + INSN + reason (analytical energy). Ctrl-C to stop.\n",
        g_filter_cgid ? " (filtered)" : "");

    while (!exiting) {
        err = ring_buffer__poll(rb, 250);
        if (err == -EINTR) break;
        if (err < 0) { fprintf(stderr, "ring_buffer__poll failed: %d\n", err); break; }
    }

    // On exit, print any partial aggregates
    if (g_agg_N) {
        for (unsigned b = 0; b < AGG_BUCKETS; b++) {
            for (struct agg *a = agg_table[b]; a; a = a->next) {
                if (a->n) agg_print_and_reset(a, g_agg_N);
            }
        }
    }

out_free:
    if (rb) ring_buffer__free(rb);
    if (skel) instrument_bpf__destroy(skel);
    // perf fds will close on process exit; explicit close skipped for brevity
    return err != 0;
}
