// SPDX-License-Identifier: GPL-2.0
//
// instrument.c - Userspace loader and event consumer for the eBPF page fault
// instrumentation tool (instrument.bpf.c).
//
// === What This Program Does ===
//
//   1. Opens, configures, loads, and attaches the BPF skeleton (instrument.bpf.o).
//   2. Opens per-CPU hardware performance counters (LLC misses, instructions retired)
//      via perf_event_open() and stores their file descriptors in BPF maps so the
//      kernel-side BPF program can read them.
//   3. Polls the BPF ring buffer for completed page fault events.
//   4. For each event, prints a one-line summary with: process name, PID, TID,
//      fault latency (ns/us), LLC misses, instructions retired, fault reason
//      (ANON/FILE/COW/SWAP), CPU info, and estimated energy.
//   5. Optionally aggregates events into windows of N faults, computing mean/min/max
//      latency, mean LLC misses, mean instructions, and a per-reason histogram.
//
// === Energy Model ===
//
//   Since direct RAPL (Running Average Power Limit) readings from BPF are not
//   portable across platforms, this tool uses an analytical energy model:
//
//     E_fault = P_static * t + k_insn * insn_retired + k_llc * llc_misses
//
//   Where:
//     P_static  = baseline core power consumption (default: 1.0 W)
//     k_insn    = energy per retired instruction (default: 3.0 pJ)
//     k_llc     = energy per LLC miss (default: 100.0 nJ, includes DRAM access)
//     t         = fault duration in seconds
//
//   These defaults are approximate for a modern server-class CPU. Adjust with
//   --einspj, --emissnj, and --pstaticw flags for your specific hardware.
//
// === Aggregation Modes ===
//
//   --aggregate N       Print a summary line every N page fault events.
//   --per-pid           Maintain separate aggregation buckets per PID.
//                       Without this, all events are aggregated into a single bucket.
//
//   The aggregation window reports:
//     - Mean, min, max latency
//     - Mean LLC misses and instructions per fault
//     - Mean estimated energy per fault
//     - Reason histogram: how many faults were ANON, FILE, COW, SWAP, UNKNOWN
//
// === Usage ===
//
//   sudo ./instrument [options]
//
//   Options:
//     --cgroup CGID     Only trace tasks in this cgroup id (u64)
//     --cgroup-path P   Resolve cgroup path to inode and use as filter
//     --aggregate N     Print summary every N events (0 = per-event only)
//     --per-pid         Aggregate per PID instead of globally
//     --einspj PJ       Energy per instruction in picojoules (default: 3.0)
//     --emissnj NJ      Energy per LLC miss in nanojoules (default: 100.0)
//     --pstaticw W      Static core power in Watts (default: 1.0)
//     --coreghz GHZ     Assumed core frequency in GHz (default: 3.9, reporting only)
//
// === Example ===
//
//   Terminal 1: sudo ./instrument --aggregate 1000
//   Terminal 2: ./minor_fault_stress $((256*1024*1024))
//
//   Output:
//     minor_fault_str pid=12345  reason=ANON  latency=2850 ns (2.9 us)  llc=3  insn=1205 ...
//     [AGG global N=1000]  mean: latency=3.142 us  llc=4.2  insn=1180.3 ...
//       reasons {UNK=0(0%) ANON=1000(100%) FILE=0(0%) COW=0(0%) SWAP=0(0%)}
//
// === Build ===
//
//   This file is compiled by the Makefile alongside instrument.bpf.c.
//   The BPF skeleton header (instrument.skel.h) is auto-generated from the
//   compiled BPF object by bpftool.
//
//   Dependencies: libbpf-dev, clang (for BPF compilation), bpftool

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
#include <sys/stat.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <linux/types.h>
#include <inttypes.h>
#include <fcntl.h>
#include <stdint.h>

#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "instrument.skel.h"   // Auto-generated BPF skeleton from instrument.bpf.o

// ------------------------------------------------------------------
// Event structure (must match struct event_t in instrument.bpf.c)
// ------------------------------------------------------------------
struct event_t {
    __u32 pid;           // TGID (process id)
    __u32 tid;           // PID (thread id)
    __u64 cgroup;        // Cgroup id of the faulting task
    __u64 delta_ns;      // Fault handling latency in nanoseconds
    __u64 llc_misses;    // LLC misses during the fault (0 if migrated or unavailable)
    __u64 insn_retired;  // Instructions retired during the fault
    __u8  reason;        // Fault type: 0=UNKNOWN, 1=ANON, 2=FILE, 3=COW, 4=SWAP
    int   cpu_enter;     // CPU at kprobe entry
    int   cpu_exit;      // CPU at kretprobe exit
    __u8  migrated;      // 1 if cpu_enter != cpu_exit (PMU deltas zeroed)
    char  comm[16];      // Task name (truncated to 16 chars by kernel)
};

// ------------------------------------------------------------------
// Globals
// ------------------------------------------------------------------

static volatile sig_atomic_t exiting = 0;

// Cgroup filter: only trace faults from tasks in this cgroup.
// 0 = no filter (trace everything).
static __u64 g_filter_cgid = 0;
static const char *g_cgroup_path = NULL;  // Alternative: resolve path to inode

// Aggregation: accumulate N events before printing a summary line.
static int g_per_pid = 0;      // 0 = single global bucket, 1 = per-PID buckets
static unsigned g_agg_N = 0;   // 0 = disabled (print every event)

// Analytical energy model coefficients.
// Default values are approximate for a modern server CPU (e.g., Intel Xeon).
static double g_einstr_pj = 3.0;      // Energy per retired instruction (picojoules)
static double g_emiss_nj  = 100.0;    // Energy per LLC miss (nanojoules, includes DRAM)
static double g_pstatic_w = 1.0;      // Static/leakage power (Watts)
static double g_core_ghz  = 3.9;      // Assumed core frequency (GHz, for IPC reporting)

static void on_sig(int sig) { (void)sig; exiting = 1; }

// ------------------------------------------------------------------
// perf_event_open() syscall wrapper
// ------------------------------------------------------------------
// Linux does not provide a glibc wrapper for perf_event_open().
// We call the syscall directly. The returned fd is stored in the BPF
// perf event array map so the kernel-side BPF program can read counters.
static int perf_event_open(struct perf_event_attr *attr, pid_t pid,
                           int cpu, int group_fd, unsigned long flags)
{
    return (int)syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

// ------------------------------------------------------------------
// Check if a kernel symbol is available for kprobing
// ------------------------------------------------------------------
// Reads /sys/kernel/tracing/available_filter_functions to verify that
// a function symbol exists and can be kprobed. Some static functions
// may exist in kallsyms but are inlined and not kprobe-able.
static int sym_available(const char *name)
{
    FILE *f = fopen("/sys/kernel/tracing/available_filter_functions", "re");
    if (!f) f = fopen("/sys/kernel/debug/tracing/available_filter_functions", "re");
    if (!f) return 1;  // If we can't check, assume yes and let attach fail gracefully

    char *line = NULL;
    size_t n = 0;
    int ok = 0;
    while (getline(&line, &n, f) > 0) {
        if (strstr(line, name)) { ok = 1; break; }
    }
    free(line);
    fclose(f);
    return ok;
}

// ------------------------------------------------------------------
// Fault reason decoding
// ------------------------------------------------------------------
// Convert the numeric reason code from BPF to a human-readable string.
// These map to the enum fault_reason in instrument.bpf.c.
static const char *reason_to_str(__u8 r)
{
    switch (r) {
        case 1: return "ANON";    // do_anonymous_page: first-touch zero page
        case 2: return "FILE";    // do_fault: file/shmem-backed page
        case 3: return "COW";     // do_wp_page: copy-on-write
        case 4: return "SWAP";    // do_swap_page: swap-in from disk
        default: return "UNKNOWN";
    }
}

#define REASON_KINDS 5  // UNKNOWN(0), ANON(1), FILE(2), COW(3), SWAP(4)

static inline unsigned clamp_reason_index(__u8 r)
{
    return (r < REASON_KINDS) ? r : 0;
}

// ------------------------------------------------------------------
// Aggregation subsystem
// ------------------------------------------------------------------
// Accumulates per-fault metrics over a window of N events, then prints
// summary statistics (mean, min, max) and resets. In per-PID mode,
// each PID gets its own bucket; otherwise all events go to one bucket.
//
// Uses a simple open-addressing hash table with chaining.

struct agg {
    __u32 pid;              // Process id (0 if global mode)
    char  comm[16];         // Last-seen task name for this PID

    unsigned long long n;   // Event count in current window

    // Running sums for computing means.
    long double sum_lat_ns;
    long double sum_llc;
    long double sum_insn;
    long double sum_e_est_J;

    __u64 min_lat_ns;       // Minimum latency in current window
    __u64 max_lat_ns;       // Maximum latency in current window

    // Per-reason fault counts: how many ANON, FILE, COW, SWAP, UNKNOWN.
    unsigned long long reason_cnt[REASON_KINDS];

    struct agg *next;       // Hash chain pointer
};

#define AGG_HASH_BITS 10
#define AGG_BUCKETS (1u << AGG_HASH_BITS)
static struct agg *agg_table[AGG_BUCKETS] = {0};

// Knuth multiplicative hash for PID -> bucket index.
static unsigned agg_hash(__u32 pid)
{
    return (pid * 2654435761u) >> (32 - AGG_HASH_BITS);
}

// Look up or create an aggregation bucket for the given PID.
static struct agg *agg_get(__u32 pid, const char comm[16])
{
    if (!g_per_pid) pid = 0;  // All events go to one global bucket
    unsigned h = agg_hash(pid);
    for (struct agg *a = agg_table[h]; a; a = a->next) {
        if (a->pid == pid) return a;
    }
    // Not found: create a new bucket.
    struct agg *a = calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->pid = pid;
    if (comm) memcpy(a->comm, comm, 16);
    a->min_lat_ns = ~0ULL;
    a->next = agg_table[h];
    agg_table[h] = a;
    return a;
}

// Reset all counters in a bucket (after printing).
static void agg_reset(struct agg *a)
{
    a->n = 0;
    a->sum_lat_ns = a->sum_llc = a->sum_insn = a->sum_e_est_J = 0.0L;
    a->min_lat_ns = ~0ULL;
    a->max_lat_ns = 0;
    memset(a->reason_cnt, 0, sizeof(a->reason_cnt));
}

// Print aggregation summary for one bucket and reset it.
static void agg_print_and_reset(const struct agg *a, unsigned N)
{
    if (a->n == 0) return;

    double mean_lat_us = (double)(a->sum_lat_ns / a->n) / 1000.0;
    double mean_llc    = (double)(a->sum_llc / a->n);
    double mean_insn   = (double)(a->sum_insn / a->n);
    double mean_e_J    = (double)(a->sum_e_est_J / a->n);
    double min_us      = (double)a->min_lat_ns / 1000.0;
    double max_us      = (double)a->max_lat_ns / 1000.0;

    // Build reason histogram string showing count and percentage for each type.
    char rhist[160];
    unsigned long long r0 = a->reason_cnt[0], r1 = a->reason_cnt[1],
                       r2 = a->reason_cnt[2], r3 = a->reason_cnt[3],
                       r4 = a->reason_cnt[4];
    double dn = (a->n > 0) ? (double)a->n : 1.0;
    snprintf(rhist, sizeof(rhist),
             "reasons {UNK=%llu(%.0f%%) ANON=%llu(%.0f%%) FILE=%llu(%.0f%%) "
             "COW=%llu(%.0f%%) SWAP=%llu(%.0f%%)}",
             r0, 100.0*r0/dn, r1, 100.0*r1/dn, r2, 100.0*r2/dn,
             r3, 100.0*r3/dn, r4, 100.0*r4/dn);

    if (g_per_pid)
        printf("[AGG pid=%-7u comm=%-16s N=%-6llu]  mean: latency=%9.3f us  "
               "llc=%-8.1f  insn=%-10.1f  E_est=%10.6f J  "
               "(min/max %.3f/%.3f us)  %s\n",
               a->pid, a->comm, a->n, mean_lat_us, mean_llc, mean_insn,
               mean_e_J, min_us, max_us, rhist);
    else
        printf("[AGG global N=%-6llu]  mean: latency=%9.3f us  "
               "llc=%-8.1f  insn=%-10.1f  E_est=%10.6f J  "
               "(min/max %.3f/%.3f us)  %s\n",
               a->n, mean_lat_us, mean_llc, mean_insn,
               mean_e_J, min_us, max_us, rhist);

    (void)N;
}

// ------------------------------------------------------------------
// libbpf log callback
// ------------------------------------------------------------------
static int libbpf_pr(enum libbpf_print_level level, const char *fmt, va_list args)
{
    return vfprintf(stderr, fmt, args);
}

// ------------------------------------------------------------------
// Analytical energy estimation
// ------------------------------------------------------------------
// Estimate the energy consumed by a single page fault using:
//   E = P_static * t + k_insn * insn_retired + k_llc * llc_misses
//
// This model captures three components:
//   1. Static/leakage power (always being consumed, proportional to time)
//   2. Dynamic instruction energy (proportional to work done)
//   3. Memory subsystem energy (LLC misses trigger expensive DRAM accesses)
static double estimate_energy_joules(const struct event_t *e)
{
    double t_sec    = (double)e->delta_ns * 1e-9;
    double e_insn_J = (double)e->insn_retired * (g_einstr_pj * 1e-12);
    double e_llc_J  = (double)e->llc_misses   * (g_emiss_nj  * 1e-9);
    double e_static = g_pstatic_w * t_sec;
    return e_insn_J + e_llc_J + e_static;
}

// ------------------------------------------------------------------
// Ring buffer event handler
// ------------------------------------------------------------------
// Called by ring_buffer__poll() for each event submitted by the BPF program.
// Prints a per-event line and (if aggregation is enabled) accumulates into buckets.
static int handle_event(void *ctx, void *data, size_t len)
{
    (void)ctx;
    if (len < sizeof(struct event_t)) return 0;
    const struct event_t *e = data;

    // Compute derived metrics for display.
    double e_est_J = estimate_energy_joules(e);
    double t_us    = (double)e->delta_ns / 1000.0;

    // Estimate cycles and IPC assuming a fixed core frequency.
    // This is approximate (frequency may vary with DVFS) but useful for reporting.
    double cycles = (double)e->delta_ns * 1e-9 * (g_core_ghz * 1e9);
    double ipc    = (cycles > 0.0) ? ((double)e->insn_retired / cycles) : 0.0;

    const char *rstr = reason_to_str(e->reason);

    // Print one line per fault event.
    printf("%-16s pid=%-7u cgid=%-12llu tid=%-7u  reason=%-7s"
           "  cpu_enter=%-3d cpu_exit=%-3d migrated=%-1u"
           "  latency=%-10" PRIu64 " ns (%6.1f us)"
           "  llc=%-8" PRIu64 "  insn=%-10" PRIu64
           "  E_est=%10.6f J  (cycles~%.0f, IPC=%.2f)\n",
           e->comm, e->pid, e->cgroup, e->tid, rstr,
           e->cpu_enter, e->cpu_exit, e->migrated,
           (uint64_t)e->delta_ns, t_us,
           (uint64_t)e->llc_misses, (uint64_t)e->insn_retired,
           e_est_J, cycles, ipc);

    // If aggregation is disabled, we're done.
    if (g_agg_N == 0) return 0;

    // Accumulate into the appropriate bucket.
    struct agg *a = agg_get(g_per_pid ? e->pid : 0, e->comm);
    if (!a) return 0;
    if (a->n == 0 && g_per_pid) memcpy(a->comm, e->comm, sizeof(a->comm));

    a->n++;
    a->sum_lat_ns  += (long double)e->delta_ns;
    a->sum_llc     += (long double)e->llc_misses;
    a->sum_insn    += (long double)e->insn_retired;
    a->sum_e_est_J += (long double)e_est_J;

    if (e->delta_ns < a->min_lat_ns) a->min_lat_ns = e->delta_ns;
    if (e->delta_ns > a->max_lat_ns) a->max_lat_ns = e->delta_ns;

    // Update per-reason histogram.
    a->reason_cnt[clamp_reason_index(e->reason)]++;

    // When the window is full, print summary and reset.
    if (a->n >= g_agg_N) {
        agg_print_and_reset(a, g_agg_N);
        agg_reset(a);
    }
    return 0;
}

// ------------------------------------------------------------------
// Usage / help
// ------------------------------------------------------------------
static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Trace minor page faults system-wide using eBPF kprobes on handle_mm_fault().\n"
        "Reports per-fault latency, LLC misses, instructions retired, fault reason,\n"
        "and estimated energy consumption.\n"
        "\n"
        "Options:\n"
        "  --cgroup CGID     Only trace faults from this cgroup id (u64)\n"
        "  --cgroup-path P   Resolve cgroup path to inode number for filtering\n"
        "  --aggregate N     Print summary every N events (0 = per-event only)\n"
        "  --per-pid         Aggregate separately per PID (default: global)\n"
        "  --einspj PJ       Energy per instruction in pJ (default: 3.0)\n"
        "  --emissnj NJ      Energy per LLC miss in nJ (default: 100.0)\n"
        "  --pstaticw W      Static/core power in Watts (default: 1.0)\n"
        "  --coreghz GHZ     Assumed core frequency in GHz (default: 3.9)\n"
        "\n"
        "Examples:\n"
        "  sudo ./instrument                         # trace all faults, per-event output\n"
        "  sudo ./instrument --aggregate 1000        # print summary every 1000 faults\n"
        "  sudo ./instrument --aggregate 500 --per-pid  # per-PID summaries\n"
        "\n"
        "Note: Requires root (CAP_BPF + CAP_PERFMON). If LLC/insn counters show 0,\n"
        "disable the NMI watchdog: echo 0 > /proc/sys/kernel/nmi_watchdog\n",
        prog);
}

// ------------------------------------------------------------------
// Main: load BPF, set up PMU counters, poll ring buffer
// ------------------------------------------------------------------
int main(int argc, char **argv)
{
    struct instrument_bpf *skel = NULL;
    struct ring_buffer *rb = NULL;
    int err = 0;

    // ---- Parse command-line arguments ----
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

    // Set up libbpf logging (all messages go to stderr).
    libbpf_set_print(libbpf_pr);

    // Install signal handlers for graceful shutdown.
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    // ---- Step 1: Open BPF skeleton ----
    // This parses the embedded ELF (instrument.bpf.o) and prepares
    // all programs and maps for loading.
    skel = instrument_bpf__open();
    if (!skel) { fprintf(stderr, "Failed to open BPF skeleton\n"); return 1; }

    // Number of CPUs for per-CPU perf event arrays.
    // Hardcoded to 32 for simplicity; could use libbpf_num_possible_cpus().
    int ncpus = 32;

    // ---- Step 2: Configure maps before loading ----
    // PERF_EVENT_ARRAY maps must be sized to the number of CPUs before load.
    bpf_map__set_max_entries(skel->maps.pe_llc,  ncpus);
    bpf_map__set_max_entries(skel->maps.pe_insn, ncpus);

    // Set the cgroup filter in BPF read-only data (compiled into the program).
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

    // ---- Step 3: Load BPF programs into the kernel ----
    // The verifier checks all programs at this point. If any program
    // fails verification, the entire load fails.
    if ((err = instrument_bpf__load(skel))) {
        fprintf(stderr, "Failed to load BPF skeleton: %d\n", err);
        goto out_free;
    }

    // ---- Step 4: Set up per-CPU hardware performance counters ----
    // We open two perf events per CPU:
    //   1. PERF_COUNT_HW_CACHE_MISSES  (LLC misses)
    //   2. PERF_COUNT_HW_INSTRUCTIONS  (retired instructions)
    //
    // Each counter is system-wide (pid=-1) for the specific CPU.
    // The file descriptor is stored in the BPF perf event array map,
    // allowing the kernel-side BPF program to call bpf_perf_event_read_value().
    //
    // NOTE: If the NMI watchdog is active, it may consume all available
    // PMU counter slots, causing perf_event_open() to fail. Disable with:
    //   echo 0 > /proc/sys/kernel/nmi_watchdog

    int fd_map_llc  = bpf_map__fd(skel->maps.pe_llc);
    int fd_map_insn = bpf_map__fd(skel->maps.pe_insn);

    int *fd_llc  = calloc(ncpus, sizeof(int));
    int *fd_insn = calloc(ncpus, sizeof(int));
    if (!fd_llc || !fd_insn) {
        fprintf(stderr, "calloc failed\n");
        err = -ENOMEM;
        goto out_free;
    }

    struct perf_event_attr attr = {0};
    attr.size = sizeof(attr);
    attr.disabled = 0;           // Start counting immediately
    attr.inherit = 0;            // Don't inherit to child tasks
    attr.exclude_kernel = 0;     // Count kernel-space events (we need this for faults)
    attr.exclude_user   = 0;     // Count user-space events too
    attr.exclude_hv     = 1;     // Exclude hypervisor
    attr.exclude_idle   = 0;     // Count during idle
    attr.pinned = 1;             // Pin to this CPU (don't multiplex)
    attr.exclusive = 0;
    attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;

    for (int cpu = 0; cpu < ncpus; cpu++) {
        // Open LLC miss counter for this CPU (system-wide: pid=-1).
        attr.type = PERF_TYPE_HARDWARE;
        attr.config = PERF_COUNT_HW_CACHE_MISSES;
        fd_llc[cpu] = perf_event_open(&attr, -1, cpu, -1, 0);
        if (fd_llc[cpu] < 0) {
            fprintf(stderr, "perf_event_open(LLC, cpu=%d): %s\n", cpu, strerror(errno));
            err = -errno;
            goto out_free;
        }
        // Store fd in BPF map so kernel-side can read it.
        if (bpf_map_update_elem(fd_map_llc, &cpu, &fd_llc[cpu], BPF_ANY) != 0) {
            fprintf(stderr, "map pe_llc cpu=%d: %s\n", cpu, strerror(errno));
            err = -errno;
            goto out_free;
        }

        // Open instruction counter for this CPU.
        attr.type = PERF_TYPE_HARDWARE;
        attr.config = PERF_COUNT_HW_INSTRUCTIONS;
        fd_insn[cpu] = perf_event_open(&attr, -1, cpu, -1, 0);
        if (fd_insn[cpu] < 0) {
            fprintf(stderr, "perf_event_open(INSN, cpu=%d): %s\n", cpu, strerror(errno));
            err = -errno;
            goto out_free;
        }
        if (bpf_map_update_elem(fd_map_insn, &cpu, &fd_insn[cpu], BPF_ANY) != 0) {
            fprintf(stderr, "map pe_insn cpu=%d: %s\n", cpu, strerror(errno));
            err = -errno;
            goto out_free;
        }
    }

    // ---- Step 5: Attach BPF programs to kernel functions ----
    // This creates the kprobe and kretprobe on handle_mm_fault().
    // From this point on, every page fault on the system will trigger our BPF code.
    if ((err = instrument_bpf__attach(skel))) {
        fprintf(stderr, "Failed to attach BPF programs: %d\n", err);
        goto out_free;
    }

    // ---- Step 6: Print configuration and start polling ----
    if (g_agg_N)
        printf("Aggregation: %s, window N=%u\n",
               g_per_pid ? "per-PID" : "global", g_agg_N);

    printf("Model: E = P_static*t + k_insn*insn + k_llc*misses  "
           "(P_static=%.3f W, k_insn=%.3f pJ, k_llc=%.3f nJ, core=%.2f GHz)\n",
           g_pstatic_w, g_einstr_pj, g_emiss_nj, g_core_ghz);

    if (g_filter_cgid)
        printf("Filtering to cgroup id: %llu%s%s\n",
               (unsigned long long)g_filter_cgid,
               g_cgroup_path ? " (path: " : "",
               g_cgroup_path ? g_cgroup_path : "");
    if (g_cgroup_path) printf(")\n");

    // Create ring buffer consumer. handle_event() is called for each event.
    rb = ring_buffer__new(bpf_map__fd(skel->maps.events), handle_event, NULL, NULL);
    if (!rb) {
        err = -errno;
        fprintf(stderr, "ring_buffer__new failed: %d\n", err);
        goto out_free;
    }

    printf("Tracing MINOR page faults%s... latency + LLC + INSN + reason. Ctrl-C to stop.\n",
           g_filter_cgid ? " (filtered)" : "");

    // Poll the ring buffer until Ctrl-C. Timeout of 250ms allows
    // periodic checking of the exiting flag.
    while (!exiting) {
        err = ring_buffer__poll(rb, 250 /* ms */);
        if (err == -EINTR) break;   // Signal received
        if (err < 0) {
            fprintf(stderr, "ring_buffer__poll failed: %d\n", err);
            break;
        }
    }

    // ---- Cleanup: print any partial aggregation windows ----
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
    // Perf event fds are closed automatically on process exit.
    return err != 0;
}
