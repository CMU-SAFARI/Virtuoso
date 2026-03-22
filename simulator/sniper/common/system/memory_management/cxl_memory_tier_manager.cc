/**
 * CXL Memory Tier Manager Implementation
 * 
 * Manages tiered memory allocation and page migration between
 * local DRAM and CXL-attached memory.
 */

#include "cxl_memory_tier_manager.h"
#include "simulator.h"
#include "config.hpp"
#include "stats.h"
#include "log.h"

// Helper functions for safe config access with defaults
static SInt64 getCfgIntSafe(const String& key, SInt64 default_val)
{
    try {
        return Sim()->getCfg()->getInt(key);
    } catch (...) {
        return default_val;
    }
}

static bool getCfgBoolSafe(const String& key, bool default_val)
{
    try {
        return Sim()->getCfg()->getBool(key);
    } catch (...) {
        return default_val;
    }
}

static String getCfgStringSafe(const String& key, const String& default_val)
{
    try {
        return Sim()->getCfg()->getString(key);
    } catch (...) {
        return default_val;
    }
}

CXLMemoryTierManager::CXLMemoryTierManager()
    : m_enabled(getCfgBoolSafe("perf_model/cxl/tiering/enabled", false))
    , m_policy(PlacementPolicy::ADDRESS_RANGE)
    , m_local_dram_start(0)
    , m_local_dram_end(0)
    , m_cxl_start(0)
    , m_cxl_end(0)
    , m_local_dram_capacity(0)
    , m_cxl_capacity(0)
    , m_hot_threshold(1000)
    , m_cold_threshold(10)
    , m_promotion_interval(SubsecondTime::MS(100))
    , m_interleave_granularity(1)
    , m_interleave_counter(0)
    , m_current_interleave_tier(Tier::LOCAL_DRAM)
    , m_pages_promoted(0)
    , m_pages_demoted(0)
    , m_promotion_failures(0)
{
    // Initialize tier info
    m_tier_info.resize(static_cast<size_t>(Tier::NUM_TIERS));
    
    // Load configuration
    String policy_str = getCfgStringSafe(
        "perf_model/cxl/tiering/policy", "address_range");
    
    if (policy_str == "address_range")
        m_policy = PlacementPolicy::ADDRESS_RANGE;
    else if (policy_str == "first_touch")
        m_policy = PlacementPolicy::FIRST_TOUCH;
    else if (policy_str == "hot_cold")
        m_policy = PlacementPolicy::HOT_COLD;
    else if (policy_str == "interleave")
        m_policy = PlacementPolicy::INTERLEAVE;
    else if (policy_str == "all_local")
        m_policy = PlacementPolicy::ALL_LOCAL;
    else if (policy_str == "all_cxl")
        m_policy = PlacementPolicy::ALL_CXL;
    
    // Load capacity configuration
    // Local DRAM capacity in GB (default 16GB)
    m_local_dram_capacity = getCfgIntSafe(
        "perf_model/cxl/tiering/local_dram_capacity_gb", 16) * 1024ULL * 1024 * 1024;
    
    // CXL capacity in GB (default 64GB)
    m_cxl_capacity = getCfgIntSafe(
        "perf_model/cxl/tiering/cxl_capacity_gb", 64) * 1024ULL * 1024 * 1024;
    
    m_tier_info[static_cast<size_t>(Tier::LOCAL_DRAM)].capacity_bytes = m_local_dram_capacity;
    m_tier_info[static_cast<size_t>(Tier::CXL_NEAR)].capacity_bytes = m_cxl_capacity;
    
    // Load address range configuration
    // CXL address threshold (addresses above this go to CXL)
    m_cxl_start = getCfgIntSafe(
        "perf_model/cxl/tiering/cxl_address_start", 0x100000000000LL);  // 16TB default
    m_local_dram_end = m_cxl_start;
    m_cxl_end = m_cxl_start + m_cxl_capacity;
    
    // Load hot/cold thresholds
    m_hot_threshold = getCfgIntSafe(
        "perf_model/cxl/tiering/hot_threshold", 1000);
    m_cold_threshold = getCfgIntSafe(
        "perf_model/cxl/tiering/cold_threshold", 10);
    
    // Load interleave granularity
    m_interleave_granularity = getCfgIntSafe(
        "perf_model/cxl/tiering/interleave_granularity", 1);
    
    // Register statistics
    registerStatsMetric("cxl_tier", 0, "pages_promoted", &m_pages_promoted);
    registerStatsMetric("cxl_tier", 0, "pages_demoted", &m_pages_demoted);
    registerStatsMetric("cxl_tier", 0, "promotion_failures", &m_promotion_failures);
    
    LOG_PRINT("CXL Memory Tier Manager initialized");
    LOG_PRINT("  Enabled: %s", m_enabled ? "true" : "false");
    LOG_PRINT("  Policy: %s", policyToString(m_policy));
    LOG_PRINT("  Local DRAM capacity: %lu GB", m_local_dram_capacity / (1024ULL * 1024 * 1024));
    LOG_PRINT("  CXL capacity: %lu GB", m_cxl_capacity / (1024ULL * 1024 * 1024));
    LOG_PRINT("  CXL address start: 0x%lx", m_cxl_start);
}

CXLMemoryTierManager::~CXLMemoryTierManager()
{
}

//=============================================================================
// Tier determination based on policy
//=============================================================================

CXLMemoryTierManager::Tier CXLMemoryTierManager::getTier(IntPtr address)
{
    if (!m_enabled)
        return Tier::LOCAL_DRAM;
    
    switch (m_policy)
    {
        case PlacementPolicy::ADDRESS_RANGE:
            return getTierAddressRange(address);
        
        case PlacementPolicy::FIRST_TOUCH:
            return getTierFirstTouch(getPageNumber(address));
        
        case PlacementPolicy::HOT_COLD:
            return getTierHotCold(getPageNumber(address));
        
        case PlacementPolicy::INTERLEAVE:
            return getTierInterleave(getPageNumber(address));
        
        case PlacementPolicy::ALL_LOCAL:
            return Tier::LOCAL_DRAM;
        
        case PlacementPolicy::ALL_CXL:
            return Tier::CXL_NEAR;
        
        default:
            return Tier::LOCAL_DRAM;
    }
}

CXLMemoryTierManager::Tier CXLMemoryTierManager::getTierAddressRange(IntPtr address) const
{
    // Simple address-based partitioning
    // Addresses >= m_cxl_start go to CXL
    if (address >= m_cxl_start)
        return Tier::CXL_NEAR;
    else
        return Tier::LOCAL_DRAM;
}

CXLMemoryTierManager::Tier CXLMemoryTierManager::getTierFirstTouch(IntPtr page_num)
{
    ScopedLock sl(m_lock);
    
    // Check if page already allocated
    auto it = m_page_table.find(page_num);
    if (it != m_page_table.end())
        return it->second.tier;
    
    // First touch - allocate based on capacity
    PageInfo info;
    
    TierInfo& local_tier = m_tier_info[static_cast<size_t>(Tier::LOCAL_DRAM)];
    TierInfo& cxl_tier = m_tier_info[static_cast<size_t>(Tier::CXL_NEAR)];
    
    if (local_tier.used_bytes + PAGE_SIZE <= local_tier.capacity_bytes)
    {
        // Local DRAM has capacity
        info.tier = Tier::LOCAL_DRAM;
        local_tier.used_bytes += PAGE_SIZE;
    }
    else if (cxl_tier.used_bytes + PAGE_SIZE <= cxl_tier.capacity_bytes)
    {
        // Overflow to CXL
        info.tier = Tier::CXL_NEAR;
        cxl_tier.used_bytes += PAGE_SIZE;
    }
    else
    {
        // Both full - default to CXL (allow overcommit)
        info.tier = Tier::CXL_NEAR;
        cxl_tier.used_bytes += PAGE_SIZE;
    }
    
    m_page_table[page_num] = info;
    return info.tier;
}

CXLMemoryTierManager::Tier CXLMemoryTierManager::getTierHotCold(IntPtr page_num)
{
    ScopedLock sl(m_lock);
    
    auto it = m_page_table.find(page_num);
    if (it != m_page_table.end())
        return it->second.tier;
    
    // New page starts in CXL (cold by default), promoted to DRAM when hot
    PageInfo info;
    info.tier = Tier::CXL_NEAR;
    
    TierInfo& cxl_tier = m_tier_info[static_cast<size_t>(Tier::CXL_NEAR)];
    cxl_tier.used_bytes += PAGE_SIZE;
    
    m_page_table[page_num] = info;
    m_cold_pages.insert(page_num);
    
    return info.tier;
}

CXLMemoryTierManager::Tier CXLMemoryTierManager::getTierInterleave(IntPtr page_num)
{
    ScopedLock sl(m_lock);
    
    // Check if page already allocated
    auto it = m_page_table.find(page_num);
    if (it != m_page_table.end())
        return it->second.tier;
    
    // Round-robin interleaving
    PageInfo info;
    info.tier = m_current_interleave_tier;
    
    m_interleave_counter++;
    if (m_interleave_counter >= m_interleave_granularity)
    {
        m_interleave_counter = 0;
        if (m_current_interleave_tier == Tier::LOCAL_DRAM)
            m_current_interleave_tier = Tier::CXL_NEAR;
        else
            m_current_interleave_tier = Tier::LOCAL_DRAM;
    }
    
    TierInfo& tier_info = m_tier_info[static_cast<size_t>(info.tier)];
    tier_info.used_bytes += PAGE_SIZE;
    
    m_page_table[page_num] = info;
    return info.tier;
}

//=============================================================================
// Access tracking
//=============================================================================

void CXLMemoryTierManager::recordAccess(IntPtr address, SubsecondTime time, bool is_write)
{
    if (!m_enabled)
        return;
    
    IntPtr page_num = getPageNumber(address);
    
    ScopedLock sl(m_lock);
    
    auto it = m_page_table.find(page_num);
    if (it != m_page_table.end())
    {
        PageInfo& info = it->second;
        info.access_count++;
        info.last_access = time;
        
        if (info.first_access == SubsecondTime::Zero())
            info.first_access = time;
        
        // Update tier access statistics
        TierInfo& tier_info = m_tier_info[static_cast<size_t>(info.tier)];
        tier_info.access_count++;
        
        // Check for hot/cold status change (for HOT_COLD policy)
        if (m_policy == PlacementPolicy::HOT_COLD)
        {
            if (!info.is_hot && info.access_count >= m_hot_threshold)
            {
                info.is_hot = true;
                m_cold_pages.erase(page_num);
                m_hot_pages.insert(page_num);
            }
        }
    }
}

void CXLMemoryTierManager::updatePageAccess(IntPtr page_num, SubsecondTime time)
{
    auto it = m_page_table.find(page_num);
    if (it != m_page_table.end())
    {
        it->second.access_count++;
        it->second.last_access = time;
    }
}

//=============================================================================
// Page migration
//=============================================================================

bool CXLMemoryTierManager::tryPromotePage(IntPtr page_num)
{
    auto it = m_page_table.find(page_num);
    if (it == m_page_table.end())
        return false;
    
    PageInfo& info = it->second;
    if (info.tier == Tier::LOCAL_DRAM)
        return true;  // Already in local DRAM
    
    TierInfo& local_tier = m_tier_info[static_cast<size_t>(Tier::LOCAL_DRAM)];
    TierInfo& cxl_tier = m_tier_info[static_cast<size_t>(info.tier)];
    
    // Check if local DRAM has capacity
    if (local_tier.used_bytes + PAGE_SIZE > local_tier.capacity_bytes)
    {
        m_promotion_failures++;
        return false;
    }
    
    // Promote page
    cxl_tier.used_bytes -= PAGE_SIZE;
    local_tier.used_bytes += PAGE_SIZE;
    info.tier = Tier::LOCAL_DRAM;
    m_pages_promoted++;
    
    return true;
}

bool CXLMemoryTierManager::tryDemotePage(IntPtr page_num)
{
    auto it = m_page_table.find(page_num);
    if (it == m_page_table.end())
        return false;
    
    PageInfo& info = it->second;
    if (info.tier != Tier::LOCAL_DRAM)
        return true;  // Already in CXL
    
    TierInfo& local_tier = m_tier_info[static_cast<size_t>(Tier::LOCAL_DRAM)];
    TierInfo& cxl_tier = m_tier_info[static_cast<size_t>(Tier::CXL_NEAR)];
    
    // Demote page
    local_tier.used_bytes -= PAGE_SIZE;
    cxl_tier.used_bytes += PAGE_SIZE;
    info.tier = Tier::CXL_NEAR;
    m_pages_demoted++;
    
    return true;
}

void CXLMemoryTierManager::checkMigration(SubsecondTime current_time)
{
    if (!m_enabled || m_policy != PlacementPolicy::HOT_COLD)
        return;
    
    ScopedLock sl(m_lock);
    
    // Promote hot pages from CXL to local DRAM
    std::vector<IntPtr> pages_to_promote;
    for (IntPtr page_num : m_hot_pages)
    {
        auto it = m_page_table.find(page_num);
        if (it != m_page_table.end() && it->second.tier != Tier::LOCAL_DRAM)
        {
            pages_to_promote.push_back(page_num);
        }
    }
    
    for (IntPtr page_num : pages_to_promote)
    {
        tryPromotePage(page_num);
    }
    
    // Demote cold pages from local DRAM to CXL if needed
    TierInfo& local_tier = m_tier_info[static_cast<size_t>(Tier::LOCAL_DRAM)];
    if (local_tier.used_bytes > local_tier.capacity_bytes * 0.9)  // 90% threshold
    {
        std::vector<IntPtr> pages_to_demote;
        for (IntPtr page_num : m_cold_pages)
        {
            auto it = m_page_table.find(page_num);
            if (it != m_page_table.end() && it->second.tier == Tier::LOCAL_DRAM)
            {
                pages_to_demote.push_back(page_num);
            }
        }
        
        for (IntPtr page_num : pages_to_demote)
        {
            if (local_tier.used_bytes <= local_tier.capacity_bytes * 0.8)  // 80% target
                break;
            tryDemotePage(page_num);
        }
    }
}

//=============================================================================
// Configuration and utilities
//=============================================================================

void CXLMemoryTierManager::setAddressRanges(IntPtr local_start, IntPtr local_end,
                                             IntPtr cxl_start, IntPtr cxl_end)
{
    m_local_dram_start = local_start;
    m_local_dram_end = local_end;
    m_cxl_start = cxl_start;
    m_cxl_end = cxl_end;
}

double CXLMemoryTierManager::getTierUtilization(Tier tier) const
{
    const TierInfo& info = m_tier_info[static_cast<size_t>(tier)];
    if (info.capacity_bytes == 0)
        return 0.0;
    return (double)info.used_bytes / info.capacity_bytes;
}
