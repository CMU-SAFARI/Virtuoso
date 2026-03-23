// SPDX-License-Identifier: GPL-2.0
// instrument.bpf.c — Minor page fault latency + LLC misses + retired instructions + reason
// Changes vs your version:
//   * Adds reason classification via mm tracepoints: ANON, FILE, COW, SWAP.
//   * Stores reason in-flight per-thread and emits it with the event.

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

char LICENSE[] SEC("license") = "GPL";

// Optional cgroup filter: 0 = no filter
const volatile __u64 target_cgid = 0;

enum fault_reason {
    FR_UNKNOWN = 0,
    FR_ANON    = 1,  // do_anonymous_page (first-touch anonymous / zero page)
    FR_FILE    = 2,  // do_fault (file/shmem-backed)
    FR_COW     = 3,  // do_wp_page (copy-on-write)
    FR_SWAP    = 4,  // do_swap_page (anonymous swapped-in)
};

struct start_t {
    __u64 ts_ns;
    __u64 maj_before;
    __u64 llc_before;
    __u64 insn_before;
    __u64 cgroup;         // cgid at entry
    int   cpu;            // CPU at entry
    __u8  reason;         // enum fault_reason (set by mm tracepoints)
};

struct event_t {
    __u32 pid;            // TGID
    __u32 tid;            // PID (thread)
    __u64 cgroup;
    __u64 delta_ns;       // handler time
    __u64 llc_misses;     // LLC misses
    __u64 insn_retired;   // instructions
    __u8  reason;         // enum fault_reason
    int   cpu_enter;      // CPU at kprobe entry
    int   cpu_exit;       // CPU at kretprobe exit
    __u8  migrated;       // 1 if cpu_enter != cpu_exit
    char  comm[16];
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, __u32);                // tid
    __type(value, struct start_t);
    __uint(max_entries, 32768);
} starts SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 24);      // 16 MiB
} events SEC(".maps");

// Perf arrays populated by userspace (per-CPU fds)
struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(int));
    __uint(value_size, sizeof(int));
    __uint(max_entries, 0);
} pe_llc SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(int));
    __uint(value_size, sizeof(int));
    __uint(max_entries, 0);
} pe_insn SEC(".maps");

static __always_inline bool pass_cgroup_filter(void)
{
    if (!target_cgid) return true;
    __u64 cg = bpf_get_current_cgroup_id();
    return cg == target_cgid;
}

/* ---------- Entry/Exit Helpers ---------- */

static __always_inline int on_enter(void)
{
    if (!pass_cgroup_filter())
        return 0;

    __u64 pid_tgid = bpf_get_current_pid_tgid();
    __u32 tid  = (__u32)pid_tgid;

    struct start_t st = {};
    
    st.cgroup = bpf_get_current_cgroup_id();
    st.reason = FR_UNKNOWN; // will be updated by mm tracepoints during the fault

    struct task_struct *task = (struct task_struct *)bpf_get_current_task_btf();
    st.maj_before = BPF_CORE_READ(task, maj_flt);

    int cpu = bpf_get_smp_processor_id();
    st.cpu = cpu;

    struct bpf_perf_event_value v = {};

    if (bpf_perf_event_read_value(&pe_llc, cpu, &v, sizeof(v)) == 0)
        st.llc_before = v.counter;

    v.counter = v.enabled = v.running = 0;
    if (bpf_perf_event_read_value(&pe_insn, cpu, &v, sizeof(v)) == 0)
        st.insn_before = v.counter;
    st.ts_ns  = bpf_ktime_get_ns();
    bpf_map_update_elem(&starts, &tid, &st, BPF_ANY);

    return 0;
}

static __always_inline int on_exit(void *ctx)
{
    if (!pass_cgroup_filter())
        return 0;

    __u64 pid_tgid = bpf_get_current_pid_tgid();
    __u32 tgid = pid_tgid >> 32;
    __u32 tid  = (__u32)pid_tgid;

    struct start_t *stp = bpf_map_lookup_elem(&starts, &tid);
    if (!stp)
        return 0;

    struct task_struct *task = (struct task_struct *)bpf_get_current_task_btf();
    __u64 maj_after = BPF_CORE_READ(task, maj_flt);

    // Only report minor faults (major unchanged)
    if (maj_after == stp->maj_before) {
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

            int cpu = stp->cpu;
            struct bpf_perf_event_value v = {};

            if (bpf_perf_event_read_value(&pe_llc, cpu, &v, sizeof(v)) == 0 && v.counter >= stp->llc_before)
                e->llc_misses = v.counter - stp->llc_before;

            v.counter = v.enabled = v.running = 0;
            if (bpf_perf_event_read_value(&pe_insn, cpu, &v, sizeof(v)) == 0 && v.counter >= stp->insn_before)
                e->insn_retired = v.counter - stp->insn_before;

            bpf_get_current_comm(&e->comm, sizeof(e->comm));

            if (e->migrated) {
                e->llc_misses = 0;
                e->insn_retired = 0;
            }
            
            bpf_ringbuf_submit(e, 0);
        }
    }

    bpf_map_delete_elem(&starts, &tid);
    return 0;
}

/* ---------- Fault-path reason tagging via tracepoints ---------- */
/* These run during the same fault and update the in-flight record for the thread. */

static __always_inline void set_reason(enum fault_reason r)
{
    __u32 tid = (__u32)bpf_get_current_pid_tgid();
    struct start_t *stp = bpf_map_lookup_elem(&starts, &tid);
    if (stp) {
        stp->reason = r;
    }
}

// SEC("tracepoint/mm/do_anonymous_page")
// int TP_do_anonymous_page(struct trace_event_raw_mm_filemap *ctx)
// {
//     set_reason(FR_ANON);
//     return 0;
// }

// SEC("tracepoint/mm/do_fault")
// int TP_do_fault(struct trace_event_raw_mm_filemap *ctx)
// {
//     // file/shmem-backed
//     set_reason(FR_FILE);
//     return 0;
// }

// SEC("tracepoint/mm/do_wp_page")
// int TP_do_wp_page(struct trace_event_raw_mm_filemap *ctx)
// {
//     // copy-on-write
//     set_reason(FR_COW);
//     return 0;
// }

// SEC("tracepoint/mm/do_swap_page")
// int TP_do_swap_page(struct trace_event_raw_mm_filemap *ctx)
// {
//     // anonymous swapped-in
//     set_reason(FR_SWAP);
//     return 0;
// }

// /* ---------- Attach to handle_mm_fault for timing/metrics ---------- */

// /* Anonymous first-touch */
// SEC("kprobe/do_anonymous_page")
// int KP_do_anonymous_page(struct pt_regs *ctx)
// {
//     set_reason(FR_ANON);
//     return 0;
// }

// /* File/shmem-backed page fault via ->fault path */
// SEC("kprobe/__do_fault")
// int KP___do_fault(struct pt_regs *ctx)
// {
//     set_reason(FR_FILE);
//     return 0;
// }

// /* Fallback if __do_fault isn’t attachable */
// SEC("kprobe/do_fault")
// int KP_do_fault(struct pt_regs *ctx) { set_reason(FR_FILE); return 0; }

// /* Copy-on-write on write-protected page */
// SEC("kprobe/do_wp_page")
// int KP_do_wp_page(struct pt_regs *ctx)
// {
//     set_reason(FR_COW);
//     return 0;
// }

// /* Swap-in of anonymous page */
// SEC("kprobe/do_swap_page")
// int KP_do_swap_page(struct pt_regs *ctx)
// {
//     set_reason(FR_SWAP);
//     return 0;
// }

// SEC("kprobe/handle_pte_fault")
// int KP_handle_pte_fault(struct pt_regs *ctx)
// {
//     struct vm_fault *vmf = (void *)PT_REGS_PARM1(ctx);
//     struct vm_area_struct *vma = BPF_CORE_READ(vmf, vma);
//     unsigned int flags = 0;
//     BPF_CORE_READ_INTO(&flags, vmf, flags);

//     // Only set if still unknown, so specific probes (COW, NUMA, etc.) can override
//     __u32 tid = (__u32)bpf_get_current_pid_tgid();
//     struct start_t *stp = bpf_map_lookup_elem(&starts, &tid);
//     if (!stp || stp->reason != FR_UNKNOWN)
//         return 0;


//     // Heuristic: infer ANON vs FILE from VMA
//     void *vm_file = BPF_CORE_READ(vma, vm_file);
//     stp->reason = vm_file ? FR_FILE : FR_ANON;
//     return 0;
// }

SEC("kprobe/handle_mm_fault")
int BPF_KPROBE(kp_handle_mm_fault) { return on_enter(); }

SEC("kretprobe/handle_mm_fault")
int BPF_KRETPROBE(krp_handle_mm_fault) { return on_exit((void *)ctx); }

