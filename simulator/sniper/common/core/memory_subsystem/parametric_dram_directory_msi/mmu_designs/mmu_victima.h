// ============================================================================
// @kanellok: Victima MMU Implementation
// Based on: "Victima: Drastically Increasing Address Translation Reach by
//            Leveraging Underutilized Cache Resources"
// Published: MICRO 2023
// Authors: Kanellopoulos et al.
// ============================================================================
//
// Victima is a software-transparent mechanism that increases translation reach
// by repurposing L2 cache blocks to store clusters of TLB entries (TLB blocks).
//
// Key Components:
// 1. TLB Blocks: L2 cache blocks storing 8 contiguous TLB entries (64 bytes)
// 2. PTW Cost Predictor (PTW-CP): Identifies costly-to-translate pages based
//    on PTW frequency and PTW cost (stored in PTE bits)
// 3. TLB-aware Cache Replacement: Modified SRRIP that prioritizes TLB entries
//    when translation pressure is high
//
// Triggering Conditions:
// - On L2 TLB miss: Transform PTE cache block into TLB block
// - On L2 TLB eviction: Issue background PTW to bring PTEs into L2 as TLB block
//
// ============================================================================

#pragma once

#include "memory_manager.h"
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "core.h"
#include "shmem_perf_model.h"
#include "pagetable.h"
#include "tlb_subsystem.h"
#include "mmu_base.h"
#include "ptmshrs.h"
#include "base_filter.h"
#include "sim_log.h"

#include <unordered_map>
#include <unordered_set>

namespace ParametricDramDirectoryMSI
{

class TLBHierarchy;

// ============================================================================
// PTW Cost Predictor (PTW-CP)
// ============================================================================
// A comparator-based predictor that estimates if a page will be costly to
// translate in the future. Uses two metrics embedded in the PTE:
// 1. PTW Frequency: 3-bit counter incremented on each PTW
// 2. PTW Cost: 4-bit counter incremented when PTW causes DRAM access
//
// Prediction: A page is costly-to-translate if (freq, cost) falls within
// the bounding box: freq ∈ [1, 12] AND cost ∈ [1, 7]
// ============================================================================
class PTWCostPredictor
{
public:
    PTWCostPredictor();
    
    // Configuration
    void configure(UInt32 freq_threshold_low, UInt32 freq_threshold_high,
                   UInt32 cost_threshold_low, UInt32 cost_threshold_high,
                   UInt32 l2_cache_mpki_threshold);
    
    // Prediction: returns true if page is predicted costly-to-translate
    bool predict(UInt32 ptw_frequency, UInt32 ptw_cost, UInt32 l2_cache_mpki) const;
    
    // Update counters on PTW completion
    void updateCounters(IntPtr vpn, bool had_dram_access);
    
    // Get counters for a page (returns {frequency, cost})
    std::pair<UInt32, UInt32> getCounters(IntPtr vpn) const;
    
    // Clear counters (e.g., on context switch)
    void clearCounters();
    
private:
    // Thresholds for bounding box predictor
    UInt32 m_freq_threshold_low;   // Default: 1
    UInt32 m_freq_threshold_high;  // Default: 12
    UInt32 m_cost_threshold_low;   // Default: 1
    UInt32 m_cost_threshold_high;  // Default: 7
    UInt32 m_l2_cache_mpki_threshold;  // Default: 5 (bypass predictor if MPKI > threshold)
    
    // Per-page counters (in real hardware, stored in PTE)
    // Map: VPN -> {ptw_frequency (3 bits), ptw_cost (4 bits)}
    struct PageCounters
    {
        UInt8 ptw_frequency : 3;  // 0-7, saturating
        UInt8 ptw_cost : 4;       // 0-15, saturating
        PageCounters() : ptw_frequency(0), ptw_cost(0) {}
    };
    std::unordered_map<IntPtr, PageCounters> m_page_counters;
};

// ============================================================================
// TLB Block: A cache block storing 8 contiguous TLB entries
// ============================================================================
struct TLBBlock
{
    static const int ENTRIES_PER_BLOCK = 8;  // 8 PTEs × 8 bytes = 64 bytes
    
    IntPtr vpn_base;       // Base VPN for this block (VPN & ~7)
    IntPtr ppn[ENTRIES_PER_BLOCK];  // Physical page numbers
    bool valid[ENTRIES_PER_BLOCK];   // Valid bits per entry
    UInt16 asid;           // Address space identifier
    int page_size;         // Page size (12=4KB, 21=2MB)
    bool is_nested;        // True for nested TLB entries (virtualization)
    SubsecondTime insert_time;  // For LRU/SRRIP tracking
    
    TLBBlock();
    void invalidate();
    bool lookupEntry(IntPtr vpn, IntPtr& out_ppn) const;
    void insertEntry(IntPtr vpn, IntPtr ppn);
};

// ============================================================================
// L2 Cache TLB Extension
// ============================================================================
// Extends the L2 cache to support TLB block storage and lookup
// ============================================================================
class L2CacheTLBExtension
{
public:
    L2CacheTLBExtension(Core* core, MemoryManagerBase* memory_manager);
    ~L2CacheTLBExtension();
    
    // Configuration
    void configure(UInt32 max_tlb_blocks, SubsecondTime lookup_latency);
    
    // TLB block operations
    bool lookupTLBBlock(IntPtr vpn, UInt16 asid, IntPtr& out_ppn, 
                        SubsecondTime& out_latency, bool& out_is_4kb);
    void insertTLBBlock(IntPtr vpn_base, UInt16 asid, int page_size,
                        const IntPtr* ppns, const bool* valid, bool is_nested);
    void invalidateTLBBlock(IntPtr vpn_base, UInt16 asid);
    void invalidateByASID(UInt16 asid);
    void invalidateAll();
    
    // Statistics
    UInt64 getTLBBlockCount() const { return m_tlb_blocks.size(); }
    UInt64 getLookupHits() const { return m_stats.lookup_hits; }
    UInt64 getLookupMisses() const { return m_stats.lookup_misses; }
    
private:
    Core* m_core;
    MemoryManagerBase* m_memory_manager;
    
    // TLB blocks stored in L2 cache
    // Key: (vpn_base, asid) -> TLBBlock
    std::unordered_map<UInt64, TLBBlock> m_tlb_blocks;
    
    // Configuration
    UInt32 m_max_tlb_blocks;
    SubsecondTime m_lookup_latency;
    
    // Statistics
    struct {
        UInt64 lookup_hits;
        UInt64 lookup_misses;
        UInt64 insertions;
        UInt64 evictions;
        UInt64 invalidations;
    } m_stats;
    
    // Helper to create key from vpn_base and asid
    UInt64 makeKey(IntPtr vpn_base, UInt16 asid) const;
    
    // Eviction policy (TLB-aware SRRIP)
    void evictIfNeeded();
};

// ============================================================================
// Victima MMU
// ============================================================================
class MemoryManagementUnitVictima : public MemoryManagementUnitBase
{
public:
    MemoryManagementUnitVictima(Core* core, MemoryManagerBase* memory_manager,
                                 ShmemPerfModel* shmem_perf_model, String name,
                                 MemoryManagementUnitBase* nested_mmu);
    ~MemoryManagementUnitVictima();
    
    // Required interface methods
    void discoverVMAs() override;
    void registerMMUStats() override;
    IntPtr performAddressTranslation(IntPtr eip, IntPtr address, bool instruction,
                                     Core::lock_signal_t lock, bool modeled, bool count) override;
    PTWResult filterPTWResult(IntPtr address, PTWResult ptw_result,
                              PageTable* page_table, bool count) override;
    
    // Victima-specific methods
    void handleL2TLBMiss(IntPtr vpn, UInt16 asid, IntPtr ppn, int page_size,
                         bool had_dram_access);
    void handleL2TLBEviction(IntPtr vpn, UInt16 asid, int page_size);
    
private:
    // Core components
    MemoryManagerBase* m_memory_manager;
    TLBHierarchy* m_tlb_subsystem;
    MSHR* m_pt_walkers;
    BaseFilter* m_ptw_filter;
    
    // Victima-specific components
    PTWCostPredictor* m_ptw_cost_predictor;
    L2CacheTLBExtension* m_l2_tlb_extension;
    
    // Configuration
    bool m_victima_enabled;
    bool m_insert_on_miss;     // Insert TLB block on L2 TLB miss
    bool m_insert_on_eviction; // Insert TLB block on L2 TLB eviction
    UInt32 m_l2_cache_mpki_threshold;  // Bypass predictor if MPKI > threshold
    
    // L2 cache MPKI tracking
    UInt64 m_l2_cache_accesses;
    UInt64 m_l2_cache_misses;
    UInt32 m_current_l2_mpki;
    SubsecondTime m_last_mpki_update;
    static const UInt64 MPKI_WINDOW_INSTRUCTIONS = 1000;
    
    // Logging
    SimLog* m_mmu_log;
    
    // Statistics
    struct {
        // Basic translation stats
        UInt64 num_translations;
        UInt64 page_faults;
        UInt64 page_table_walks;
        
        // TLB stats
        SubsecondTime total_walk_latency;
        SubsecondTime total_translation_latency;
        SubsecondTime total_tlb_latency;
        SubsecondTime total_fault_latency;
        SubsecondTime* tlb_latency_per_level;
        UInt64* tlb_hit_page_sizes;
        
        // Victima-specific stats
        UInt64 l2_tlb_block_lookups;
        UInt64 l2_tlb_block_hits;
        UInt64 l2_tlb_block_misses;
        UInt64 l2_tlb_block_insertions;
        UInt64 l2_tlb_block_evictions;
        UInt64 ptw_cost_predictions_positive;
        UInt64 ptw_cost_predictions_negative;
        UInt64 ptw_cost_predictions_bypassed;  // Due to low L2 cache MPKI
        SubsecondTime l2_tlb_block_hit_latency_saved;
        
    } m_translation_stats;
    
    // Initialization helpers
    void instantiatePageTableWalker();
    void instantiateTLBSubsystem();
    void initializeVictimaComponents();
    
    // Helper methods
    void updateL2CacheMPKI(bool was_miss, UInt64 instruction_count);
    bool shouldInsertTLBBlock(IntPtr vpn, bool had_dram_access);
    void fetchPTEBlockForEvictedEntry(IntPtr vpn, UInt16 asid, int page_size);
};

}  // namespace ParametricDramDirectoryMSI
