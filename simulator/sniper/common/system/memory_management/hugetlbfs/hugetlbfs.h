/**
 * @file hugetlbfs.h
 * @brief HugeTLBfs - Huge Page Filesystem OS Service
 *
 * This module simulates Linux's hugetlbfs, a pseudo-filesystem that provides
 * an interface for applications to allocate and use huge pages (2MB or 1GB).
 *
 * Key Features:
 * - Pre-allocated huge page pool management
 * - Per-application huge page quota tracking
 * - Support for 2MB (order-9) and 1GB (order-18) huge pages
 * - Statistics tracking for huge page allocations/deallocations
 *
 * Usage Pattern (mirrors Linux):
 * 1. System reserves a pool of huge pages at boot (configure via config)
 * 2. Applications request huge pages from the pool
 * 3. Pages are returned to the pool on deallocation
 *
 * Configuration (in sniper config):
 *   [perf_model/hugetlbfs]
 *   enabled = true
 *   nr_hugepages_2mb = 1024      # Number of 2MB huge pages in pool
 *   nr_hugepages_1gb = 0         # Number of 1GB huge pages in pool
 *   overcommit = false           # Allow allocations beyond reserved pool
 *
 * @author kanellok
 */

#ifndef HUGETLBFS_H
#define HUGETLBFS_H

#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <cstddef>
#include "fixed_types.h"

// Huge page size constants (in 4KB base pages)
#define HUGEPAGE_2MB_PAGES  512      // 2MB = 512 * 4KB
#define HUGEPAGE_1GB_PAGES  262144   // 1GB = 262144 * 4KB

// Huge page size in bits (for page_size field)
#define HUGEPAGE_2MB_BITS   21       // 2^21 = 2MB
#define HUGEPAGE_1GB_BITS   30       // 2^30 = 1GB

/**
 * @brief HugeTLBfs - Huge Page Filesystem Service
 *
 * Manages a pool of pre-allocated huge pages that applications can
 * request for memory-mapped regions requiring large page support.
 */
class HugeTLBfs {
public:
    /**
     * @brief Huge page size enumeration
     */
    enum class HugePageSize {
        SIZE_2MB = 21,   // 2MB huge pages
        SIZE_1GB = 30    // 1GB huge pages
    };

    /**
     * @brief Information about a single huge page allocation
     */
    struct HugePageInfo {
        IntPtr  base_ppn;       // Base physical page number (in 4KB pages)
        int     app_id;         // Application that owns this page
        IntPtr  vaddr;          // Virtual address mapped to this huge page
        HugePageSize size;      // Size of this huge page
        bool    in_use;         // Whether this page is currently allocated
    };

    /**
     * @brief Statistics for hugetlbfs operations
     */
    struct Stats {
        UInt64 allocations_2mb;         // Successful 2MB allocations
        UInt64 allocations_1gb;         // Successful 1GB allocations
        UInt64 deallocations_2mb;       // 2MB page deallocations
        UInt64 deallocations_1gb;       // 1GB page deallocations
        UInt64 allocation_failures;     // Failed allocation attempts (pool exhausted)
        UInt64 pool_exhausted_events;   // Times the pool was completely empty
    };

    // ========================================================================
    // Construction / Destruction
    // ========================================================================

    /**
     * @brief Construct HugeTLBfs service
     *
     * Reads configuration and initializes the huge page pools.
     */
    HugeTLBfs();

    /**
     * @brief Destructor - cleanup resources
     */
    ~HugeTLBfs();

    // ========================================================================
    // Huge Page Allocation Interface
    // ========================================================================

    /**
     * @brief Allocate a huge page from the pool
     *
     * @param app_id    Application requesting the page
     * @param vaddr     Virtual address to be mapped (for tracking)
     * @param size      Requested huge page size (2MB or 1GB)
     * @return Base physical page number (in 4KB units), or -1 if allocation fails
     */
    IntPtr allocateHugePage(int app_id, IntPtr vaddr, HugePageSize size = HugePageSize::SIZE_2MB);

    /**
     * @brief Deallocate a huge page back to the pool
     *
     * @param base_ppn  Base physical page number of the huge page
     * @param size      Size of the huge page being deallocated
     * @return true if deallocation succeeded, false otherwise
     */
    bool deallocateHugePage(IntPtr base_ppn, HugePageSize size = HugePageSize::SIZE_2MB);

    /**
     * @brief Deallocate all huge pages owned by an application
     *
     * Called when an application exits to reclaim its huge pages.
     *
     * @param app_id    Application ID
     * @return Number of huge pages reclaimed
     */
    int deallocateAllForApp(int app_id);

    // ========================================================================
    // Pool Query Interface
    // ========================================================================

    /**
     * @brief Get number of free huge pages in pool
     *
     * @param size  Huge page size to query
     * @return Number of available huge pages
     */
    UInt64 getFreeHugePages(HugePageSize size = HugePageSize::SIZE_2MB) const;

    /**
     * @brief Get total number of huge pages in pool
     *
     * @param size  Huge page size to query
     * @return Total huge pages (free + allocated)
     */
    UInt64 getTotalHugePages(HugePageSize size = HugePageSize::SIZE_2MB) const;

    /**
     * @brief Get number of huge pages used by an application
     *
     * @param app_id    Application ID
     * @param size      Huge page size to query
     * @return Number of huge pages allocated to this application
     */
    UInt64 getAppHugePages(int app_id, HugePageSize size = HugePageSize::SIZE_2MB) const;

    /**
     * @brief Check if hugetlbfs is enabled
     *
     * @return true if enabled in configuration
     */
    bool isEnabled() const { return m_enabled; }

    /**
     * @brief Get statistics reference
     */
    const Stats& getStats() const { return m_stats; }

    // ========================================================================
    // Debug / Logging
    // ========================================================================

    /**
     * @brief Dump pool state to log file
     */
    void dumpPoolState();

private:
    // ========================================================================
    // Private Members
    // ========================================================================

    bool m_enabled;                     // Whether hugetlbfs is enabled

    // Pool of 2MB huge pages
    std::vector<HugePageInfo> m_pool_2mb;
    UInt64 m_nr_hugepages_2mb;          // Total 2MB pages in pool
    UInt64 m_free_hugepages_2mb;        // Free 2MB pages

    // Pool of 1GB huge pages
    std::vector<HugePageInfo> m_pool_1gb;
    UInt64 m_nr_hugepages_1gb;          // Total 1GB pages in pool
    UInt64 m_free_hugepages_1gb;        // Free 1GB pages

    bool m_overcommit;                  // Allow overcommit beyond pool

    // Per-application tracking: app_id -> count of huge pages
    std::unordered_map<int, UInt64> m_app_usage_2mb;
    std::unordered_map<int, UInt64> m_app_usage_1gb;

    // Statistics
    Stats m_stats;

    // Logging
    std::ofstream m_log_file;
    std::string m_log_file_name;

    // Base physical address for huge page pool (in 4KB pages)
    IntPtr m_pool_base_ppn;

    // ========================================================================
    // Private Helpers
    // ========================================================================

    /**
     * @brief Initialize the huge page pool
     *
     * Called from constructor to set up the pre-allocated pool.
     */
    void initializePool();

    /**
     * @brief Register statistics with Sniper's stats framework
     */
    void registerStats();

    /**
     * @brief Find a free huge page in the pool
     *
     * @param size  Size of huge page to find
     * @return Index into pool vector, or -1 if none available
     */
    int findFreeHugePage(HugePageSize size);
};

#endif // HUGETLBFS_H
