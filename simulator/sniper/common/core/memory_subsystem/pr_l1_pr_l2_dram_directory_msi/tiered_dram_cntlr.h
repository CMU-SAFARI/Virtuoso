#pragma once

/**
 * Tiered DRAM Controller with Heterogeneous Memory & NUMA Support
 * 
 * This controller manages access to N heterogeneous memory tiers.
 * Each tier can use any DramPerfModel type (constant, normal, ddr, cxl, etc.)
 * configured via the config file.
 * 
 * NUMA support: When NUMA is enabled, each tier is split into N NUMA nodes,
 * each with its own performance model instance. The controller determines the
 * NUMA node from the physical address (PFN→Node mapping), and uses the 
 * corresponding per-node performance model for latency computation.
 *
 * Configuration example:
 *   [perf_model/dram/tiers]
 *   num_tiers = 2
 *   
 *   [perf_model/dram/tier0]
 *   type = ddr              # Local DDR5
 *   capacity_gb = 16
 *   
 *   [perf_model/dram/tier1]  
 *   type = cxl              # CXL-attached
 *   capacity_gb = 64
 *
 *   [perf_model/dram/numa]
 *   enabled = true
 *   num_nodes = 2
 *   node0/capacity_gb = 64
 *   node0/kernel_reserved_gb = 8
 *   node0/type = ddr
 *   node1/capacity_gb = 64
 *   node1/kernel_reserved_gb = 8
 *   node1/type = ddr
 */

#include "dram_cntlr.h"
#include "dram_perf_model.h"
#include "cxl_memory_tier_manager.h"

#include <vector>
#include <unordered_map>

namespace PrL1PrL2DramDirectoryMSI
{

class TieredDramCntlr : public DramCntlr
{
public:
    /**
     * Per-tier configuration and state
     */
    struct TierConfig
    {
        UInt32 tier_id;
        String type;                    // "constant", "normal", "ddr", "cxl", etc.
        DramPerfModel* perf_model;      // The performance model for this tier
        UInt64 capacity_bytes;
        UInt64 start_address;
        UInt64 end_address;
        HitWhere::where_t hit_where;    // HitWhere value for this tier
        
        // Statistics
        UInt64 reads;
        UInt64 writes;
        SubsecondTime total_latency;
        
        TierConfig() 
            : tier_id(0), perf_model(nullptr), capacity_bytes(0)
            , start_address(0), end_address(0), hit_where(HitWhere::DRAM)
            , reads(0), writes(0), total_latency(SubsecondTime::Zero()) {}
    };

    /**
     * Per-NUMA-node configuration within a tier
     */
    struct NumaNodeInfo
    {
        UInt32 node_id;
        UInt32 tier_id;                 // Which tier this node belongs to
        String type;                    // Memory type ("ddr", "cxl", etc.)
        DramPerfModel* perf_model;      // Per-node performance model instance
        UInt64 capacity_bytes;
        UInt64 kernel_reserved_bytes;   // Kernel reservation for CPU-local apps
        UInt64 start_pfn;               // Start PFN (4KB granularity)
        UInt64 end_pfn;
        HitWhere::where_t hit_where;

        // Per-node statistics
        UInt64 reads;
        UInt64 writes;
        UInt64 local_accesses;          // Accesses from CPU-local core
        UInt64 remote_accesses;         // Accesses from remote cores
        SubsecondTime total_latency;

        NumaNodeInfo()
            : node_id(0), tier_id(0), perf_model(nullptr)
            , capacity_bytes(0), kernel_reserved_bytes(0)
            , start_pfn(0), end_pfn(0), hit_where(HitWhere::DRAM)
            , reads(0), writes(0), local_accesses(0), remote_accesses(0)
            , total_latency(SubsecondTime::Zero()) {}
    };

private:
    // Vector of memory tiers (tier 0 = fastest, tier N = slowest)
    std::vector<TierConfig> m_tiers;
    UInt32 m_num_tiers;
    
    // Memory tier manager for placement decisions
    CXLMemoryTierManager* m_tier_manager;
    
    // Core info
    core_id_t m_core_id;
    
    // Dummy ShmemPerf for write paths (not tracked)
    ShmemPerf m_tiered_dummy_shmem_perf;

    // =========================================================================
    // NUMA support
    // =========================================================================
    bool m_numa_enabled;
    UInt32 m_num_numa_nodes;
    std::vector<NumaNodeInfo> m_numa_nodes;

    // Core-to-NUMA-node mapping (which cores are local to which node)
    std::vector<UInt32> m_core_to_node;   // core_id -> NUMA node_id

    // NUMA remote access latency penalty (additive, in ns)
    SubsecondTime m_numa_remote_latency;

    // Per-NUMA-node statistics (aggregated)
    UInt64* m_numa_node_reads;
    UInt64* m_numa_node_writes;

public:
    TieredDramCntlr(MemoryManagerBase* memory_manager,
                    ShmemPerfModel* shmem_perf_model,
                    UInt32 cache_block_size,
                    AddressHomeLookup* address_home_lookup);
    
    ~TieredDramCntlr();
    
    // Override to add tiering logic
    boost::tuple<SubsecondTime, HitWhere::where_t> 
    getDataFromDram(IntPtr address, core_id_t requester, Byte* data_buf, 
                    SubsecondTime now, ShmemPerf *perf, bool is_metadata) override;
    
    boost::tuple<SubsecondTime, HitWhere::where_t> 
    putDataToDram(IntPtr address, core_id_t requester, Byte* data_buf, 
                  SubsecondTime now, bool is_metadata) override;
    
    // Accessors
    CXLMemoryTierManager* getTierManager() { return m_tier_manager; }
    UInt32 getNumTiers() const { return m_num_tiers; }
    const TierConfig& getTier(UInt32 tier_id) const { return m_tiers[tier_id]; }
    DramPerfModel* getTierPerfModel(UInt32 tier_id) { return m_tiers[tier_id].perf_model; }

    // NUMA accessors
    bool isNumaEnabled() const { return m_numa_enabled; }
    UInt32 getNumNumaNodes() const { return m_num_numa_nodes; }
    const NumaNodeInfo& getNumaNode(UInt32 node_id) const { return m_numa_nodes[node_id]; }
    UInt32 getNumaNodeForAddress(IntPtr address) const;
    UInt32 getNumaNodeForCore(core_id_t core_id) const;
    bool isLocalAccess(core_id_t core_id, IntPtr address) const;

private:
    void initializeTiers(core_id_t core_id, UInt32 cache_block_size, 
                         AddressHomeLookup* address_home_lookup);
    
    void initializeNuma(core_id_t core_id, UInt32 cache_block_size,
                        AddressHomeLookup* address_home_lookup);
    
    UInt32 getTierForAddress(IntPtr address);
    
    SubsecondTime runTieredDramPerfModel(core_id_t requester, SubsecondTime time, 
                                          IntPtr address, 
                                          DramCntlrInterface::access_t access_type, 
                                          ShmemPerf *perf, bool is_metadata);
    
    HitWhere::where_t getHitWhereForTier(UInt32 tier_id, const String& tier_type,
                                          const String& topology = "");
    
    HitWhere::where_t getHitWhereForNumaAccess(UInt32 numa_node_id,
                                                 core_id_t requester) const;
};

} // namespace PrL1PrL2DramDirectoryMSI
