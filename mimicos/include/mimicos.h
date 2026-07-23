/**
 * @file mimicos.h
 * @brief MimicOS - Userspace Memory Management OS
 *
 * Lightweight imitation-based OS kernel. Handles physical memory allocation,
 * page fault servicing, huge page management, swap, and OS daemon threads
 * (kcompactd, khugepaged, kswapd).
 *
 * When running under SDE/PIN, POSIX threads created here are instrumented
 * and simulated by the architectural simulator (Sniper/Ramulator2).
 */
#pragma once
#include <string>
#include <iostream>
#include <fstream>
#include <pthread.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include <deque>
#include "INIReader.h"
#include "memory_management/physical_memory_allocators/physical_memory_allocator.h"
#include "memory_management/kcompactd.h"
#include "memory_management/khugepaged.h"
#include "memory_management/kswapd.h"
#include "memory_management/hugetlbfs.h"
#include "memory_management/swap_cache.h"
#include "globals.h"
#include "mm/fault_phases.h"
#include "mm/fault_replay.h"
#include "mm/fast_fault_profile.h"
#include "mm/shared_region.h"
#include "mm/scheduler/scheduler.h"
#include "mm/kernel_arena.h"
#include "mm/proc_vma.h"

// Forward declarations
class INIReader;
class MetricsRegistry;
class PhysicalMemoryAllocator;
struct mm_package;

// ============================================================================
// Userspace policies (no Sniper stats, stdout/file logging)
// ============================================================================
namespace Virtuoso {

namespace Kcompactd {
    struct NoMetricsPolicy {
        template <typename T> void on_init(T*) { std::cout << "[Virtuoso] kcompactd initialized" << std::endl; }
        void log(const std::string& msg) const { std::cout << msg << std::endl; }
    };
}

namespace Khugepaged {
    struct NoMetricsPolicy {
        template <typename T> void on_init(T*) { std::cout << "[Virtuoso] khugepaged initialized" << std::endl; }
        void log(const std::string& msg) const { std::cout << msg << std::endl; }
    };
}

namespace Kswapd {
    struct NoMetricsPolicy {
        template <typename T> void on_init(T*) { std::cout << "[Virtuoso] kswapd initialized" << std::endl; }
        void log(const std::string& msg) const { std::cout << msg << std::endl; }
    };
}

namespace HugeTLBfs {
    struct NoMetricsPolicy {
        template <typename T> void on_init(T*) { std::cout << "[Virtuoso] HugeTLBfs initialized" << std::endl; }
        void log(const std::string& msg) const { std::cout << msg << std::endl; }
    };
}

namespace SwapCache {
    struct NoMetricsPolicy {
        template <typename T> void on_init(T*) { std::cout << "[Virtuoso] SwapCache initialized" << std::endl; }
        void log(const std::string& msg) const { std::cout << msg << std::endl; }
    };
}

} // namespace Virtuoso

// Type aliases for userspace
using VirtuosoKcompactd  = ::Kcompactd<Virtuoso::Kcompactd::NoMetricsPolicy>;
using VirtuosoKhugepaged = ::Khugepaged<Virtuoso::Khugepaged::NoMetricsPolicy>;
using VirtuosoKswapd     = ::Kswapd<Virtuoso::Kswapd::NoMetricsPolicy>;
using VirtuosoHugeTLBfs  = ::HugeTLBfs<Virtuoso::HugeTLBfs::NoMetricsPolicy>;
using VirtuosoSwapCache  = ::SwapCache<Virtuoso::SwapCache::NoMetricsPolicy>;

struct Message{
    int argc;
    uint64_t *argv;
};

class MimicOS {
    private:

        INIReader* reader;
        MetricsRegistry* m_stats;
        PhysicalMemoryAllocator* physical_memory_allocator;
        mm_package* mmpackage;

        // OS Daemons (POSIX threads, instrumented by SDE)
        VirtuosoKcompactd*  m_kcompactd  = nullptr;
        VirtuosoKhugepaged* m_khugepaged = nullptr;
        VirtuosoKswapd*     m_kswapd     = nullptr;

        // OS Services
        std::unique_ptr<VirtuosoHugeTLBfs> m_hugetlbfs;
        std::unique_ptr<VirtuosoSwapCache> m_swap_cache;

        // Fault phase instrumentation & calibration
        FaultMetrics         m_fault_metrics;
        FaultCalibrationParams m_fault_calibration;

        // Fast-path fault replay engine (three-mode replay cache — legacy)
        FaultReplayEngine    m_fault_replay;

        // Train-then-replay registry (Phase 1/2 online profile).  Moves
        // each fingerprint through UNKNOWN -> TRAINING -> TRAINED/ABANDONED
        // based on per-fault cycle samples.  Phase 3 turns TRAINED entries
        // into SimFastFault magic shipments; Phase 2 still runs detailed
        // on every fault and only accumulates statistics.
        FastFaultRegistry    m_fast_fault_registry;

        /* Phase 9 predictor: last-fault allocator fast-path class
           (0=miss, 1=hit, 2=unknown).  Read at the pre-fault
           fingerprint-build site when policy.hierarchical_split_enabled
           is true; updated at the post-fault recording site from the
           allocator's last_alloc_fastpath field.  Initialised to 2
           (unknown) so the very first fault on each profile lands in
           the "unknown" bucket until the second fault reclassifies. */
        uint8_t              m_last_pcp_class = 2;

        // Shared memory region manager
        SharedRegionManager  m_shared_regions;

        // Scheduler
        Scheduler            m_scheduler;

        // Kernel arena (slab for MimicOS internal metadata,
        // backed by host mmap + accounted to simulated kernel reserve).
        KernelArena          m_kernel_arena;

        // ---- Stage D (Apr 17 2026): per-core state ----
        // Each core gets a kernel pthread + one (or more) live apps.
        // Per-core data is isolated; shared state (allocator) is mutex-protected.
        struct PerCoreState {
            int              core_id    = -1;
            pid_t            child_pid  = 0;
            ProcVmaTree      vma_tree;
            pthread_t        thread     = 0;     // 0 for main thread (core 0)
            bool             active     = false;
            /* Stage 2E (Apr 18 2026): per-core run-queue of app_thread_id's
               this kernel pthread is responsible for.  Cooperative v1 — no
               preemption: we only swap apps on fault-handled / thread_exit
               boundaries.  A new_thread event appends; a thread_exit event
               pops.  The currently-running app is implicitly at the front. */
            std::deque<uint64_t> runqueue;
        };
        int                  m_num_cores = 1;
        int                  m_num_live_apps = 0;  // set in boot() from m_app_paths
        std::vector<PerCoreState> m_cores;
        std::mutex           m_alloc_mutex;  // protects physical_memory_allocator

        // Legacy single-core aliases (kept for backward compat with trace-based path)
        pid_t                m_child_pid = 0;
        ProcVmaTree          m_app_vma_tree;

        static MimicOS* instance;
    public:

        // Path to the output file to keep track of statistics
        std::string path_to_outputFile;
        // Path to the application file
        std::string path_to_app;
        // Path to the configuration file
        std::string path_to_configFile;

        MimicOS(std::string configurationFile, std::string outputFile, std::string appFile);
        ~MimicOS();
        void initHandlers();
        void boot();
        void setupSharedMemory();
        void start_application();
        /* Stage 2E (Apr 18 2026): core_id tells which kernel pthread we are
           (for per-core runqueue + message slot).  has_initial_app tells
           poll_for_signal whether to do the legacy SimContextSwitch at the
           top (true = a live app is already injected into this core's
           TraceThread, false = idle kernel waiting to be scheduled work). */
        void poll_for_signal(int core_id = 0, bool has_initial_app = true);

        /* Stage A (Apr 15 2026): spawn a real binary as a userspace process
           under PIN+SDE.  Uses fork() + execve(); PIN's follow-child handler
           picks up the child, record-trace allocates a new app_id, Sniper
           creates a new TraceThread with its reader_kernel bound to our
           shared kernel SIFT.  Faults from the spawned app flow through
           MimicOS's poll_for_signal just like pre-recorded traces. */
        /* Returns child pid on success, -1 on failure. */
        pid_t spawn_live_application(const std::string& binary,
                                      const std::vector<std::string>& argv);

        // Stage D: per-core kernel worker entry point.
        // Called from pthread_create for cores 1..N-1; main thread runs core 0.
        void kernel_worker(int core_id);
        static void* kernel_worker_trampoline(void* arg);

        // Per-core app paths (from [apps] INI section or cmdline fallback).
        std::vector<std::string> m_app_paths;

        // Daemon management
        void start_daemons();
        void stop_daemons();

        mm_package* get_mm_package(){ return mmpackage; }
        PhysicalMemoryAllocator* get_physical_memory_allocator(){ return physical_memory_allocator; }

        /* Thread-safe allocator wrappers (Stage D: multiple kernel pthreads). */
        std::pair<uint64_t, uint64_t> locked_allocate(uint64_t bytes, uint64_t va, uint64_t core_id) {
            std::lock_guard<std::mutex> lk(m_alloc_mutex);
            return physical_memory_allocator->allocate(bytes, va, core_id);
        }
        uint64_t locked_handle_pt_alloc(uint64_t bytes) {
            std::lock_guard<std::mutex> lk(m_alloc_mutex);
            return physical_memory_allocator->handle_page_table_allocations(bytes);
        }

        // OS Services
        VirtuosoHugeTLBfs* getHugeTLBfs() { return m_hugetlbfs.get(); }
        VirtuosoSwapCache* getSwapCache() { return m_swap_cache.get(); }
        bool isHugeTLBfsEnabled() const { return m_hugetlbfs != nullptr && m_hugetlbfs->isEnabled(); }

        // Function to get the instance of the MimicOS class
        static MimicOS* getMimicOS(std::string configurationFile, std::string outputFile, std::string appFile){
            if(instance == NULL){
                instance = new MimicOS(configurationFile, outputFile, appFile);
            }
            return instance;
        }

        static MimicOS* getMimicOS(){
            return instance;
        }

        void handleDeallocate();
        void handlePrintAllocator();
        void handleInitRandom();

        // Fault instrumentation
        FaultMetrics& getFaultMetrics() { return m_fault_metrics; }
        const FaultMetrics& getFaultMetrics() const { return m_fault_metrics; }
        FaultCalibrationParams& getFaultCalibration() { return m_fault_calibration; }
        const FaultCalibrationParams& getFaultCalibration() const { return m_fault_calibration; }
        FaultReplayEngine& getFaultReplay() { return m_fault_replay; }
        const FaultReplayEngine& getFaultReplay() const { return m_fault_replay; }
        FastFaultRegistry& getFastFaultRegistry() { return m_fast_fault_registry; }
        const FastFaultRegistry& getFastFaultRegistry() const { return m_fast_fault_registry; }
        SharedRegionManager& getSharedRegions() { return m_shared_regions; }
        Scheduler& getScheduler() { return m_scheduler; }
        KernelArena& getKernelArena() { return m_kernel_arena; }
        ProcVmaTree& getAppVmaTree() { return m_app_vma_tree; }
        pid_t getChildPid() const { return m_child_pid; }
        void  setChildPid(pid_t p) { m_child_pid = p; }

};