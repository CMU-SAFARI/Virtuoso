/*
 * @kanellok: MimicOS as a library, for simulators other than Sniper.
 *
 * MimicOS also exists compiled into Sniper, and as a standalone process
 * driven by magic instructions over SDE.  Both are tied to Sniper.  This
 * is not: nothing crosses the boundary but integers and POD structs, so
 * an adapter needs this header and nothing else.  gem5 and ChampSim use
 * it to hand MimicOS physical allocation, the page tables and the
 * minor-fault cost.
 *
 * Not thread safe.  Serialise if the host translates from more than one
 * thread, as the standalone kernel does with m_alloc_mutex.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mimicos {

/* ------------------------------------------------------------------ */
/*  Request / response types                                           */
/* ------------------------------------------------------------------ */

enum class AccessType : uint8_t { READ = 0, WRITE = 1, EXEC = 2 };

enum class MappingType : uint8_t { ANON = 0, FILE_BACKED = 1, SHARED = 2 };

/* One page-table entry touched during a walk. */
struct PtwStep {
    /* pt_levels is the root, 1 is the last table.  ChampSim's numbering. */
    uint32_t level     = 0;
    /* Distance from the root.  depth == pt_levels - level. */
    uint32_t depth     = 0;
    /* Byte address of the entry: frame_ppn * 4096 + index * 8. */
    uint64_t pte_paddr = 0;
    /* This entry completes the translation. */
    bool     is_leaf   = false;
};

struct FaultRequest {
    uint64_t    vaddr          = 0;
    /** Address-space id.  ChampSim passes `cpu_num`, gem5 passes the pid. */
    uint32_t    asid           = 0;
    uint32_t    core_id        = 0;
    AccessType  access         = AccessType::WRITE;
    MappingType mapping        = MappingType::ANON;
    bool        is_instruction = false;
};

struct Translation {
    /*
     * The 4K frame holding vaddr.  Already offset into a large page, so
     * the caller can use it as is.  Same convention as Sniper's MMU.
     */
    uint64_t ppn              = 0;
    /* 12, 21 or 30. */
    uint32_t page_size_bits   = 12;
    /* This call had to resolve a fault. */
    bool     faulted          = false;
    /* Cost of that fault; 0 if there was none. */
    uint64_t fault_latency_ns = 0;
    /* False if there was no physical memory left. */
    bool     ok               = false;
};

struct KernelStats {
    uint64_t translations       = 0;
    uint64_t minor_faults       = 0;
    uint64_t huge_page_faults   = 0;
    uint64_t pt_frames_allocated = 0;
    uint64_t data_frames_allocated = 0;
    uint64_t failed_allocations = 0;
    uint64_t total_fault_latency_ns = 0;
    /*
     * Frames handed out twice.  Must stay 0.  Anything else means two
     * unrelated pages share physical memory, which shrinks the working
     * set and flatters every cache number downstream, and it does so
     * without crashing anything.
     */
    uint64_t aliased_frames = 0;
};

/* ------------------------------------------------------------------ */
/*  Configuration                                                      */
/* ------------------------------------------------------------------ */

struct KernelConfig {
    /* --- physical memory allocator --- */
    /** "baseline", "reserve_thp" or "linux_buddy_anon". */
    std::string allocator            = "reserve_thp";
    uint64_t    memory_size_mb       = 16384;
    uint64_t    kernel_size_mb       = 512;
    int         max_order            = 12;
    std::string fragmentation_type   = "largepage";
    double      fragmentation        = 0.0;
    double      promotion_threshold  = -1.0;

    /* --- page table geometry --- */
    /* 4 for x86-64, 5 for LA57.  Must match the host. */
    int      pt_levels            = 4;
    /* Only 4096 is supported. */
    uint64_t page_table_page_size = 4096;

    /*
     * Narrate every allocation, as the standalone kernel does.  Off here:
     * it drowns the host's own output and costs real time on the fault
     * path.  Turn on with [mimicos] verbose when debugging.
     */
    bool verbose = false;

    /* --- minor-fault cost model (see mm/fault_phases.h) --- */
    uint64_t vma_lookup_base_ns          = 0;
    uint64_t pt_page_alloc_cost_ns       = 0;
    uint64_t phys_page_alloc_cost_ns     = 0;
    uint64_t zero_fill_cost_ns           = 0;
    uint64_t pte_install_cost_ns         = 0;
    uint64_t lock_contention_penalty_ns  = 0;
    uint64_t cache_tlb_perturb_penalty_ns = 0;
    uint64_t numa_penalty_ns             = 0;

    /*
      * Parse the same INI the standalone kernel takes.  Reads
      * [allocator], [pmem_alloc], [page_table] and [fault_calibration].
      */
    static bool from_ini(const std::string& path, KernelConfig* out, std::string* err);
};

/* ------------------------------------------------------------------ */
/*  Kernel                                                             */
/* ------------------------------------------------------------------ */

class Kernel {
public:
    static std::unique_ptr<Kernel> create(const KernelConfig& cfg, std::string* err);
    /* from_ini() then create(). */
    static std::unique_ptr<Kernel> create_from_ini(const std::string& ini_path, std::string* err);

    ~Kernel();

    Kernel(const Kernel&)            = delete;
    Kernel& operator=(const Kernel&) = delete;

    /* -- translation -------------------------------------------------- */

    /*
     * Resolve req.vaddr, allocating a frame and installing page table
     * entries if it is not mapped yet.  The only call that allocates.
     */
    Translation translate(const FaultRequest& req);

    /* Look up without faulting.  False if not mapped. */
    bool probe(uint32_t asid, uint64_t vaddr,
               uint64_t* ppn, uint32_t* page_size_bits) const;

    /*
     * Address of the page table entry read at `level` on the walk for
     * req.vaddr.  Missing interior tables are created here, so a host
     * walker can read real entries before the leaf exists.
     */
    bool pte_address(const FaultRequest& req, uint32_t level, PtwStep* out);

    /* Every entry on the walk, root first. */
    std::vector<PtwStep> walk(const FaultRequest& req);

    /* Address of the root table. */
    uint64_t root_table_paddr(uint32_t asid);

    /*
     * Level the walk ends at: 1 for 4K, 2 for 2M, 3 for 1G.  Unmapped
     * pages read as 1, since nothing yet says the walk is short.
     */
    uint32_t leaf_level(uint32_t asid, uint64_t vaddr) const;

    /* -- cost model ---------------------------------------------------- */

    /* Cost of a fault of this shape. */
    uint64_t minor_fault_latency_ns(const FaultRequest& req,
                                    uint32_t page_size_bits,
                                    bool pt_alloc_needed) const;

    /* -- introspection ------------------------------------------------- */

    uint32_t    pt_levels() const;
    KernelStats stats() const;
    /* Free 4K frames left in the data pool. */
    uint64_t    available_frames() const;

    /*
     * Log every mapping decision to a CSV.  Used to check that a host
     * simulator drives MimicOS the same way the library does on its own.
     */
    bool enable_mapping_trace(const std::string& path, std::string* err);
    void flush_mapping_trace();

private:
    Kernel();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mimicos
