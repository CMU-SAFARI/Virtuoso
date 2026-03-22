#ifndef __CXL_MEMORY_TIER_MANAGER_H__
#define __CXL_MEMORY_TIER_MANAGER_H__

/**
 * CXL Memory Tier Manager
 * 
 * Manages tiered memory allocation between local DRAM (Tier 0) and 
 * CXL-attached memory (Tier 1+). Supports multiple placement policies:
 * 
 * 1. ADDRESS_RANGE: Static partitioning based on physical address ranges
 *    - Addresses below threshold -> Local DRAM
 *    - Addresses above threshold -> CXL memory
 * 
 * 2. FIRST_TOUCH: Pages allocated based on first access location
 *    - First N GB -> Local DRAM
 *    - Overflow -> CXL memory (NUMA-like behavior)
 * 
 * 3. HOT_COLD: Dynamic page migration based on access frequency
 *    - Hot pages promoted to local DRAM
 *    - Cold pages demoted to CXL memory
 *    - Requires page access tracking
 * 
 * 4. INTERLEAVE: Round-robin allocation across tiers
 *    - Similar to NUMA interleaving
 *    - Balances bandwidth across tiers
 * 
 * Based on insights from:
 *   - CXL-DMSim (arXiv:2411.02282)
 *   - TPP: Transparent Page Placement (ASPLOS'23)
 */

#include "fixed_types.h"
#include "subsecond_time.h"
#include "lock.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>

class CXLMemoryTierManager
{
public:
    /**
     * Memory tier enumeration
     */
    enum class Tier : UInt8
    {
        LOCAL_DRAM = 0,   // Fast local DDR memory (Tier 0)
        CXL_NEAR = 1,     // Direct-attached CXL memory (Tier 1)
        CXL_FAR = 2,      // Switch-attached CXL memory (Tier 2)
        CXL_POOLED = 3,   // Pooled/shared CXL memory
        NUM_TIERS
    };

    /**
     * Page placement policy
     */
    enum class PlacementPolicy
    {
        ADDRESS_RANGE,    // Static address-based partitioning
        FIRST_TOUCH,      // First-touch with capacity limits
        HOT_COLD,         // Dynamic hot/cold page migration
        INTERLEAVE,       // Round-robin interleaving
        ALL_LOCAL,        // Everything in local DRAM (baseline)
        ALL_CXL           // Everything in CXL (stress test)
    };

    /**
     * Page information for tracking
     */
    struct PageInfo
    {
        Tier tier;                    // Current tier
        UInt64 access_count;          // Total access count
        SubsecondTime last_access;    // Last access time
        SubsecondTime first_access;   // First access time
        bool is_hot;                  // Hot page flag
        
        PageInfo() 
            : tier(Tier::LOCAL_DRAM)
            , access_count(0)
            , last_access(SubsecondTime::Zero())
            , first_access(SubsecondTime::Zero())
            , is_hot(false) 
        {}
    };

    /**
     * Tier capacity and statistics
     */
    struct TierInfo
    {
        UInt64 capacity_bytes;        // Total capacity
        UInt64 used_bytes;            // Currently used
        UInt64 access_count;          // Total accesses
        SubsecondTime total_latency;  // Cumulative latency
        
        TierInfo()
            : capacity_bytes(0)
            , used_bytes(0)
            , access_count(0)
            , total_latency(SubsecondTime::Zero())
        {}
    };

private:
    //=========================================================================
    // Configuration
    //=========================================================================
    
    bool m_enabled;
    PlacementPolicy m_policy;
    
    // Address range thresholds (for ADDRESS_RANGE policy)
    IntPtr m_local_dram_start;
    IntPtr m_local_dram_end;
    IntPtr m_cxl_start;
    IntPtr m_cxl_end;
    
    // Capacity limits (for FIRST_TOUCH policy)
    UInt64 m_local_dram_capacity;
    UInt64 m_cxl_capacity;
    
    // Hot/cold thresholds (for HOT_COLD policy)
    UInt64 m_hot_threshold;           // Access count to be considered hot
    UInt64 m_cold_threshold;          // Access count to be considered cold
    SubsecondTime m_promotion_interval;  // Minimum time between migrations
    
    // Interleave granularity (for INTERLEAVE policy)
    UInt32 m_interleave_granularity;  // Pages to allocate before switching tier

    //=========================================================================
    // State tracking
    //=========================================================================
    
    // Page tracking (page number -> page info)
    std::unordered_map<IntPtr, PageInfo> m_page_table;
    
    // Tier information
    std::vector<TierInfo> m_tier_info;
    
    // Hot/cold page lists for migration
    std::unordered_set<IntPtr> m_hot_pages;
    std::unordered_set<IntPtr> m_cold_pages;
    
    // Interleave state
    UInt32 m_interleave_counter;
    Tier m_current_interleave_tier;
    
    // Statistics
    UInt64 m_pages_promoted;          // Pages moved DRAM <- CXL
    UInt64 m_pages_demoted;           // Pages moved CXL <- DRAM
    UInt64 m_promotion_failures;      // Failed promotions (DRAM full)
    
    // Thread safety
    Lock m_lock;
    
    // Page size (4KB default)
    static const UInt32 PAGE_SIZE = 4096;
    static const UInt32 PAGE_SIZE_LOG2 = 12;

    //=========================================================================
    // Helper methods
    //=========================================================================
    
    IntPtr getPageNumber(IntPtr address) const
    {
        return address >> PAGE_SIZE_LOG2;
    }
    
    Tier getTierAddressRange(IntPtr address) const;
    Tier getTierFirstTouch(IntPtr page_num);
    Tier getTierHotCold(IntPtr page_num);
    Tier getTierInterleave(IntPtr page_num);
    
    void updatePageAccess(IntPtr page_num, SubsecondTime time);
    bool tryPromotePage(IntPtr page_num);
    bool tryDemotePage(IntPtr page_num);

public:
    CXLMemoryTierManager();
    ~CXLMemoryTierManager();
    
    //=========================================================================
    // Core API
    //=========================================================================
    
    /**
     * Determine which memory tier should handle this address
     */
    Tier getTier(IntPtr address);
    
    /**
     * Record an access to update hotness tracking
     */
    void recordAccess(IntPtr address, SubsecondTime time, bool is_write = false);
    
    /**
     * Check if an address is in CXL memory
     */
    bool isCXLAddress(IntPtr address)
    {
        return getTier(address) != Tier::LOCAL_DRAM;
    }
    
    /**
     * Periodic migration check (called from simulator)
     */
    void checkMigration(SubsecondTime current_time);
    
    //=========================================================================
    // Configuration API
    //=========================================================================
    
    void setPolicy(PlacementPolicy policy) { m_policy = policy; }
    PlacementPolicy getPolicy() const { return m_policy; }
    
    void setLocalDramCapacity(UInt64 bytes) { m_local_dram_capacity = bytes; }
    void setCXLCapacity(UInt64 bytes) { m_cxl_capacity = bytes; }
    
    void setAddressRanges(IntPtr local_start, IntPtr local_end, 
                          IntPtr cxl_start, IntPtr cxl_end);
    
    void setHotColdThresholds(UInt64 hot, UInt64 cold) 
    { 
        m_hot_threshold = hot; 
        m_cold_threshold = cold; 
    }
    
    //=========================================================================
    // Statistics API
    //=========================================================================
    
    UInt64 getPagesPromoted() const { return m_pages_promoted; }
    UInt64 getPagesDemoted() const { return m_pages_demoted; }
    UInt64 getPromotionFailures() const { return m_promotion_failures; }
    
    UInt64 getTierUsedBytes(Tier tier) const 
    { 
        return m_tier_info[static_cast<size_t>(tier)].used_bytes; 
    }
    
    UInt64 getTierAccessCount(Tier tier) const 
    { 
        return m_tier_info[static_cast<size_t>(tier)].access_count; 
    }
    
    double getTierUtilization(Tier tier) const;
    
    /**
     * Get string representation of tier
     */
    static const char* tierToString(Tier tier)
    {
        switch (tier)
        {
            case Tier::LOCAL_DRAM: return "LOCAL_DRAM";
            case Tier::CXL_NEAR:   return "CXL_NEAR";
            case Tier::CXL_FAR:    return "CXL_FAR";
            default:               return "UNKNOWN";
        }
    }
    
    /**
     * Get string representation of policy
     */
    static const char* policyToString(PlacementPolicy policy)
    {
        switch (policy)
        {
            case PlacementPolicy::ADDRESS_RANGE: return "ADDRESS_RANGE";
            case PlacementPolicy::FIRST_TOUCH:   return "FIRST_TOUCH";
            case PlacementPolicy::HOT_COLD:      return "HOT_COLD";
            case PlacementPolicy::INTERLEAVE:    return "INTERLEAVE";
            case PlacementPolicy::ALL_LOCAL:     return "ALL_LOCAL";
            case PlacementPolicy::ALL_CXL:       return "ALL_CXL";
            default:                             return "UNKNOWN";
        }
    }
};

#endif /* __CXL_MEMORY_TIER_MANAGER_H__ */
