// SPDX-License-Identifier: GPL-2.0
//
// instrument.bpf.c - eBPF kernel-space program for tracing Linux page faults.
//
// This BPF program attaches to the kernel's handle_mm_fault() function using
// kprobe/kretprobe to measure the latency, LLC misses, and retired instructions
// of every minor page fault on the system. It also classifies faults by type
// (anonymous, file-backed, COW, swap) using the VMA metadata available at
// fault entry time.
//
// === Architecture ===
//
//   User process                  Kernel                         This BPF program
//   -----------                  ------                         ----------------
//   load/store to               handle_mm_fault()          -->  kprobe: on_enter()
//   unmapped page               |                                 - record timestamp
//     |                         |-- do_anonymous_page()            - read LLC/insn PMU counters
//     |                         |   OR do_fault()                  - read vma->vm_file for reason
//     |                         |   OR do_wp_page()                - store in per-thread hash map
//     |                         |   OR do_swap_page()
//     |                         |                           -->  kretprobe: on_exit()
//     |                         v                                  - compute delta (time, LLC, insn)
//   page is mapped              return to user                    - filter: only report minor faults
//                                                                 - emit event to ring buffer
//
// === Data Flow ===
//
//   1. kprobe fires on handle_mm_fault() entry:
//      - Capture: timestamp, LLC miss counter, instruction counter, CPU id,
//        cgroup id, major fault count (to detect major faults later).
//      - Read vma->vm_file from handle_mm_fault's first argument to classify
//        the fault as ANON (vm_file == NULL) or FILE (vm_file != NULL).
//      - Store all of this in `starts` hash map, keyed by thread id (tid).
//
//   2. kretprobe fires on handle_mm_fault() return:
//      - Look up the entry record from `starts` by tid.
//      - Check if major fault count changed: if so, skip (we only want minor faults).
//      - Read current PMU counters and compute deltas.
//      - If the thread migrated CPUs between entry and exit, zero out PMU deltas
//        (per-CPU counters are not meaningful across CPUs).
//      - Reserve space in the ring buffer, fill in the event, and submit.
//
// === BPF Maps ===
//
//   starts     (HASH, tid -> start_t)        : in-flight fault records
//   events     (RINGBUF, 16 MiB)             : completed fault events sent to userspace
//   pe_llc     (PERF_EVENT_ARRAY, per-CPU)   : LLC miss counters (populated by userspace)
//   pe_insn    (PERF_EVENT_ARRAY, per-CPU)   : instruction counters (populated by userspace)
//
// === Fault Reason Classification ===
//
//   The fault reason is determined by reading handle_mm_fault's first argument,
//   which is a pointer to the faulting vm_area_struct (VMA). If vma->vm_file is
//   NULL, the VMA is anonymous (heap, stack, mmap MAP_ANONYMOUS); otherwise it
//   is file-backed (mmap'd file, shared library, shmem).
//
//   More specific classification (COW, SWAP) would require kprobes on internal
//   sub-handlers like do_wp_page() and do_swap_page(), but these are often
//   inlined by the compiler and not kprobe-able on production kernels.
//
// === PMU Counters ===
//
//   The tool reads hardware performance counters (LLC misses, retired instructions)
//   via BPF_MAP_TYPE_PERF_EVENT_ARRAY. Userspace opens per-CPU perf events and
//   stores the file descriptors in pe_llc and pe_insn maps. The BPF program reads
//   them with bpf_perf_event_read_value(). If hardware PMU counters are unavailable
//   (e.g., NMI watchdog consuming all counters, or running in a VM), the values
//   will be zero. Disable the NMI watchdog to free counters:
//     echo 0 > /proc/sys/kernel/nmi_watchdog
//
// === Cgroup Filtering ===
//
//   The optional `target_cgid` variable allows filtering to a specific cgroup.
//   If set to 0 (default), all tasks are traced. Userspace sets this before
//   loading the program via skel->rodata->target_cgid.

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

char LICENSE[] SEC("license") = "GPL";

// Optional cgroup filter (set by userspace before load). 0 = trace all tasks.
const volatile __u64 target_cgid = 0;

// Fault reason codes. Stored in event_t.reason and sent to userspace.
enum fault_reason {
    FR_UNKNOWN = 0,   // Could not classify (should not happen normally)
    FR_ANON    = 1,   // Anonymous fault: first touch on heap/stack/anon mmap
    FR_FILE    = 2,   // File-backed fault: mmap'd file, shared library, shmem
    FR_COW     = 3,   // Copy-on-write: write to shared/forked page (reserved)
    FR_SWAP    = 4,   // Swap-in: page was swapped out and is being read back (reserved)
};

// Per-thread entry record, stored in `starts` map during a fault.
// Created at kprobe entry, consumed and deleted at kretprobe exit.
struct start_t {
    __u64 ts_ns;        // Timestamp at fault entry (bpf_ktime_get_ns)
    __u64 maj_before;   // task->maj_flt at entry (to detect major faults)
    __u64 llc_before;   // LLC miss counter at entry (per-CPU PMU)
    __u64 insn_before;  // Instruction counter at entry (per-CPU PMU)
    __u64 cgroup;       // Cgroup id of the faulting task
    int   cpu;          // CPU at entry (for PMU delta: must match exit CPU)
    __u8  reason;       // Fault type (set from VMA at entry)
};

// Event sent to userspace via ring buffer for each completed minor fault.
struct event_t {
    __u32 pid;          // TGID (process id)
    __u32 tid;          // PID (thread id, confusingly named in Linux)
    __u64 cgroup;       // Cgroup id
    __u64 delta_ns;     // Fault handling latency in nanoseconds
    __u64 llc_misses;   // LLC misses during the fault (0 if migrated or unavailable)
    __u64 insn_retired; // Instructions retired during the fault (0 if migrated)
    __u8  reason;       // Fault type (enum fault_reason)
    int   cpu_enter;    // CPU at kprobe entry
    int   cpu_exit;     // CPU at kretprobe exit
    __u8  migrated;     // 1 if cpu_enter != cpu_exit (PMU deltas invalid)
    char  comm[16];     // Task name (e.g., "minor_fault_str")
};

// ----- BPF Maps -----

// Hash map: tid -> in-flight fault record.
// One entry per thread that is currently inside handle_mm_fault().
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, __u32);             // tid (Linux PID)
    __type(value, struct start_t);
    __uint(max_entries, 32768);     // Supports up to 32K concurrent faults
} starts SEC(".maps");

// Ring buffer: completed fault events streamed to userspace.
// 16 MiB is large enough for ~200K events before the ring wraps.
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 24);   // 16 MiB
} events SEC(".maps");

// Per-CPU perf event arrays for hardware PMU counters.
// Userspace opens perf_event_open() for each CPU and stores the fd here.
// The BPF program reads counters via bpf_perf_event_read_value().
struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(int));
    __uint(value_size, sizeof(int));
    __uint(max_entries, 0);         // Resized by userspace to ncpus
} pe_llc SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(int));
    __uint(value_size, sizeof(int));
    __uint(max_entries, 0);
} pe_insn SEC(".maps");

// ----- Helper Functions -----

// Check if the current task's cgroup matches the filter.
// Returns true if no filter is set (target_cgid == 0) or if it matches.
static __always_inline bool pass_cgroup_filter(void)
{
    if (!target_cgid) return true;
    __u64 cg = bpf_get_current_cgroup_id();
    return cg == target_cgid;
}

// ----- Kprobe Entry: handle_mm_fault() -----
//
// Called when any thread enters handle_mm_fault(). We capture:
//   - Monotonic timestamp (for latency measurement)
//   - Hardware PMU counters (LLC misses, instructions retired)
//   - Major fault count (to filter out major faults in the exit probe)
//   - VMA type (anonymous vs file-backed, from handle_mm_fault's first arg)
//
// The VMA pointer is the first argument to handle_mm_fault() on Linux 5.x:
//   vm_fault_t handle_mm_fault(struct vm_area_struct *vma,
//                              unsigned long address,
//                              unsigned int flags,
//                              struct pt_regs *regs);
//
// We classify the fault by checking vma->vm_file:
//   - NULL  -> anonymous VMA (heap, stack, MAP_ANONYMOUS)
//   - !NULL -> file-backed VMA (mmap'd file, shared library, shmem/tmpfs)

static __always_inline int on_enter(__u8 reason)
{
    if (!pass_cgroup_filter())
        return 0;

    __u64 pid_tgid = bpf_get_current_pid_tgid();
    __u32 tid  = (__u32)pid_tgid;

    struct start_t st = {};

    st.cgroup = bpf_get_current_cgroup_id();
    st.reason = reason;

    // Read task->maj_flt to detect major faults later.
    // bpf_get_current_task_btf() returns a BTF-typed pointer to current task_struct.
    struct task_struct *task = (struct task_struct *)bpf_get_current_task_btf();
    st.maj_before = BPF_CORE_READ(task, maj_flt);

    // Record the CPU so we can detect migration at exit.
    int cpu = bpf_get_smp_processor_id();
    st.cpu = cpu;

    // Read hardware PMU counters (LLC misses and instructions retired).
    // These are per-CPU counters opened by userspace and stored in pe_llc/pe_insn maps.
    // bpf_perf_event_read_value() reads the counter for the specified CPU.
    struct bpf_perf_event_value v = {};

    if (bpf_perf_event_read_value(&pe_llc, cpu, &v, sizeof(v)) == 0)
        st.llc_before = v.counter;

    v.counter = v.enabled = v.running = 0;
    if (bpf_perf_event_read_value(&pe_insn, cpu, &v, sizeof(v)) == 0)
        st.insn_before = v.counter;

    // Timestamp last (minimizes instrumentation skew on the latency measurement).
    st.ts_ns = bpf_ktime_get_ns();

    // Store in hash map for the exit probe to find.
    bpf_map_update_elem(&starts, &tid, &st, BPF_ANY);

    return 0;
}

// ----- Kretprobe Exit: handle_mm_fault() -----
//
// Called when handle_mm_fault() returns. We compute deltas (time, LLC, insn),
// filter to minor faults only, and emit an event to the ring buffer.
//
// Minor vs major fault detection:
//   If task->maj_flt changed between entry and exit, a major fault occurred
//   (disk I/O was needed). We skip these because their latency is dominated
//   by I/O, not the page fault handler itself.
//
// CPU migration handling:
//   Per-CPU PMU counters are only meaningful if the thread stayed on the same
//   CPU for the entire fault. If it migrated (preempted and rescheduled on a
//   different CPU), the LLC/insn deltas are garbage, so we zero them out and
//   set the `migrated` flag.

static __always_inline int on_exit(void *ctx)
{
    if (!pass_cgroup_filter())
        return 0;

    __u64 pid_tgid = bpf_get_current_pid_tgid();
    __u32 tgid = pid_tgid >> 32;   // Process id (TGID)
    __u32 tid  = (__u32)pid_tgid;   // Thread id (PID)

    // Look up the entry record we stored in on_enter().
    struct start_t *stp = bpf_map_lookup_elem(&starts, &tid);
    if (!stp)
        return 0;  // No matching entry (e.g., cgroup filter skipped it)

    // Check if this was a major fault (task->maj_flt incremented).
    struct task_struct *task = (struct task_struct *)bpf_get_current_task_btf();
    __u64 maj_after = BPF_CORE_READ(task, maj_flt);

    // Only report minor faults (major fault count unchanged).
    if (maj_after == stp->maj_before) {
        // Reserve space in the ring buffer for one event.
        struct event_t *e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
        if (e) {
            e->pid = tgid;
            e->tid = tid;
            e->cgroup = stp->cgroup;
            e->delta_ns = bpf_ktime_get_ns() - stp->ts_ns;
            e->llc_misses = 0;
            e->insn_retired = 0;
            e->reason = stp->reason;
            e->cpu_enter = stp->cpu;
            e->cpu_exit  = bpf_get_smp_processor_id();
            e->migrated  = (e->cpu_enter != e->cpu_exit);

            // Compute PMU counter deltas (LLC misses and instructions).
            // We read from the ENTRY cpu's counter, since that's where
            // the "before" reading was taken.
            int cpu = stp->cpu;
            struct bpf_perf_event_value v = {};

            if (bpf_perf_event_read_value(&pe_llc, cpu, &v, sizeof(v)) == 0
                && v.counter >= stp->llc_before)
                e->llc_misses = v.counter - stp->llc_before;

            v.counter = v.enabled = v.running = 0;
            if (bpf_perf_event_read_value(&pe_insn, cpu, &v, sizeof(v)) == 0
                && v.counter >= stp->insn_before)
                e->insn_retired = v.counter - stp->insn_before;

            bpf_get_current_comm(&e->comm, sizeof(e->comm));

            // If the thread migrated CPUs, the PMU deltas are meaningless
            // (different physical counters). Zero them out.
            if (e->migrated) {
                e->llc_misses = 0;
                e->insn_retired = 0;
            }

            // Submit event to ring buffer (consumed by userspace handle_event()).
            bpf_ringbuf_submit(e, 0);
        }
    }

    // Clean up the entry record (fault is complete).
    bpf_map_delete_elem(&starts, &tid);
    return 0;
}

// ----- Kprobe/Kretprobe Attachment Points -----
//
// We attach to handle_mm_fault(), the top-level kernel function that handles
// all page faults (both user and kernel, major and minor). This is the
// standard entry point in mm/memory.c.
//
// The BPF_KPROBE macro extracts function arguments from pt_regs:
//   - First argument (RDI on x86_64): struct vm_area_struct *vma
//
// We read vma->vm_file to classify the fault type. Using bpf_probe_read_kernel
// because the vma pointer comes from a kprobe argument (scalar in the verifier's
// view), not a BTF-typed pointer.

SEC("kprobe/handle_mm_fault")
int BPF_KPROBE(kp_handle_mm_fault, struct vm_area_struct *vma)
{
    // Classify fault reason from the VMA before entering the generic path.
    // Anonymous VMAs (heap, stack, MAP_ANONYMOUS) have vm_file == NULL.
    // File-backed VMAs (mmap'd files, shared libs, shmem) have vm_file != NULL.
    __u8 reason = FR_UNKNOWN;
    if (vma) {
        void *vm_file = NULL;
        bpf_probe_read_kernel(&vm_file, sizeof(vm_file), &vma->vm_file);
        reason = vm_file ? FR_FILE : FR_ANON;
    }
    return on_enter(reason);
}

SEC("kretprobe/handle_mm_fault")
int BPF_KRETPROBE(krp_handle_mm_fault) { return on_exit((void *)ctx); }
