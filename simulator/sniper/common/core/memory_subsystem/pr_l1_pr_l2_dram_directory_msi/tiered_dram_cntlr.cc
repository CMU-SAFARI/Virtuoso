/**
 * Tiered DRAM Controller with Heterogeneous Memory & NUMA Support
 * 
 * Supports N configurable memory tiers, each using any DramPerfModel type.
 * When NUMA is enabled, each tier/node combination gets its own perf model.
 */

#include "tiered_dram_cntlr.h"
#include "hit_where.h"
#include "memory_manager.h"
#include "core.h"
#include "simulator.h"
#include "config.hpp"
#include "stats.h"
#include "shmem_perf.h"
#include "log.h"
#include "mimicos.h"

#include <cstring>

// Helper functions for safe config access
static SInt64 getCfgIntSafe(const String& key, SInt64 default_val)
{
    if (Sim()->getCfg()->hasKey(key))
        return Sim()->getCfg()->getInt(key);
    return default_val;
}

static String getCfgStringSafe(const String& key, const String& default_val)
{
    if (Sim()->getCfg()->hasKey(key))
        return Sim()->getCfg()->getString(key);
    return default_val;
}

static bool getCfgBoolSafe(const String& key, bool default_val)
{
    if (Sim()->getCfg()->hasKey(key))
        return Sim()->getCfg()->getBool(key);
    return default_val;
}

namespace PrL1PrL2DramDirectoryMSI
{

TieredDramCntlr::TieredDramCntlr(MemoryManagerBase* memory_manager,
                                   ShmemPerfModel* shmem_perf_model,
                                   UInt32 cache_block_size,
                                   AddressHomeLookup* address_home_lookup)
    : DramCntlr(memory_manager, shmem_perf_model, cache_block_size, address_home_lookup)
    , m_num_tiers(0)
    , m_tier_manager(nullptr)
    , m_core_id(memory_manager->getCore()->getId())
    , m_numa_enabled(false)
    , m_num_numa_nodes(0)
    , m_numa_remote_latency(SubsecondTime::Zero())
    , m_numa_node_reads(nullptr)
    , m_numa_node_writes(nullptr)
{
    // Initialize the memory tiers from config
    initializeTiers(m_core_id, cache_block_size, address_home_lookup);
    
    // NUMA-only mode: when NUMA is enabled but CXL is not, disable complex tiering
    bool cxl_enabled = getCfgBoolSafe("perf_model/cxl/enabled", false);
    bool numa_only = getCfgBoolSafe("perf_model/dram/numa/enabled", false) && !cxl_enabled;
    
    // Create tier manager for placement decisions (skip in NUMA-only mode)
    bool tiering_enabled = getCfgBoolSafe("perf_model/dram/tiering/enabled", !numa_only);
    if (tiering_enabled && m_num_tiers > 1 && !numa_only)
    {
        m_tier_manager = new CXLMemoryTierManager();
    }
    
    // Initialize NUMA support
    m_numa_enabled = getCfgBoolSafe("perf_model/dram/numa/enabled", false);
    if (m_numa_enabled)
    {
        initializeNuma(m_core_id, cache_block_size, address_home_lookup);
    }
    
    LOG_PRINT("TieredDramCntlr: Initialized %u memory tiers, NUMA=%s (%u nodes) for core %d", 
              m_num_tiers, m_numa_enabled ? "enabled" : "disabled", m_num_numa_nodes, m_core_id);
}

TieredDramCntlr::~TieredDramCntlr()
{
    // Delete all tier performance models (except tier 0 which is parent's)
    for (UInt32 i = 1; i < m_num_tiers; ++i)
    {
        if (m_tiers[i].perf_model)
        {
            delete m_tiers[i].perf_model;
        }
    }
    
    // Delete NUMA node perf models
    for (auto& node : m_numa_nodes)
    {
        // Don't delete if it was shared with a tier (tier 0 / parent model)
        bool is_shared = false;
        for (UInt32 i = 0; i < m_num_tiers; ++i)
            if (m_tiers[i].perf_model == node.perf_model) { is_shared = true; break; }
        if (!is_shared && node.perf_model)
            delete node.perf_model;
    }
    
    if (m_tier_manager)
        delete m_tier_manager;
    
    if (m_numa_node_reads)
        delete[] m_numa_node_reads;
    if (m_numa_node_writes)
        delete[] m_numa_node_writes;
}

void TieredDramCntlr::initializeTiers(core_id_t core_id, UInt32 cache_block_size,
                                       AddressHomeLookup* address_home_lookup)
{
    // Get number of tiers from config (default: 2 for backward compatibility)
    m_num_tiers = getCfgIntSafe("perf_model/dram/tiers/num_tiers", 2);
    
    if (m_num_tiers < 1)
        m_num_tiers = 1;
    
    m_tiers.resize(m_num_tiers);
    
    UInt64 current_address = 0;
    
    for (UInt32 i = 0; i < m_num_tiers; ++i)
    {
        TierConfig& tier = m_tiers[i];
        tier.tier_id = i;
        
        // Build config path for this tier
        String tier_path = String("perf_model/dram/tier") + std::to_string(i).c_str();
        
        // Get tier type (default: tier0 = "ddr", others = "cxl")
        String default_type = (i == 0) ? "ddr" : "cxl";
        tier.type = getCfgStringSafe(tier_path + "/type", default_type);
        
        // Get tier capacity
        SInt64 default_capacity_gb = (i == 0) ? 16 : 64;
        tier.capacity_bytes = getCfgIntSafe(tier_path + "/capacity_gb", default_capacity_gb) 
                              * 1024ULL * 1024 * 1024;
        
        // Set address range for this tier
        tier.start_address = current_address;
        tier.end_address = current_address + tier.capacity_bytes;
        current_address = tier.end_address;
        
        // Create or assign performance model
        if (i == 0)
        {
            // Tier 0 uses the parent's DRAM model (already created in DramCntlr)
            tier.perf_model = getDramPerfModel();
        }
        else
        {
            // Create a new DramPerfModel using the factory
            String tier_suffix = String("-tier") + std::to_string(i).c_str();
            tier.perf_model = DramPerfModel::createDramPerfModel(
                core_id, cache_block_size, address_home_lookup, tier.type, tier_suffix);
        }
        
        // Determine HitWhere value for this tier (can be configured per-tier)
        String tier_topology = getCfgStringSafe(tier_path + "/topology", "");
        tier.hit_where = getHitWhereForTier(i, tier.type, tier_topology);
        
        // Initialize statistics
        tier.reads = 0;
        tier.writes = 0;
        tier.total_latency = SubsecondTime::Zero();
        
        // Register per-tier statistics
        String tier_name = String("tier") + std::to_string(i).c_str();
        registerStatsMetric("dram", core_id, (tier_name + "_reads").c_str(), &tier.reads);
        registerStatsMetric("dram", core_id, (tier_name + "_writes").c_str(), &tier.writes);
        
        LOG_PRINT("  Tier %u: type=%s, capacity=%lu GB, addresses=0x%lx-0x%lx, hit_where=%d",
                  i, tier.type.c_str(), tier.capacity_bytes / (1024ULL * 1024 * 1024),
                  tier.start_address, tier.end_address, tier.hit_where);
    }
}

HitWhere::where_t TieredDramCntlr::getHitWhereForTier(UInt32 tier_id, const String& tier_type,
                                                       const String& topology)
{
    // If topology is explicitly configured, use it
    if (!topology.empty())
    {
        if (topology == "local")
            return HitWhere::DRAM_LOCAL;
        else if (topology == "remote")
            return HitWhere::DRAM_REMOTE;
        else if (topology == "cxl_near" || topology == "near")
            return HitWhere::CXL_NEAR;
        else if (topology == "cxl_far" || topology == "far")
            return HitWhere::CXL_FAR;
        else if (topology == "cxl_pooled" || topology == "pooled")
            return HitWhere::CXL_POOLED;
    }
    
    // Fallback: infer from tier type and ID
    if (tier_type == "cxl")
    {
        // CXL tiers get specific CXL HitWhere values based on tier ID
        if (tier_id == 1)
            return HitWhere::CXL_NEAR;
        else if (tier_id == 2)
            return HitWhere::CXL_FAR;
        else
            return HitWhere::CXL_POOLED;
    }
    else
    {
        // Non-CXL tiers
        if (tier_id == 0)
            return HitWhere::DRAM_LOCAL;
        else
            return HitWhere::DRAM_REMOTE;
    }
}

UInt32 TieredDramCntlr::getTierForAddress(IntPtr address)
{
    // If we have a tier manager, use its policy
    if (m_tier_manager)
    {
        CXLMemoryTierManager::Tier mgr_tier = m_tier_manager->getTier(address);
        UInt32 tier_id = static_cast<UInt32>(mgr_tier);
        if (tier_id < m_num_tiers)
            return tier_id;
    }
    
    // Fallback: use address range mapping
    for (UInt32 i = 0; i < m_num_tiers; ++i)
    {
        if (address >= m_tiers[i].start_address && address < m_tiers[i].end_address)
            return i;
    }
    
    // Default to tier 0 if address doesn't match any tier
    return 0;
}

boost::tuple<SubsecondTime, HitWhere::where_t>
TieredDramCntlr::getDataFromDram(IntPtr address, core_id_t requester, 
                                  Byte* data_buf, SubsecondTime now, 
                                  ShmemPerf *perf, bool is_metadata)
{
    // Run tiered performance model
    SubsecondTime dram_access_latency = runTieredDramPerfModel(
        requester, now, address, READ, perf, is_metadata);
    
    HitWhere::where_t hit_where;
    
    if (m_numa_enabled)
    {
        // NUMA: determine hit_where based on node locality
        UInt32 numa_node = getNumaNodeForAddress(address);
        hit_where = getHitWhereForNumaAccess(numa_node, requester);
        if (numa_node < m_num_numa_nodes)
            ++m_numa_nodes[numa_node].reads;
    }
    else
    {
        // Non-NUMA: use tier-based hit_where
        UInt32 tier_id = getTierForAddress(address);
        hit_where = m_tiers[tier_id].hit_where;
        ++m_tiers[tier_id].reads;
    }
    
    return boost::tuple<SubsecondTime, HitWhere::where_t>(dram_access_latency, hit_where);
}

boost::tuple<SubsecondTime, HitWhere::where_t>
TieredDramCntlr::putDataToDram(IntPtr address, core_id_t requester, 
                                Byte* data_buf, SubsecondTime now, 
                                bool is_metadata)
{
    // Run tiered performance model
    SubsecondTime dram_access_latency = runTieredDramPerfModel(
        requester, now, address, WRITE, &m_tiered_dummy_shmem_perf, is_metadata);
    
    HitWhere::where_t hit_where;
    
    if (m_numa_enabled)
    {
        UInt32 numa_node = getNumaNodeForAddress(address);
        hit_where = getHitWhereForNumaAccess(numa_node, requester);
        if (numa_node < m_num_numa_nodes)
            ++m_numa_nodes[numa_node].writes;
    }
    else
    {
        UInt32 tier_id = getTierForAddress(address);
        hit_where = m_tiers[tier_id].hit_where;
        ++m_tiers[tier_id].writes;
    }
    
    return boost::tuple<SubsecondTime, HitWhere::where_t>(dram_access_latency, hit_where);
}

SubsecondTime
TieredDramCntlr::runTieredDramPerfModel(core_id_t requester, SubsecondTime time, 
                                         IntPtr address, 
                                         DramCntlrInterface::access_t access_type, 
                                         ShmemPerf *perf, bool is_metadata)
{
    UInt64 pkt_size = getCacheBlockSize();
    
    // Determine which tier handles this address
    UInt32 tier_id = getTierForAddress(address);
    TierConfig& tier = m_tiers[tier_id];
    
    // Record access for hotness tracking (if tier manager exists)
    if (m_tier_manager)
    {
        m_tier_manager->recordAccess(address, time, access_type == WRITE);
    }
    
    SubsecondTime access_latency;
    
    // =========================================================================
    // NUMA-aware access latency
    // =========================================================================
    if (m_numa_enabled)
    {
        UInt32 numa_node = getNumaNodeForAddress(address);
        
        if (numa_node < m_num_numa_nodes && m_numa_nodes[numa_node].perf_model)
        {
            // Use per-NUMA-node performance model
            access_latency = m_numa_nodes[numa_node].perf_model->getAccessLatency(
                time, pkt_size, requester, address, access_type, perf, is_metadata);
            
            // Add remote access penalty if cross-node
            bool is_local = isLocalAccess(requester, address);
            if (!is_local)
            {
                access_latency += m_numa_remote_latency;
                m_numa_nodes[numa_node].remote_accesses++;
            }
            else
            {
                m_numa_nodes[numa_node].local_accesses++;
            }
            
            // Update per-node stats
            m_numa_nodes[numa_node].total_latency += access_latency;
            if (access_type == READ)
                m_numa_node_reads[numa_node]++;
            else
                m_numa_node_writes[numa_node]++;
        }
        else
        {
            // Fallback to tier model
            access_latency = tier.perf_model->getAccessLatency(
                time, pkt_size, requester, address, access_type, perf, is_metadata);
        }
    }
    else
    {
        // Non-NUMA: use the tier's performance model directly
        access_latency = tier.perf_model->getAccessLatency(
            time, pkt_size, requester, address, access_type, perf, is_metadata);
    }
    
    // Update tier statistics
    tier.total_latency += access_latency;
    
    return access_latency;
}

// =============================================================================
// NUMA initialization and helpers
// =============================================================================

void TieredDramCntlr::initializeNuma(core_id_t core_id, UInt32 cache_block_size,
                                      AddressHomeLookup* address_home_lookup)
{
    m_num_numa_nodes = getCfgIntSafe("perf_model/dram/numa/num_nodes", 2);
    
    if (m_num_numa_nodes < 1) m_num_numa_nodes = 1;
    
    m_numa_nodes.resize(m_num_numa_nodes);
    
    // Remote access latency penalty (in ns, converted to SubsecondTime)
    SInt64 remote_latency_ns = getCfgIntSafe("perf_model/dram/numa/remote_latency_ns", 40);
    m_numa_remote_latency = SubsecondTime::NS(remote_latency_ns);
    
    // Core-to-node mapping: default is round-robin
    // Config: perf_model/dram/numa/cores_per_node (default: total_cores / num_nodes)
    UInt32 total_cores = Sim()->getConfig()->getApplicationCores();
    UInt32 cores_per_node = getCfgIntSafe("perf_model/dram/numa/cores_per_node", 
                                           total_cores / m_num_numa_nodes);
    if (cores_per_node == 0) cores_per_node = 1;
    
    m_core_to_node.resize(total_cores);
    for (UInt32 c = 0; c < total_cores; ++c)
        m_core_to_node[c] = c / cores_per_node;
    
    // Allocate per-node stats
    m_numa_node_reads = new UInt64[m_num_numa_nodes]();
    m_numa_node_writes = new UInt64[m_num_numa_nodes]();
    
    UInt64 current_pfn = 0;
    
    for (UInt32 n = 0; n < m_num_numa_nodes; ++n)
    {
        NumaNodeInfo& node = m_numa_nodes[n];
        node.node_id = n;
        
        // Build config path
        String node_path = String("perf_model/dram/numa/node") + std::to_string(n).c_str();
        
        // Node memory type
        String default_type = (m_num_tiers > 0) ? m_tiers[0].type : String("ddr");
        node.type = getCfgStringSafe(node_path + "/type", default_type);
        
        // Node capacity
        SInt64 default_capacity_gb = 64;
        node.capacity_bytes = getCfgIntSafe(node_path + "/capacity_gb", default_capacity_gb) 
                              * 1024ULL * 1024 * 1024;
        
        // Kernel reservation per node (for CPU-local apps)
        SInt64 default_kernel_gb = 8;
        node.kernel_reserved_bytes = getCfgIntSafe(node_path + "/kernel_reserved_gb", default_kernel_gb) 
                                     * 1024ULL * 1024 * 1024;
        
        // Which tier this node belongs to
        node.tier_id = getCfgIntSafe(node_path + "/tier_id", 0);
        if (node.tier_id >= m_num_tiers) node.tier_id = 0;
        
        // PFN range
        UInt64 node_pages = node.capacity_bytes / 4096;
        node.start_pfn = current_pfn;
        node.end_pfn = current_pfn + node_pages;
        current_pfn = node.end_pfn;
        
        // Create per-node performance model
        if (n == 0 && node.type == m_tiers[0].type)
        {
            // Node 0 can share tier 0's perf model
            node.perf_model = getDramPerfModel();
        }
        else
        {
            String node_suffix = String("-numa") + std::to_string(n).c_str();
            node.perf_model = DramPerfModel::createDramPerfModel(
                core_id, cache_block_size, address_home_lookup, node.type, node_suffix);
        }
        
        // Determine HitWhere for this node
        node.hit_where = getHitWhereForTier(node.tier_id, node.type, "");
        
        // Initialize stats
        node.reads = 0;
        node.writes = 0;
        node.local_accesses = 0;
        node.remote_accesses = 0;
        node.total_latency = SubsecondTime::Zero();
        
        // Register per-NUMA-node statistics
        String node_name = String("numa_node") + std::to_string(n).c_str();
        registerStatsMetric("dram", core_id, (node_name + "_reads").c_str(), &m_numa_node_reads[n]);
        registerStatsMetric("dram", core_id, (node_name + "_writes").c_str(), &m_numa_node_writes[n]);
        registerStatsMetric("dram", core_id, (node_name + "_local_accesses").c_str(), &node.local_accesses);
        registerStatsMetric("dram", core_id, (node_name + "_remote_accesses").c_str(), &node.remote_accesses);
        
        LOG_PRINT("  NUMA Node %u: type=%s, capacity=%lu GB, kernel=%lu GB, PFN=0x%lx-0x%lx, tier=%u",
                  n, node.type.c_str(), 
                  node.capacity_bytes / (1024ULL * 1024 * 1024),
                  node.kernel_reserved_bytes / (1024ULL * 1024 * 1024),
                  node.start_pfn, node.end_pfn, node.tier_id);
    }
}

UInt32 TieredDramCntlr::getNumaNodeForAddress(IntPtr address) const
{
    if (!m_numa_enabled) return 0;
    
    UInt64 ppn = address >> 12;
    
    // Primary: query the allocator via MimicOS (authoritative source of truth)
    MimicOS* os = Sim()->getMimicOS();
    if (os)
    {
        return os->getNumaNodeForPPN(ppn);
    }
    
    // Fallback: static PFN-range lookup (used during early init before MimicOS exists)
    for (UInt32 n = 0; n < m_num_numa_nodes; ++n)
    {
        if (ppn >= m_numa_nodes[n].start_pfn && ppn < m_numa_nodes[n].end_pfn)
            return n;
    }
    
    return 0; // fallback to node 0
}

UInt32 TieredDramCntlr::getNumaNodeForCore(core_id_t core_id) const
{
    if (!m_numa_enabled) return 0;
    
    if (core_id >= 0 && (UInt32)core_id < m_core_to_node.size())
        return m_core_to_node[core_id];
    
    return 0;
}

bool TieredDramCntlr::isLocalAccess(core_id_t core_id, IntPtr address) const
{
    if (!m_numa_enabled) return true;
    return getNumaNodeForCore(core_id) == getNumaNodeForAddress(address);
}

HitWhere::where_t TieredDramCntlr::getHitWhereForNumaAccess(UInt32 numa_node_id,
                                                              core_id_t requester) const
{
    if (!m_numa_enabled || numa_node_id >= m_num_numa_nodes)
        return HitWhere::DRAM;
    
    bool is_local = (getNumaNodeForCore(requester) == numa_node_id);
    
    const NumaNodeInfo& node = m_numa_nodes[numa_node_id];
    
    if (node.type == "cxl")
    {
        if (numa_node_id == 1)      return HitWhere::CXL_NEAR;
        else if (numa_node_id == 2) return HitWhere::CXL_FAR;
        else                        return HitWhere::CXL_POOLED;
    }
    
    return is_local ? HitWhere::DRAM_LOCAL : HitWhere::DRAM_REMOTE;
}

} // namespace PrL1PrL2DramDirectoryMSI
