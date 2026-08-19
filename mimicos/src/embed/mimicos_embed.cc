/*
 * @kanellok: the only place the library ties MimicOS's internals -- allocator
 * factory, radix page table, fault cost model -- to mimicos::Kernel.
 * Must not include sim_api.h or any Sniper header beyond the allocator
 * core the standalone kernel already compiles.
 */
#include "mimicos_embed.h"

#include "fixed_types.h"
#include "globals.h"
#include "INIReader.h"
#include "embed/radix_pt_embed.h"
#include "mm/allocator_factory.h"
#include "mm/fault_phases.h"
#include "memory_management/physical_memory_allocators/physical_memory_allocator.h"

#include <cstdio>
#include <fstream>
#include <unordered_map>

namespace mimicos {

namespace {

/*
 * Cap on the pending frame memo.  Going over only blurs fault cost
 * attribution, never correctness, so clear instead of growing.
 */
constexpr size_t kMaxPendingPtEntries = 1u << 16;

inline uint64_t memo_key(uint32_t asid, uint64_t vpn)
{
    return (static_cast<uint64_t>(asid) << 48) ^ vpn;
}

FaultMappingType to_internal(MappingType m)
{
    switch (m) {
        case MappingType::FILE_BACKED: return FaultMappingType::FILE_BACKED;
        case MappingType::SHARED:      return FaultMappingType::SHARED;
        case MappingType::ANON:
        default:                       return FaultMappingType::ANON;
    }
}

FaultAccessType to_internal(AccessType a)
{
    switch (a) {
        case AccessType::READ: return FaultAccessType::READ;
        case AccessType::EXEC: return FaultAccessType::EXEC;
        case AccessType::WRITE:
        default:               return FaultAccessType::WRITE;
    }
}

FaultPageSize to_page_size(uint32_t bits)
{
    if (bits >= 30) return FaultPageSize::PAGE_1G;
    if (bits >= 21) return FaultPageSize::PAGE_2M;
    return FaultPageSize::PAGE_4K;
}

} // namespace

/* ------------------------------------------------------------------ */
/*  KernelConfig                                                       */
/* ------------------------------------------------------------------ */

bool KernelConfig::from_ini(const std::string& path, KernelConfig* out, std::string* err)
{
    if (out == nullptr) {
        if (err) *err = "KernelConfig::from_ini: null output";
        return false;
    }

    INIReader reader(path);
    if (reader.ParseError() != 0) {
        if (err) {
            *err = "KernelConfig::from_ini: cannot parse '" + path + "' (error at line "
                 + std::to_string(reader.ParseError()) + ")";
        }
        return false;
    }

    KernelConfig cfg;

    /* Same section names as the standalone kernel, so one file drives all. */
    cfg.allocator           = reader.Get("allocator", "memory_allocator", cfg.allocator);
    cfg.max_order           = static_cast<int>(reader.GetInteger("allocator", "max_order", cfg.max_order));

    cfg.memory_size_mb      = static_cast<uint64_t>(reader.GetInteger("pmem_alloc", "memory_size", static_cast<long>(cfg.memory_size_mb)));
    cfg.kernel_size_mb      = static_cast<uint64_t>(reader.GetInteger("pmem_alloc", "kernel_size", static_cast<long>(cfg.kernel_size_mb)));
    cfg.max_order           = static_cast<int>(reader.GetInteger("pmem_alloc", "max_order", cfg.max_order));
    cfg.fragmentation_type  = reader.Get("pmem_alloc", "frag_type", cfg.fragmentation_type);
    cfg.fragmentation       = reader.GetReal("pmem_alloc", "target_fragmentation", cfg.fragmentation);
    cfg.promotion_threshold = reader.GetReal("pmem_alloc", "threshold_for_promotion", cfg.promotion_threshold);

    /* Host-facing geometry, in a section the standalone kernel ignores. */
    cfg.verbose              = reader.GetBoolean("mimicos", "verbose", cfg.verbose);

    cfg.pt_levels            = static_cast<int>(reader.GetInteger("page_table", "levels", cfg.pt_levels));
    cfg.page_table_page_size = static_cast<uint64_t>(reader.GetInteger("page_table", "page_size", static_cast<long>(cfg.page_table_page_size)));

    cfg.vma_lookup_base_ns           = static_cast<uint64_t>(reader.GetInteger("fault_calibration", "vma_lookup_base", 0));
    cfg.pt_page_alloc_cost_ns        = static_cast<uint64_t>(reader.GetInteger("fault_calibration", "pt_page_alloc_cost", 0));
    cfg.phys_page_alloc_cost_ns      = static_cast<uint64_t>(reader.GetInteger("fault_calibration", "phys_page_alloc_cost", 0));
    cfg.zero_fill_cost_ns            = static_cast<uint64_t>(reader.GetInteger("fault_calibration", "zero_fill_cost", 0));
    cfg.pte_install_cost_ns          = static_cast<uint64_t>(reader.GetInteger("fault_calibration", "pte_install_cost", 0));
    cfg.lock_contention_penalty_ns   = static_cast<uint64_t>(reader.GetInteger("fault_calibration", "lock_contention_penalty", 0));
    cfg.cache_tlb_perturb_penalty_ns = static_cast<uint64_t>(reader.GetInteger("fault_calibration", "cache_tlb_perturb_penalty", 0));
    cfg.numa_penalty_ns              = static_cast<uint64_t>(reader.GetInteger("fault_calibration", "numa_penalty", 0));

    *out = cfg;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Kernel::Impl                                                       */
/* ------------------------------------------------------------------ */

struct Kernel::Impl {
    KernelConfig cfg;
    PhysicalMemoryAllocator* alloc = nullptr;
    FaultCalibrationParams   calib;
    KernelStats              stats;

    std::unordered_map<uint32_t, std::unique_ptr<EmbedRadixPageTable>> page_tables;

    /*
     * Interior frames pte_address() created for a VA that is not mapped
     * yet.  The next translate() of that VA takes them, so the fault
     * still pays for them even though the walk made them earlier.
     */
    std::unordered_map<uint64_t, uint32_t> pending_pt_frames;

    std::ofstream mapping_trace;
    uint64_t      mapping_seq = 0;

    /*
     * One bit per frame, to catch the allocator handing the same frame
     * out twice.  A 4 GB pool costs 128 KB, which is worth paying: this
     * failure is silent otherwise, and a stale or half-written
     * libmimicos.a has produced it before.
     */
    std::vector<uint64_t> frame_owned;

    bool frame_bit(uint64_t frame) const
    {
        const size_t word = frame >> 6;
        return word < frame_owned.size()
               && (frame_owned[word] >> (frame & 63)) & 1u;
    }

    void set_frame_bit(uint64_t frame)
    {
        const size_t word = frame >> 6;
        if (word < frame_owned.size())
            frame_owned[word] |= (uint64_t{1} << (frame & 63));
    }

    /* Returns how many of these frames were already owned. */
    uint64_t claim_frames(uint64_t base, uint64_t count)
    {
        uint64_t clashes = 0;
        for (uint64_t f = base; f < base + count; f++) {
            if (frame_bit(f))
                clashes++;
            set_frame_bit(f);
        }
        return clashes;
    }

    EmbedRadixPageTable* pt_for(uint32_t asid)
    {
        auto it = page_tables.find(asid);
        if (it != page_tables.end())
            return it->second.get();
        auto pt = std::unique_ptr<EmbedRadixPageTable>(
            new EmbedRadixPageTable(alloc, cfg.pt_levels,
                                    static_cast<int>(cfg.page_table_page_size / 8)));
        EmbedRadixPageTable* raw = pt.get();
        page_tables.emplace(asid, std::move(pt));
        return raw;
    }

    const EmbedRadixPageTable* pt_for_const(uint32_t asid) const
    {
        auto it = page_tables.find(asid);
        return it == page_tables.end() ? nullptr : it->second.get();
    }

    ~Impl()
    {
        if (mapping_trace.is_open())
            mapping_trace.close();
        page_tables.clear();
        delete alloc;
    }
};

/* ------------------------------------------------------------------ */
/*  Kernel                                                             */
/* ------------------------------------------------------------------ */

Kernel::Kernel() : m_impl(new Impl()) {}
Kernel::~Kernel() = default;

std::unique_ptr<Kernel> Kernel::create(const KernelConfig& cfg, std::string* err)
{
    if (cfg.pt_levels < 2 || cfg.pt_levels > 5) {
        if (err) *err = "MimicOS: pt_levels must be between 2 and 5, got "
                      + std::to_string(cfg.pt_levels);
        return nullptr;
    }
    if (cfg.page_table_page_size != 4096) {
        if (err) *err = "MimicOS: only 4096-byte page-table pages are supported, got "
                      + std::to_string(cfg.page_table_page_size);
        return nullptr;
    }
    if (cfg.kernel_size_mb >= cfg.memory_size_mb) {
        if (err) *err = "MimicOS: kernel_size (" + std::to_string(cfg.kernel_size_mb)
                      + " MB) must be smaller than memory_size ("
                      + std::to_string(cfg.memory_size_mb) + " MB)";
        return nullptr;
    }

    std::unique_ptr<Kernel> k(new Kernel());
    k->m_impl->cfg = cfg;

    /* Before the allocator is built: its constructor logs too. */
    mimicos_log::verbose = cfg.verbose;

    k->m_impl->alloc = AllocatorFactory::createAllocator(
        String(cfg.allocator.c_str()),
        static_cast<int>(cfg.memory_size_mb),
        cfg.max_order,
        static_cast<int>(cfg.kernel_size_mb),
        String(cfg.fragmentation_type.c_str()),
        cfg.promotion_threshold);

    if (k->m_impl->alloc == nullptr) {
        if (err) *err = "MimicOS: unknown allocator '" + cfg.allocator
                      + "' (supported: baseline, reserve_thp, linux_buddy_anon)";
        return nullptr;
    }

    /* Same boot step the standalone kernel does before serving faults. */
    if (cfg.fragmentation > 0.0)
        k->m_impl->alloc->fragment_memory(cfg.fragmentation);

    const uint64_t frames = cfg.memory_size_mb * 1024ull * 1024ull / 4096ull;
    k->m_impl->frame_owned.assign((frames + 63) / 64, 0);

    FaultCalibrationParams& c = k->m_impl->calib;
    c.vma_lookup_base_ns           = cfg.vma_lookup_base_ns;
    c.pt_page_alloc_cost_ns        = cfg.pt_page_alloc_cost_ns;
    c.phys_page_alloc_cost_ns      = cfg.phys_page_alloc_cost_ns;
    c.zero_fill_cost_ns            = cfg.zero_fill_cost_ns;
    c.pte_install_cost_ns          = cfg.pte_install_cost_ns;
    c.lock_contention_penalty_ns   = cfg.lock_contention_penalty_ns;
    c.cache_tlb_perturb_penalty_ns = cfg.cache_tlb_perturb_penalty_ns;
    c.numa_penalty_ns              = cfg.numa_penalty_ns;

    return k;
}

std::unique_ptr<Kernel> Kernel::create_from_ini(const std::string& ini_path, std::string* err)
{
    KernelConfig cfg;
    if (!KernelConfig::from_ini(ini_path, &cfg, err))
        return nullptr;
    return create(cfg, err);
}

uint32_t Kernel::pt_levels() const { return static_cast<uint32_t>(m_impl->cfg.pt_levels); }

KernelStats Kernel::stats() const { return m_impl->stats; }

uint64_t Kernel::available_frames() const
{
    /* No uniform free-page count on the allocator, so report what we counted. */
    const uint64_t total_frames = (m_impl->cfg.memory_size_mb - m_impl->cfg.kernel_size_mb)
                                * 1024ull * 1024ull / 4096ull;
    const uint64_t used = m_impl->stats.data_frames_allocated;
    return used >= total_frames ? 0 : total_frames - used;
}

bool Kernel::probe(uint32_t asid, uint64_t vaddr, uint64_t* ppn, uint32_t* page_size_bits) const
{
    const EmbedRadixPageTable* pt = m_impl->pt_for_const(asid);
    if (pt == nullptr)
        return false;

    uint64_t base_ppn = 0;
    uint32_t ps       = 12;
    if (!pt->lookup(vaddr, &base_ppn, &ps))
        return false;

    /* Offset into the frame so the caller always gets a 4K number. */
    const uint64_t within = (vaddr & ((1ull << ps) - 1)) >> 12;
    if (ppn)            *ppn = base_ppn + within;
    if (page_size_bits) *page_size_bits = ps;
    return true;
}

uint32_t Kernel::leaf_level(uint32_t asid, uint64_t vaddr) const
{
    const EmbedRadixPageTable* pt = m_impl->pt_for_const(asid);
    if (pt == nullptr)
        return 1;
    return static_cast<uint32_t>(m_impl->cfg.pt_levels) - pt->leaf_depth(vaddr);
}

uint64_t Kernel::root_table_paddr(uint32_t asid)
{
    return m_impl->pt_for(asid)->root_ppn() * 4096ull;
}

uint64_t Kernel::minor_fault_latency_ns(const FaultRequest& req, uint32_t page_size_bits,
                                        bool pt_alloc_needed) const
{
    FaultClass fc;
    fc.mapping         = to_internal(req.mapping);
    fc.access          = to_internal(req.access);
    fc.page_size       = to_page_size(page_size_bits);
    fc.pt_alloc_needed = pt_alloc_needed;
    fc.core_id         = static_cast<uint8_t>(req.core_id);
    return m_impl->calib.total_penalty(fc);
}

Translation Kernel::translate(const FaultRequest& req)
{
    Translation out;
    m_impl->stats.translations++;

    EmbedRadixPageTable* pt = m_impl->pt_for(req.asid);

    uint64_t base_ppn = 0;
    uint32_t ps       = 12;

    if (pt->lookup(req.vaddr, &base_ppn, &ps)) {
        const uint64_t within = (req.vaddr & ((1ull << ps) - 1)) >> 12;
        out.ppn             = base_ppn + within;
        out.page_size_bits  = ps;
        out.faulted         = false;
        out.ok              = true;
        return out;
    }

    /* ---- minor fault ---- */
    auto [alloc_ppn, alloc_ps] = m_impl->alloc->allocate(4096, req.vaddr, req.core_id);
    if (alloc_ppn == static_cast<UInt64>(-1)) {
        m_impl->stats.failed_allocations++;
        out.ok = false;
        return out;
    }

    ps       = static_cast<uint32_t>(alloc_ps);
    base_ppn = alloc_ppn;

    bool promoted = false;
    const int pt_frames = pt->install(req.vaddr, base_ppn, ps, &promoted);
    if (pt_frames < 0) {
        m_impl->stats.failed_allocations++;
        out.ok = false;
        return out;
    }

    /* Frames pte_address() made earlier belong to this fault. */
    const uint64_t key = memo_key(req.asid, req.vaddr >> 12);
    uint32_t pending = 0;
    auto pend_it = m_impl->pending_pt_frames.find(key);
    if (pend_it != m_impl->pending_pt_frames.end()) {
        pending = pend_it->second;
        m_impl->pending_pt_frames.erase(pend_it);
    }
    const uint32_t total_pt_frames = static_cast<uint32_t>(pt_frames) + pending;

    const uint64_t within = (req.vaddr & ((1ull << ps) - 1)) >> 12;
    out.ppn              = base_ppn + within;
    out.page_size_bits   = ps;
    out.faulted          = true;
    out.ok               = true;
    out.fault_latency_ns = minor_fault_latency_ns(req, ps, total_pt_frames > 0);

    m_impl->stats.minor_faults++;
    if (ps > 12)
        m_impl->stats.huge_page_faults++;
    /*
     * A promotion covers frames handed out earlier as 4K pages in the
     * same region, so only a fresh mapping should find them free.
     */
    const uint64_t covered = 1ull << (ps - 12);
    const uint64_t clashes = m_impl->claim_frames(base_ppn, covered);
    if (!promoted && clashes > 0) {
        m_impl->stats.aliased_frames += clashes;
        if (m_impl->stats.aliased_frames == clashes) {
            std::fprintf(stderr,
                         "[MimicOS] BUG: frame %llu was already handed out; "
                         "virtual page %llu now shares physical memory with "
                         "another page\n",
                         (unsigned long long)base_ppn,
                         (unsigned long long)(req.vaddr >> 12));
        }
    }

    m_impl->stats.pt_frames_allocated += total_pt_frames;
    /* A promotion covers frames already counted, so do not count them twice. */
    if (!promoted)
        m_impl->stats.data_frames_allocated += (1ull << (ps - 12));
    m_impl->stats.total_fault_latency_ns += out.fault_latency_ns;

    if (m_impl->mapping_trace.is_open()) {
        m_impl->mapping_trace << m_impl->mapping_seq++ << ','
                              << req.asid << ','
                              << (req.vaddr >> 12) << ','
                              << base_ppn << ','
                              << ps << ','
                              << total_pt_frames << ','
                              << out.fault_latency_ns << '\n';
        /*
         * Flush per row: a host may exit without destroying us, and a
         * truncated trace looks like a mismatch.  Test-only path.
         */
        m_impl->mapping_trace.flush();
    }

    return out;
}

bool Kernel::pte_address(const FaultRequest& req, uint32_t level, PtwStep* out)
{
    const uint32_t levels = static_cast<uint32_t>(m_impl->cfg.pt_levels);
    if (level == 0 || level > levels)
        return false;

    const uint32_t depth = levels - level;
    EmbedRadixPageTable* pt = m_impl->pt_for(req.asid);

    uint64_t paddr   = 0;
    bool     is_leaf = false;
    uint32_t created = 0;

    if (!pt->entry_address(req.vaddr, depth, /*allocate=*/true, &paddr, &is_leaf, &created))
        return false;

    if (created > 0) {
        if (m_impl->pending_pt_frames.size() >= kMaxPendingPtEntries)
            m_impl->pending_pt_frames.clear();
        m_impl->pending_pt_frames[memo_key(req.asid, req.vaddr >> 12)] += created;
    }

    if (out) {
        out->level     = level;
        out->depth     = depth;
        out->pte_paddr = paddr;
        out->is_leaf   = is_leaf;
    }
    return true;
}

std::vector<PtwStep> Kernel::walk(const FaultRequest& req)
{
    std::vector<PtwStep> steps;
    const uint32_t levels = static_cast<uint32_t>(m_impl->cfg.pt_levels);
    for (uint32_t level = levels; level >= 1; level--) {
        PtwStep step;
        if (!pte_address(req, level, &step))
            break;
        steps.push_back(step);
        if (step.is_leaf)
            break;
    }
    return steps;
}

bool Kernel::enable_mapping_trace(const std::string& path, std::string* err)
{
    m_impl->mapping_trace.open(path, std::ios::out | std::ios::trunc);
    if (!m_impl->mapping_trace.is_open()) {
        if (err) *err = "MimicOS: cannot open mapping trace '" + path + "' for writing";
        return false;
    }
    /*
     * Record the geometry and allocator this ran under.  Replay against
     * a different config silently changes the frame counts, and the
     * comparison then looks like an adapter bug rather than a mismatch.
     */
    m_impl->mapping_trace
        << "# mimicos_mapping_trace v1"
        << " pt_levels=" << m_impl->cfg.pt_levels
        << " allocator=" << m_impl->cfg.allocator
        << " memory_size_mb=" << m_impl->cfg.memory_size_mb
        << " kernel_size_mb=" << m_impl->cfg.kernel_size_mb
        << " max_order=" << m_impl->cfg.max_order
        << " frag_type=" << m_impl->cfg.fragmentation_type
        << " fragmentation=" << m_impl->cfg.fragmentation
        << " promotion_threshold=" << m_impl->cfg.promotion_threshold
        << "\n";
    m_impl->mapping_trace << "seq,asid,vpn,base_ppn,page_size_bits,pt_frames,fault_latency_ns\n";
    return true;
}

void Kernel::flush_mapping_trace()
{
    if (m_impl->mapping_trace.is_open())
        m_impl->mapping_trace.flush();
}

} // namespace mimicos
