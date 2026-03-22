/* 0 -> no debug, 1 -> debug, 2 -> detailed debug */

#pragma once

/* DEBUG levels */
#define DEBUG_NONE     0
#define DEBUG_BASIC    1
#define DEBUG_DETAILED 2

/* Unified log file - when enabled, all components also write to a single file for timing analysis */
#define UNIFIED_LOG_ENABLED  0  /* 0 = disabled, 1 = enabled */
#define UNIFIED_LOG_FILENAME "unified_debug.log"

// Sniper specific
#define DEBUG_CORE                  DEBUG_NONE  /* 0, 1 or 2 */    // NOTE: very noisy in DETAILED
#define DEBUG_MEM_MANAGER           DEBUG_NONE  /* 0, 1 or 2 */ // NOTE: very noisy in DETAILED
#define LOG_MEM_MANAGER_STATS       0           /* 0 = disabled, 1 = enabled */
#define DEBUG_MMU                   DEBUG_NONE  /* 0, 1 or 2 */
#define ENABLE_MMU_CSV_LOGS         0           // Enable detailed CSV metrics (expensive)

#define DEBUG_MMU_BASE              DEBUG_NONE  /* 0, 1 or 2 */  // Separate flag for mmu_base.cc
#define DEBUG_MAGIC_SERVER          DEBUG_NONE  /* 0, 1 or 2 */
#define DEBUG_TRACE_THREAD          DEBUG_NONE  /* 0, 1 or 2 */
    
// > Page Tables
#define DEBUG_PAGE_TABLE_RADIX      DEBUG_NONE /* 0, 1 or 2 */

// ROB Performance Model specific
#define DEBUG_ROB_TIMER             DEBUG_NONE /* 0, 1 or 2 */
#define DEBUG_PERF_MODEL            DEBUG_NONE /* 0, 1 or 2 */
#define DEBUG_ROB_PERF_MODEL        DEBUG_NONE /* 0, 1 or 2 */

// MimicOS specific
#define DEBUG_VIRTUOS               DEBUG_NONE  /* 0, 1 or 2 */

// common (i.e., phys memory allocators)
#define DEBUG_RESERVATION_THP       DEBUG_NONE  /* 0, 1 or 2 */
#define DEBUG_EXCEPTION_HANDLER     DEBUG_NONE  /* 0, 1 or 2 */

#define DEBUG_BUDDY                 DEBUG_NONE  /* 0, 1 or 2 */

#define DEBUG_BASELINE_ALLOCATOR    DEBUG_NONE /* 0, 1 or 2 */

#define DEBUG_SPOT_ALLOCATOR        DEBUG_NONE /* 0, 1 or 2 */

#define DEBUG_UTOPIA                DEBUG_NONE /* 0, 1 or 2 */ // Utopia allocator logging
#define ENABLE_UTOPIA_MIGRATION_CSV 1          /* 0 = disabled, 1 = enabled */ // Log VPN migrations to CSV

#define DEBUG_TLB                   DEBUG_NONE /* 0, 1 or 2 */

// HugeTLBfs & Swap Cache
#define DEBUG_HUGETLBFS             DEBUG_NONE /* 0, 1 or 2 */
#define DEBUG_SWAP_CACHE            DEBUG_NONE /* 0, 1 or 2 */

// Range-based translation (RMM)
#define DEBUG_RLB                   DEBUG_NONE /* 0, 1 or 2 */ // Range Lookup Buffer logging
#define DEBUG_EAGER_PAGING          DEBUG_NONE /* 0, 1 or 2 */ // Eager paging allocator logging
#define DEBUG_RANGE_TABLE           DEBUG_NONE /* 0, 1 or 2 */ // Range table B-tree logging

// MimicOS
#define DEBUG_MIMICOS               DEBUG_NONE /* 0, 1 or 2 */ // MimicOS userspace framework logging

// MPLRU Adaptive Controller
#define DEBUG_MPLRU                 DEBUG_NONE /* 0, 1 or 2 */ // MPLRU controller logging

// DRAM Performance Model
#define DEBUG_DRAM_DETAILED         DEBUG_NONE /* 0, 1 or 2 */ // Detailed DRAM timing model debug messages
#define DEBUG_DRAM_CSV              0          /* 0 = disabled, 1 = enabled */ // DRAM latency and PTW CSV logging

// L2 Cache Set Index logging
#define DEBUG_L2_SET_INDEX          0 /* 0, 1 or 2 */ // L2 cache set index logging

// L2 Cache Content logging (metadata vs data distribution)
#define DEBUG_L2_CONTENT            0 /* 0, 1 or 2 */ // L2 cache content logging
#define L2_CONTENT_LOG_INTERVAL     10000    // Log every N L2 accesses

// NUCA Cache Content logging (metadata vs data distribution)
#define DEBUG_NUCA_CONTENT          0 /* 0, 1 or 2 */ // NUCA cache content logging
#define NUCA_CONTENT_LOG_INTERVAL   10000    // Log every N NUCA accesses

#define DEBUG_MMU_SPEC              DEBUG_NONE /* 0, 1 or 2 */ // MMU Spec logging
#define ENABLE_MMU_SPEC_CSV_LOGS    0          /* 0 = disabled, 1 = enabled */ // PTW-vs-spec timing CSV (mmu_spec only)

// Speculative prefetch eviction tracking: count demand misses on lines
// evicted by speculative prefetches within this window of L2 demand accesses.
#define SPEC_EVICTION_WINDOW        50
#define DEBUG_MMU_VIRT              DEBUG_NONE /* 0, 1 or 2 */ // MMU Virt logging
#define DEBUG_MMU_POMTLB            DEBUG_NONE /* 0, 1 or 2 */ // MMU POM-TLB logging
#define DEBUG_MMU_RANGE             DEBUG_NONE /* 0, 1 or 2 */ // MMU Range logging
#define DEBUG_MMU_DMT               DEBUG_NONE /* 0, 1 or 2 */ // MMU DMT logging

#define DEBUG_FAST_DETAILED         DEBUG_NONE /* 0, 1 or 2 */ // Fast detailed cache hierarchy logging

#define DEBUG_TRACE_CHAMPSIM        DEBUG_NONE /* 0, 1 or 2 */
#define DEBUG_CHAMPSIM_CACHE        DEBUG_NONE /* 0, 1 or 2 */ // ChampSim instruction cache hits/misses
#define DEBUG_DYNAMIC_MICROOP       DEBUG_NONE /* 0, 1 or 2 */ // DynamicMicroOp alloc/dealloc and refcount

// DEBUG_SIFT_READER is defined & used in sift_reader.cc