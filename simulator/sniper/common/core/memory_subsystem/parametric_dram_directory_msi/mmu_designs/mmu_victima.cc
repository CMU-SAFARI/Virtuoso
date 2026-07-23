// ============================================================================
// @kanellok: Victima MMU Implementation
// Based on: "Victima: Drastically Increasing Address Translation Reach by
//            Leveraging Underutilized Cache Resources"
// Published: MICRO 2023
// ============================================================================

#include "mmu_victima.h"
#include "mmu_base.h"
#include "memory_manager.h"
#include "cache_cntlr.h"
#include "pagetable_factory.h"
#include "pagetable_radix.h"
#include "mimicos.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "instruction.h"
#include "core.h"
#include "thread.h"
#include "debug_config.h"

#include <iostream>
#include <fstream>
#include <algorithm>

namespace ParametricDramDirectoryMSI
{

// ============================================================================
// PTW Cost Predictor Implementation
// ============================================================================

PTWCostPredictor::PTWCostPredictor()
    : m_freq_threshold_low(1)
    , m_freq_threshold_high(12)
    , m_cost_threshold_low(1)
    , m_cost_threshold_high(7)
    , m_l2_cache_mpki_threshold(5)
{
}

void PTWCostPredictor::configure(UInt32 freq_threshold_low, UInt32 freq_threshold_high,
                                  UInt32 cost_threshold_low, UInt32 cost_threshold_high,
                                  UInt32 l2_cache_mpki_threshold)
{
    m_freq_threshold_low = freq_threshold_low;
    m_freq_threshold_high = freq_threshold_high;
    m_cost_threshold_low = cost_threshold_low;
    m_cost_threshold_high = cost_threshold_high;
    m_l2_cache_mpki_threshold = l2_cache_mpki_threshold;
}

bool PTWCostPredictor::predict(UInt32 ptw_frequency, UInt32 ptw_cost, UInt32 l2_cache_mpki) const
{
    // If L2 cache MPKI is high (data has low locality), bypass predictor
    // and always insert TLB block (caching data is not beneficial anyway)
    if (l2_cache_mpki > m_l2_cache_mpki_threshold)
    {
        return true;  // Bypass: always insert
    }
    
    // Bounding box predictor: predict costly if within the box
    // freq ∈ [freq_low, freq_high] AND cost ∈ [cost_low, cost_high]
    bool in_freq_range = (ptw_frequency >= m_freq_threshold_low && 
                          ptw_frequency <= m_freq_threshold_high);
    bool in_cost_range = (ptw_cost >= m_cost_threshold_low && 
                          ptw_cost <= m_cost_threshold_high);
    
    return in_freq_range && in_cost_range;
}

void PTWCostPredictor::updateCounters(IntPtr vpn, bool had_dram_access)
{
    auto& counters = m_page_counters[vpn];
    
    // Increment PTW frequency (saturating at max 3-bit value)
    if (counters.ptw_frequency < 7)
        counters.ptw_frequency++;
    
    // Increment PTW cost if walk caused DRAM access (saturating at max 4-bit value)
    if (had_dram_access && counters.ptw_cost < 15)
        counters.ptw_cost++;
}

std::pair<UInt32, UInt32> PTWCostPredictor::getCounters(IntPtr vpn) const
{
    auto it = m_page_counters.find(vpn);
    if (it != m_page_counters.end())
    {
        return {it->second.ptw_frequency, it->second.ptw_cost};
    }
    return {0, 0};
}

void PTWCostPredictor::clearCounters()
{
    m_page_counters.clear();
}

// ============================================================================
// TLB Block Implementation
// ============================================================================

TLBBlock::TLBBlock()
    : vpn_base(0)
    , asid(0)
    , page_size(12)
    , is_nested(false)
    , insert_time(SubsecondTime::Zero())
{
    invalidate();
}

void TLBBlock::invalidate()
{
    for (int i = 0; i < ENTRIES_PER_BLOCK; i++)
    {
        ppn[i] = 0;
        valid[i] = false;
    }
}

bool TLBBlock::lookupEntry(IntPtr vpn, IntPtr& out_ppn) const
{
    // Check if VPN falls within this block
    IntPtr vpn_aligned = vpn & ~(IntPtr)(ENTRIES_PER_BLOCK - 1);
    if (vpn_aligned != vpn_base)
        return false;
    
    // Get entry index within block
    int index = vpn & (ENTRIES_PER_BLOCK - 1);
    if (!valid[index])
        return false;
    
    out_ppn = ppn[index];
    return true;
}

void TLBBlock::insertEntry(IntPtr vpn, IntPtr _ppn)
{
    int index = vpn & (ENTRIES_PER_BLOCK - 1);
    ppn[index] = _ppn;
    valid[index] = true;
}

// ============================================================================
// L2 Cache TLB Extension Implementation
// ============================================================================

L2CacheTLBExtension::L2CacheTLBExtension(Core* core, MemoryManagerBase* memory_manager)
    : m_core(core)
    , m_memory_manager(memory_manager)
    , m_max_tlb_blocks(4096)  // Default: 4K TLB blocks = 32K TLB entries
    , m_lookup_latency(SubsecondTime::NS(2))  // Parallel to L2 lookup
{
    m_stats = {0, 0, 0, 0, 0};
}

L2CacheTLBExtension::~L2CacheTLBExtension()
{
}

void L2CacheTLBExtension::configure(UInt32 max_tlb_blocks, SubsecondTime lookup_latency)
{
    m_max_tlb_blocks = max_tlb_blocks;
    m_lookup_latency = lookup_latency;
}

UInt64 L2CacheTLBExtension::makeKey(IntPtr vpn_base, UInt16 asid) const
{
    // Combine VPN base and ASID into a single key
    return ((UInt64)asid << 48) | (vpn_base & 0xFFFFFFFFFFFF);
}

bool L2CacheTLBExtension::lookupTLBBlock(IntPtr vpn, UInt16 asid, IntPtr& out_ppn,
                                          SubsecondTime& out_latency, bool& out_is_4kb)
{
    // Compute VPN base (aligned to 8 entries)
    IntPtr vpn_base = vpn & ~(IntPtr)(TLBBlock::ENTRIES_PER_BLOCK - 1);
    
    // Lookup both 4KB and 2MB page sizes in parallel (as in the paper)
    // Try 4KB first
    UInt64 key_4kb = makeKey(vpn_base, asid);
    auto it = m_tlb_blocks.find(key_4kb);
    
    if (it != m_tlb_blocks.end())
    {
        TLBBlock& block = it->second;
        if (block.lookupEntry(vpn, out_ppn))
        {
            out_latency = m_lookup_latency;
            out_is_4kb = (block.page_size == 12);
            m_stats.lookup_hits++;
            return true;
        }
    }
    
    // Try 2MB pages (VPN base is different for 2MB pages)
    IntPtr vpn_2mb = vpn >> 9;  // 2MB VPN (shift by 9 to account for 512 4KB pages per 2MB)
    IntPtr vpn_base_2mb = vpn_2mb & ~(IntPtr)(TLBBlock::ENTRIES_PER_BLOCK - 1);
    UInt64 key_2mb = makeKey(vpn_base_2mb, asid) | (1ULL << 63);  // Mark as 2MB
    
    it = m_tlb_blocks.find(key_2mb);
    if (it != m_tlb_blocks.end())
    {
        TLBBlock& block = it->second;
        if (block.lookupEntry(vpn_2mb, out_ppn))
        {
            out_latency = m_lookup_latency;
            out_is_4kb = false;
            m_stats.lookup_hits++;
            return true;
        }
    }
    
    out_latency = m_lookup_latency;
    m_stats.lookup_misses++;
    return false;
}

void L2CacheTLBExtension::insertTLBBlock(IntPtr vpn_base, UInt16 asid, int page_size,
                                          const IntPtr* ppns, const bool* valid, bool is_nested)
{
    // Evict if necessary
    evictIfNeeded();
    
    // Create key (differentiate 4KB and 2MB by high bit)
    UInt64 key = makeKey(vpn_base, asid);
    if (page_size == 21)  // 2MB
        key |= (1ULL << 63);
    
    // Create or update TLB block
    TLBBlock& block = m_tlb_blocks[key];
    block.vpn_base = vpn_base;
    block.asid = asid;
    block.page_size = page_size;
    block.is_nested = is_nested;
    block.insert_time = SubsecondTime::Zero();  // TODO: Use current time
    
    for (int i = 0; i < TLBBlock::ENTRIES_PER_BLOCK; i++)
    {
        block.ppn[i] = ppns[i];
        block.valid[i] = valid[i];
    }
    
    m_stats.insertions++;
}

void L2CacheTLBExtension::invalidateTLBBlock(IntPtr vpn_base, UInt16 asid)
{
    UInt64 key_4kb = makeKey(vpn_base, asid);
    UInt64 key_2mb = key_4kb | (1ULL << 63);
    
    if (m_tlb_blocks.erase(key_4kb) || m_tlb_blocks.erase(key_2mb))
    {
        m_stats.invalidations++;
    }
}

void L2CacheTLBExtension::invalidateByASID(UInt16 asid)
{
    auto it = m_tlb_blocks.begin();
    while (it != m_tlb_blocks.end())
    {
        if (it->second.asid == asid)
        {
            it = m_tlb_blocks.erase(it);
            m_stats.invalidations++;
        }
        else
        {
            ++it;
        }
    }
}

void L2CacheTLBExtension::invalidateAll()
{
    m_stats.invalidations += m_tlb_blocks.size();
    m_tlb_blocks.clear();
}

void L2CacheTLBExtension::evictIfNeeded()
{
    while (m_tlb_blocks.size() >= m_max_tlb_blocks)
    {
        // Simple LRU eviction - evict the entry with oldest insert_time
        // In a real implementation, this would use SRRIP with TLB awareness
        auto oldest = m_tlb_blocks.begin();
        m_tlb_blocks.erase(oldest);
        m_stats.evictions++;
    }
}

// ============================================================================
// Victima MMU Implementation
// ============================================================================

MemoryManagementUnitVictima::MemoryManagementUnitVictima(Core* core, MemoryManagerBase* memory_manager,
                                                           ShmemPerfModel* shmem_perf_model, String name,
                                                           MemoryManagementUnitBase* nested_mmu)
    : MemoryManagementUnitBase(core, memory_manager, shmem_perf_model, name, nested_mmu)
    , m_memory_manager(memory_manager)
    , m_tlb_subsystem(nullptr)
    , m_pt_walkers(nullptr)
    , m_ptw_filter(nullptr)
    , m_ptw_cost_predictor(nullptr)
    , m_l2_tlb_extension(nullptr)
    , m_victima_enabled(true)
    , m_insert_on_miss(true)
    , m_insert_on_eviction(true)
    , m_l2_cache_mpki_threshold(5)
    , m_l2_cache_accesses(0)
    , m_l2_cache_misses(0)
    , m_current_l2_mpki(0)
    , m_last_mpki_update(SubsecondTime::Zero())
{
    std::cout << std::endl;
    std::cout << "[Victima MMU] Initializing Victima MMU for core " << core->getId() << std::endl;
    
    // Initialize logging
    m_mmu_log = new SimLog("VictimaMMU", core->getId(), DEBUG_MMU);
    m_mmu_log->log("Initializing Victima MMU for core " + std::to_string(core->getId()));
    
    // Initialize components
    instantiatePageTableWalker();
    instantiateTLBSubsystem();
    initializeVictimaComponents();
    registerMMUStats();
    
    std::cout << std::endl;
}

MemoryManagementUnitVictima::~MemoryManagementUnitVictima()
{
    std::cout << "[Victima MMU] Destroying Victima MMU for core " << core->getId() << std::endl;
    
    // Print Victima-specific stats
    std::cout << "[Victima MMU] Core " << core->getId() << " Summary:" << std::endl;
    std::cout << "  TLB Block Lookups: " << m_translation_stats.l2_tlb_block_lookups << std::endl;
    std::cout << "  TLB Block Hits: " << m_translation_stats.l2_tlb_block_hits << std::endl;
    std::cout << "  TLB Block Hit Rate: " 
              << (m_translation_stats.l2_tlb_block_lookups > 0 
                  ? (100.0 * m_translation_stats.l2_tlb_block_hits / m_translation_stats.l2_tlb_block_lookups) 
                  : 0.0) << "%" << std::endl;
    std::cout << "  TLB Block Insertions: " << m_translation_stats.l2_tlb_block_insertions << std::endl;
    std::cout << "  PTW-CP Positive Predictions: " << m_translation_stats.ptw_cost_predictions_positive << std::endl;
    std::cout << "  PTW-CP Negative Predictions: " << m_translation_stats.ptw_cost_predictions_negative << std::endl;
    std::cout << "  PTW-CP Bypassed: " << m_translation_stats.ptw_cost_predictions_bypassed << std::endl;
    
    delete m_mmu_log;
    delete m_ptw_cost_predictor;
    delete m_l2_tlb_extension;
    delete m_tlb_subsystem;
    delete m_pt_walkers;
    if (m_ptw_filter)
        delete m_ptw_filter;
    
    if (m_translation_stats.tlb_latency_per_level)
        delete[] m_translation_stats.tlb_latency_per_level;
    if (m_translation_stats.tlb_hit_page_sizes)
        delete[] m_translation_stats.tlb_hit_page_sizes;
}

void MemoryManagementUnitVictima::instantiatePageTableWalker()
{
    m_mmu_log->log("Instantiating page table walker");
    
    // Create PTW MSHRs
    int num_walkers = Sim()->getCfg()->getInt(name + "/page_table_walkers");
    m_pt_walkers = new MSHR(num_walkers);
    m_ptw_filter = nullptr;  // No filter for Victima
    
    m_mmu_log->log("Created " + std::to_string(num_walkers) + " page table walkers");
}

void MemoryManagementUnitVictima::instantiateTLBSubsystem()
{
    m_mmu_log->log("Instantiating TLB subsystem");
    
    m_tlb_subsystem = new TLBHierarchy(name, core, m_memory_manager, shmem_perf_model);
    
    m_mmu_log->log("TLB subsystem initialized with " + 
                   std::to_string(m_tlb_subsystem->getTLBSubsystem().size()) + " levels");
}

void MemoryManagementUnitVictima::initializeVictimaComponents()
{
    m_mmu_log->log("Initializing Victima-specific components");
    
    // Helper lambda for config with defaults
    auto getIntCfg = [this](const String& key, int defaultVal) -> int {
        if (Sim()->getCfg()->hasKey(name + key))
            return Sim()->getCfg()->getInt(name + key);
        return defaultVal;
    };
    
    // Read Victima configuration
    m_victima_enabled = Sim()->getCfg()->getBoolDefault(name + "/victima/enabled", true);
    m_insert_on_miss = Sim()->getCfg()->getBoolDefault(name + "/victima/insert_on_miss", true);
    m_insert_on_eviction = Sim()->getCfg()->getBoolDefault(name + "/victima/insert_on_eviction", true);
    m_l2_cache_mpki_threshold = getIntCfg("/victima/l2_cache_mpki_threshold", 5);
    
    // Initialize PTW Cost Predictor
    m_ptw_cost_predictor = new PTWCostPredictor();
    UInt32 freq_low = getIntCfg("/victima/ptw_cp/freq_threshold_low", 1);
    UInt32 freq_high = getIntCfg("/victima/ptw_cp/freq_threshold_high", 12);
    UInt32 cost_low = getIntCfg("/victima/ptw_cp/cost_threshold_low", 1);
    UInt32 cost_high = getIntCfg("/victima/ptw_cp/cost_threshold_high", 7);
    m_ptw_cost_predictor->configure(freq_low, freq_high, cost_low, cost_high, m_l2_cache_mpki_threshold);
    
    // Initialize L2 Cache TLB Extension
    m_l2_tlb_extension = new L2CacheTLBExtension(core, m_memory_manager);
    UInt32 max_tlb_blocks = getIntCfg("/victima/max_tlb_blocks", 4096);
    SubsecondTime lookup_latency = SubsecondTime::NS(getIntCfg("/victima/tlb_block_lookup_latency_ns", 2));
    m_l2_tlb_extension->configure(max_tlb_blocks, lookup_latency);
    
    m_mmu_log->log("Victima enabled: " + std::string(m_victima_enabled ? "true" : "false"));
    m_mmu_log->log("Insert on miss: " + std::string(m_insert_on_miss ? "true" : "false"));
    m_mmu_log->log("Insert on eviction: " + std::string(m_insert_on_eviction ? "true" : "false"));
    m_mmu_log->log("Max TLB blocks: " + std::to_string(max_tlb_blocks));
}

void MemoryManagementUnitVictima::registerMMUStats()
{
    // Initialize stats structure
    m_translation_stats.num_translations = 0;
    m_translation_stats.page_faults = 0;
    m_translation_stats.page_table_walks = 0;
    m_translation_stats.total_walk_latency = SubsecondTime::Zero();
    m_translation_stats.total_translation_latency = SubsecondTime::Zero();
    m_translation_stats.total_tlb_latency = SubsecondTime::Zero();
    m_translation_stats.total_fault_latency = SubsecondTime::Zero();
    m_translation_stats.tlb_latency_per_level = nullptr;
    m_translation_stats.tlb_hit_page_sizes = nullptr;
    m_translation_stats.l2_tlb_block_lookups = 0;
    m_translation_stats.l2_tlb_block_hits = 0;
    m_translation_stats.l2_tlb_block_misses = 0;
    m_translation_stats.l2_tlb_block_insertions = 0;
    m_translation_stats.l2_tlb_block_evictions = 0;
    m_translation_stats.ptw_cost_predictions_positive = 0;
    m_translation_stats.ptw_cost_predictions_negative = 0;
    m_translation_stats.ptw_cost_predictions_bypassed = 0;
    m_translation_stats.l2_tlb_block_hit_latency_saved = SubsecondTime::Zero();
    
    // Basic translation stats
    registerStatsMetric(name, core->getId(), "num_translations", &m_translation_stats.num_translations);
    registerStatsMetric(name, core->getId(), "page_faults", &m_translation_stats.page_faults);
    registerStatsMetric(name, core->getId(), "page_table_walks", &m_translation_stats.page_table_walks);
    
    // Latency stats
    registerStatsMetric(name, core->getId(), "total_walk_latency", &m_translation_stats.total_walk_latency);
    registerStatsMetric(name, core->getId(), "total_translation_latency", &m_translation_stats.total_translation_latency);
    registerStatsMetric(name, core->getId(), "total_tlb_latency", &m_translation_stats.total_tlb_latency);
    registerStatsMetric(name, core->getId(), "total_fault_latency", &m_translation_stats.total_fault_latency);
    
    // Victima-specific stats
    registerStatsMetric(name, core->getId(), "victima_l2_tlb_block_lookups", &m_translation_stats.l2_tlb_block_lookups);
    registerStatsMetric(name, core->getId(), "victima_l2_tlb_block_hits", &m_translation_stats.l2_tlb_block_hits);
    registerStatsMetric(name, core->getId(), "victima_l2_tlb_block_misses", &m_translation_stats.l2_tlb_block_misses);
    registerStatsMetric(name, core->getId(), "victima_l2_tlb_block_insertions", &m_translation_stats.l2_tlb_block_insertions);
    registerStatsMetric(name, core->getId(), "victima_l2_tlb_block_evictions", &m_translation_stats.l2_tlb_block_evictions);
    registerStatsMetric(name, core->getId(), "victima_ptw_cp_positive", &m_translation_stats.ptw_cost_predictions_positive);
    registerStatsMetric(name, core->getId(), "victima_ptw_cp_negative", &m_translation_stats.ptw_cost_predictions_negative);
    registerStatsMetric(name, core->getId(), "victima_ptw_cp_bypassed", &m_translation_stats.ptw_cost_predictions_bypassed);
    registerStatsMetric(name, core->getId(), "victima_latency_saved", &m_translation_stats.l2_tlb_block_hit_latency_saved);
    
    // Per-TLB-level stats
    m_translation_stats.tlb_latency_per_level = new SubsecondTime[m_tlb_subsystem->getTLBSubsystem().size()];
    for (UInt32 i = 0; i < m_tlb_subsystem->getTLBSubsystem().size(); i++)
    {
        registerStatsMetric(name, core->getId(), "tlb_latency_" + itostr(i), 
                           &m_translation_stats.tlb_latency_per_level[i]);
    }
}

void MemoryManagementUnitVictima::discoverVMAs()
{
    m_mmu_log->log("VMA discovery not implemented for Victima MMU");
}

PTWResult MemoryManagementUnitVictima::filterPTWResult(IntPtr address, PTWResult ptw_result,
                                                        PageTable* page_table, bool count)
{
    // Apply PTW filter if configured
    if (m_ptw_filter)
    {
        return m_ptw_filter->filterPTWResult(address, ptw_result, page_table, count);
    }
    return ptw_result;
}

IntPtr MemoryManagementUnitVictima::performAddressTranslation(IntPtr eip, IntPtr address,
                                                               bool instruction, Core::lock_signal_t lock,
                                                               bool modeled, bool count)
{
    dram_accesses_during_last_walk = 0;
    
    // Handle perfect translation mode
    if (perfect_translation_enabled)
    {
        int app_id = core->getThread()->getAppId();
        PageTable* page_table = Sim()->getMimicOS()->getPageTable(app_id);
        auto [physical_address, page_size] = translateWithoutTiming(address, page_table);
        
        if (count)
            m_translation_stats.num_translations++;
        
        return physical_address;
    }
    
    m_mmu_log->section("Starting Victima translation for VA: " + m_mmu_log->hex(address));
    
    SubsecondTime time = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
    m_pt_walkers->removeCompletedEntries(time);
    
    if (count)
        m_translation_stats.num_translations++;
    
    int app_id = core->getThread()->getAppId();
    PageTable* page_table = Sim()->getMimicOS()->getPageTable(app_id);
    UInt16 asid = static_cast<UInt16>(app_id);
    
    // ========================================================================
    // PHASE 1: TLB Hierarchy Lookup
    // ========================================================================
    TLBSubsystem tlbs = m_tlb_subsystem->getTLBSubsystem();
    
    bool tlb_hit = false;
    int hit_level = -1;
    IntPtr ppn = 0;
    int page_size = 12;  // Default 4KB
    SubsecondTime tlb_latency = SubsecondTime::Zero();
    CacheBlockInfo* tlb_block_info = nullptr;
    
    // Search TLB hierarchy
    for (UInt32 level = 0; level < tlbs.size() && !tlb_hit; level++)
    {
        SubsecondTime level_latency = SubsecondTime::Zero();
        
        for (UInt32 tlb_idx = 0; tlb_idx < tlbs[level].size(); tlb_idx++)
        {
            TLB* tlb = tlbs[level][tlb_idx];
            
            // Check if this TLB handles this type of access (instruction vs data)
            TLBtype tlb_type = tlb->getType();
            if ((instruction && tlb_type == TLBtype::Data) || (!instruction && tlb_type == TLBtype::Instruction))
                continue;
            
            tlb_block_info = tlb->lookup(address, time, count, lock, eip, modeled, count, page_table);
            level_latency = std::max(level_latency, tlb->getLatency());
            
            if (tlb_block_info != nullptr)
            {
                tlb_hit = true;
                hit_level = level;
                break;
            }
        }
        
        tlb_latency += level_latency;
        if (count && hit_level >= 0)
            m_translation_stats.tlb_latency_per_level[hit_level] += level_latency;
    }
    
    // ========================================================================
    // PHASE 2: Victima L2 Cache TLB Block Lookup (on L2 TLB miss)
    // ========================================================================
    bool victima_hit = false;
    SubsecondTime victima_latency = SubsecondTime::Zero();
    
    if (!tlb_hit && m_victima_enabled)
    {
        IntPtr vpn = address >> 12;  // 4KB VPN
        
        m_translation_stats.l2_tlb_block_lookups++;
        
        bool is_4kb;
        if (m_l2_tlb_extension->lookupTLBBlock(vpn, asid, ppn, victima_latency, is_4kb))
        {
            victima_hit = true;
            page_size = is_4kb ? 12 : 21;
            m_translation_stats.l2_tlb_block_hits++;
            
            m_mmu_log->debug("Victima TLB block hit for VPN: " + m_mmu_log->hex(vpn));
        }
        else
        {
            m_translation_stats.l2_tlb_block_misses++;
        }
    }
    
    // ========================================================================
    // PHASE 3: Page Table Walk (on TLB and Victima miss)
    // ========================================================================
    SubsecondTime walk_latency = SubsecondTime::Zero();
    bool performed_ptw = false;
    bool had_dram_access = false;
    
    if (!tlb_hit && !victima_hit)
    {
        m_mmu_log->debug("TLB and Victima miss, performing PTW");
        
        if (count)
            m_translation_stats.page_table_walks++;
        
        // Perform page table walk
        PTWOutcome ptw_outcome = performPTW(address, modeled, count, false, eip, lock, page_table, false, instruction);
        
        walk_latency = ptw_outcome.latency;
        ppn = ptw_outcome.ppn;
        page_size = ptw_outcome.page_size;
        performed_ptw = true;
        had_dram_access = (dram_accesses_during_last_walk > 0);
        
        if (count)
            m_translation_stats.total_walk_latency += walk_latency;
        
        // Handle page fault
        if (ptw_outcome.page_fault)
        {
            m_mmu_log->debug("Page fault during PTW");
            m_translation_stats.page_faults++;
            return (IntPtr)-1;
        }
    }
    
    // ========================================================================
    // PHASE 4: Victima TLB Block Insertion
    // ========================================================================
    if (performed_ptw && m_victima_enabled && m_insert_on_miss)
    {
        IntPtr vpn = address >> page_size;
        
        // Update PTW cost predictor counters
        m_ptw_cost_predictor->updateCounters(vpn, had_dram_access);
        
        // Check if should insert TLB block
        if (shouldInsertTLBBlock(vpn, had_dram_access))
        {
            handleL2TLBMiss(vpn, asid, ppn, page_size, had_dram_access);
        }
    }
    
    // ========================================================================
    // PHASE 5: TLB Allocation (on PTW or Victima hit)
    // ========================================================================
    if (!tlb_hit)
    {
        // Allocate in all "allocate on miss" TLBs
        for (UInt32 level = 0; level < tlbs.size(); level++)
        {
            for (UInt32 tlb_idx = 0; tlb_idx < tlbs[level].size(); tlb_idx++)
            {
                TLB* tlb = tlbs[level][tlb_idx];
                
                TLBtype tlb_type = tlb->getType();
                if ((instruction && tlb_type == TLBtype::Data) || (!instruction && tlb_type == TLBtype::Instruction))
                    continue;
                
                if (tlb->getAllocateOnMiss())
                {
                    TLBAllocResult alloc_result = tlb->allocate(address, time, count, lock, page_size, ppn);
                    
                    // Handle L2 TLB eviction for Victima
                    if (alloc_result.evicted && m_victima_enabled && m_insert_on_eviction && level == 1)
                    {
                        IntPtr evicted_vpn = alloc_result.address >> alloc_result.page_size;
                        handleL2TLBEviction(evicted_vpn, asid, alloc_result.page_size);
                    }
                }
            }
        }
    }
    
    // ========================================================================
    // PHASE 6: Calculate Physical Address
    // ========================================================================
    int page_offset_bits = page_size;
    IntPtr page_offset_mask = (1ULL << page_offset_bits) - 1;
    IntPtr physical_address = (ppn << page_offset_bits) | (address & page_offset_mask);
    
    // Update timing model
    SubsecondTime total_latency = tlb_latency;
    if (!tlb_hit)
    {
        if (victima_hit)
        {
            total_latency += victima_latency;
            // Track latency saved vs doing full PTW
            m_translation_stats.l2_tlb_block_hit_latency_saved += walk_latency;
        }
        else
        {
            total_latency += walk_latency;
        }
    }
    
    if (modeled)
    {
        shmem_perf_model->incrElapsedTime(total_latency, ShmemPerfModel::_USER_THREAD);
    }
    
    if (count)
    {
        m_translation_stats.total_translation_latency += total_latency;
        m_translation_stats.total_tlb_latency += tlb_latency;
    }
    
    m_mmu_log->debug("Translation complete: VA " + m_mmu_log->hex(address) + 
                     " -> PA " + m_mmu_log->hex(physical_address) +
                     " (TLB hit: " + (tlb_hit ? "yes" : "no") +
                     ", Victima hit: " + (victima_hit ? "yes" : "no") + ")");
    
    return physical_address;
}

bool MemoryManagementUnitVictima::shouldInsertTLBBlock(IntPtr vpn, bool had_dram_access)
{
    auto [freq, cost] = m_ptw_cost_predictor->getCounters(vpn);
    
    // Check if L2 cache has high MPKI (bypass predictor)
    if (m_current_l2_mpki > m_l2_cache_mpki_threshold)
    {
        m_translation_stats.ptw_cost_predictions_bypassed++;
        return true;
    }
    
    bool should_insert = m_ptw_cost_predictor->predict(freq, cost, m_current_l2_mpki);
    
    if (should_insert)
        m_translation_stats.ptw_cost_predictions_positive++;
    else
        m_translation_stats.ptw_cost_predictions_negative++;
    
    return should_insert;
}

void MemoryManagementUnitVictima::handleL2TLBMiss(IntPtr vpn, UInt16 asid, IntPtr ppn,
                                                   int page_size, bool had_dram_access)
{
    m_mmu_log->debug("Handling L2 TLB miss for VPN: " + m_mmu_log->hex(vpn));
    
    // Create TLB block with 8 contiguous entries
    // In real hardware, the PTW fetches 8 PTEs in a cache line
    IntPtr vpn_base = vpn & ~(IntPtr)(TLBBlock::ENTRIES_PER_BLOCK - 1);
    
    IntPtr ppns[TLBBlock::ENTRIES_PER_BLOCK];
    bool valid[TLBBlock::ENTRIES_PER_BLOCK];
    
    // Initialize all entries as invalid
    for (int i = 0; i < TLBBlock::ENTRIES_PER_BLOCK; i++)
    {
        ppns[i] = 0;
        valid[i] = false;
    }
    
    // Set the entry we know about
    int index = vpn & (TLBBlock::ENTRIES_PER_BLOCK - 1);
    ppns[index] = ppn;
    valid[index] = true;
    
    // Insert TLB block into L2 cache
    m_l2_tlb_extension->insertTLBBlock(vpn_base, asid, page_size, ppns, valid, false);
    m_translation_stats.l2_tlb_block_insertions++;
}

void MemoryManagementUnitVictima::handleL2TLBEviction(IntPtr vpn, UInt16 asid, int page_size)
{
    m_mmu_log->debug("Handling L2 TLB eviction for VPN: " + m_mmu_log->hex(vpn));
    
    // Check PTW-CP prediction
    auto [freq, cost] = m_ptw_cost_predictor->getCounters(vpn);
    
    if (!m_ptw_cost_predictor->predict(freq, cost, m_current_l2_mpki))
    {
        m_translation_stats.ptw_cost_predictions_negative++;
        return;  // Don't insert TLB block
    }
    
    m_translation_stats.ptw_cost_predictions_positive++;
    
    // In real hardware, would issue background PTW to fetch PTE block
    // For simulation, we just mark that an insertion should happen
    // The actual insertion would happen when the background PTW completes
    
    // TODO: Implement background PTW for eviction-triggered insertion
    // For now, we skip this as it requires significant additional infrastructure
}

void MemoryManagementUnitVictima::updateL2CacheMPKI(bool was_miss, UInt64 instruction_count)
{
    m_l2_cache_accesses++;
    if (was_miss)
        m_l2_cache_misses++;
    
    // Update MPKI periodically
    if (instruction_count - m_l2_cache_accesses >= MPKI_WINDOW_INSTRUCTIONS)
    {
        m_current_l2_mpki = (m_l2_cache_misses * 1000) / MPKI_WINDOW_INSTRUCTIONS;
        m_l2_cache_accesses = 0;
        m_l2_cache_misses = 0;
    }
}

}  // namespace ParametricDramDirectoryMSI
