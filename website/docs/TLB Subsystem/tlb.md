# Translation Lookaside Buffer (TLB) Overview

This document provides an overview of a **Translation Lookaside Buffer (TLB)** implementation within the ParametricDramDirectoryMSI namespace. The TLB code, as well as its interaction with caches, prefetchers, and related memory subsystems, is described here. The goal of this TLB is to cache virtual-to-physical page translations, accelerating address translation requests and reducing memory access latencies.

---

## Table of Contents

1. [Introduction](#introduction)
2. [Data Structures and Classes](#data-structures-and-classes)
3. [Key Functions](#key-functions)
    - [Constructor](#constructor)
    - [lookup()](#lookup)
    - [allocate()](#allocate)
4. [Statistics Tracking](#statistics-tracking)
5. [Prefetching Support](#prefetching-support)
6. [Code Listing](#code-listing)
7. [Usage Example](#usage-example)
8. [Conclusion](#conclusion)

---

## Introduction

A **Translation Lookaside Buffer (TLB)** is a critical hardware (or hardware-assisted) structure used for address translation in modern computer architectures. When an application references a virtual address, the TLB quickly looks up the corresponding physical address (if one exists in the TLB), saving time compared to performing a full page table walk in memory.

In this code, you will see how we:
- Instantiate a TLB with different page sizes.
- Use the TLB to check for hits and misses.
- Handle TLB fills and evictions.
- Integrate with other memory system components like prefetchers and caches.

---

## Data Structures and Classes

### `TLB`
The core class representing our translation lookaside buffer. It includes:
- **Cache**: An internal cache-like structure that stores address-to-page translations.
- **Statistics**: Counters for hits, misses, evictions, etc.
- **Prefetchers**: References to optional prefetcher structures that can be enabled if desired.

### `CacheBlockInfo`
A supporting class that typically holds metadata about an individual cache/TLB line (e.g., its tag, page size, replacement state).

### `SubsecondTime`
A utility class (not fully shown) that measures time in subsecond resolution, used to keep track of timing events like cache hits/misses and TLB lookups.

### `PageTable`
A hypothetical page table class. The TLB can consult the page table for translations when needed.

### Other Includes
- **`stats.h`**: Provides statistics tracking.
- **`config.hpp`**: Configurations for the system (e.g., cache/TLB parameters).
- **`cache_cntlr.h`**: Manages cache-level operations.
- **`memory_manager.h`**: Orchestrates higher-level memory processes.
- **`core_manager.h`**: Manages CPU cores and associated resources.
- **`cache_set.h`, `cache_base.h`, `utils.h`, `log.h`, `rng.h`, `address_home_lookup.h`, `fault_injection.h`**: Various helper modules for logging, random number generation, fault injection, address calculations, etc.

---

## Key Functions

### Constructor

```cpp
TLB::TLB(String name,
         String cfgname,
         core_id_t core_id,
         ComponentLatency access_latency,
         UInt32 num_entries,
         UInt32 associativity,
         int *page_size_list,
         int page_sizes,
         String tlb_type,
         bool allocate_on_miss,
         bool prefetch,
         TLBPrefetcherBase **tpb,
         int _number_of_prefetchers,
         int _max_prefetch_count)
   : m_size(num_entries),
     m_core_id(core_id),
     m_name(name),
     m_associativity(associativity),
     m_cache(name + "_cache", cfgname, core_id, num_entries / associativity,
             associativity, (1L << 3), "lru", CacheBase::PR_L1_CACHE,
             CacheBase::HASH_MASK, NULL, NULL, true,
             page_size_list, page_sizes),
     m_type(tlb_type),
     m_page_size_list(NULL),
     m_page_sizes(page_sizes),
     m_allocate_miss(allocate_on_miss),
     m_prefetch(prefetch),
     prefetchers(tpb),
     number_of_prefetchers(_number_of_prefetchers),
     max_prefetch_count(_max_prefetch_count),
     m_access_latency(access_latency)
{
    // Ensure valid TLB configuration
    LOG_ASSERT_ERROR((num_entries / associativity) * associativity == num_entries,
                     "Invalid TLB configuration: num_entries(%d) must be a multiple of the associativity(%d)",
                     num_entries, associativity);

    // Allocate array of page sizes
    m_page_size_list = std::unique_ptr<int[]>(new int[m_page_sizes]);
    for (int i = 0; i < m_page_sizes; i++) {
        m_page_size_list[i] = page_size_list[i];
    }

    // Zero out TLB statistics and register them
    bzero(&tlb_stats, sizeof(tlb_stats));
    registerStatsMetric(name, core_id, "access", &tlb_stats.m_access);
    registerStatsMetric(name, core_id, "eviction", &tlb_stats.m_eviction);
    registerStatsMetric(name, core_id, "miss", &tlb_stats.m_miss);
}
### Parameters

- **name, cfgname**: Identifiers for the TLB and its configuration.
- **core_id**: ID of the core this TLB is attached to.
- **access_latency**: Latency for accesses to this TLB.
- **num_entries, associativity**: Overall capacity and associativity.
- **page_size_list**: List of supported page sizes.
- **tlb_type**: A string descriptor (e.g. "L1_TLB", "L2_TLB").
- **allocate_on_miss**: Flag that determines if we insert a new translation on miss.
- **prefetch**: Enables or disables any attached TLB prefetchers.
- **prefetchers**: Array of TLB prefetcher objects.
- **_number_of_prefetchers, _max_prefetch_count**: Additional prefetcher configuration.

### Notes

- Instantiates the TLB’s internal cache structure.
- Registers TLB stats like misses, hits, and evictions.

### `lookup()`

```cpp
CacheBlockInfo* TLB::lookup(IntPtr address,
                            SubsecondTime now,
                            bool model_count,
                            Core::lock_signal_t lock_signal,
                            IntPtr eip,
                            bool modeled,
                            bool count,
                            PageTable* pt)
{
    if (model_count)
        tlb_stats.m_access++;

#ifdef DEBUG_TLB
    std::cout << "TLB " << m_name << " Lookup: " << address << std::endl;
#endif

    // Access the internal cache structure for this address
    CacheBlockInfo* hit = m_cache.accessSingleLineTLB(address, Cache::LOAD, NULL, 0, now, true);

#ifdef DEBUG_TLB
    if (hit)
        std::cout << " Hit at level: " << m_name << std::endl;
    else
        std::cout << " Miss at level: " << m_name << std::endl;
#endif

    // If we got a TLB hit
    if (hit)
    {
        tlb_stats.m_hit++;
        return hit;
    }

    // If not found, count as a miss if needed
    if (model_count)
        tlb_stats.m_miss++;

    return NULL;
}
```

**Purpose**:
- Checks if a given virtual address is present in the TLB.
- If `model_count` is true, updates the total access count and later the miss count.
- Returns a pointer to the `CacheBlockInfo` if found, or `NULL` otherwise.

**Debugging**:
- `#ifdef DEBUG_TLB` blocks can be enabled to print detailed debug information to standard output.

### `allocate()`

```cpp
std::tuple<bool, IntPtr, int> TLB::allocate(IntPtr address,
                                           SubsecondTime now,
                                           bool count,
                                           Core::lock_signal_t lock_signal,
                                           int page_size,
                                           IntPtr ppn,
                                           bool self_alloc)
{
    if (getPrefetch() && !self_alloc)
    {
        // If prefetching is enabled, skip direct allocation unless it's a self_alloc
        return std::make_tuple(false, 0, 0);
    }
    IntPtr evict_addr;
    CacheBlockInfo evict_block_info;

    IntPtr tag;
    UInt32 set_index;

    // Extract set index and tag based on page size
    m_cache.splitAddressTLB(address, tag, set_index, page_size);

#ifdef DEBUG_TLB
    std::cout << " Allocate " << address << " at level: " << m_name
              << " with page_size " << page_size
              << " and tag " << tag << std::endl;
#endif

    bool eviction = false;

    // Insert a new translation line into the TLB
    m_cache.insertSingleLineTLB(address, NULL, &eviction, &evict_addr,
                                &evict_block_info, NULL, now, NULL,
                                CacheBlockInfo::block_type_t::NON_PAGE_TABLE,
                                page_size, ppn);

    // Count evictions if needed
    if (eviction && count)
        tlb_stats.m_eviction++;

#ifdef DEBUG_TLB
    if (eviction)
        std::cout << " Evicted " << evict_addr << " from level: " << m_name
                  << " with page_size " << page_size << std::endl;
#endif

    // Return tuple: (was there an eviction?, evicted address, evicted page size)
    return std::make_tuple(eviction, evict_addr, evict_block_info.getPageSize());
}
```

**Purpose**:
- Inserts a new translation line into the TLB if it is not found (on a miss).
- Evicts an old entry if no free lines exist in the set.
- Returns info about whether something was evicted, which address was evicted, and what the evicted page size was.

**Key Logic**:
- `m_cache.splitAddressTLB(address, tag, set_index, page_size);`
  - Splits the incoming address into a tag and set index, factoring in the page_size.
- `m_cache.insertSingleLineTLB(...)`
  - Actually performs the insertion, populating it with a physical page number (ppn).

### Statistics Tracking

This TLB tracks statistics such as:

- **m_access**: Number of accesses made to the TLB.
- **m_hit**: Number of hits in the TLB.
- **m_miss**: Number of misses in the TLB.
- **m_eviction**: Number of evictions that occurred during TLB insertions.

At the end of execution or during simulation, these metrics can be retrieved and logged for performance analysis.

### Prefetching Support

If enabled (`m_prefetch == true`), the TLB can work with one or more TLB prefetchers:

- A prefetcher might predict which translations will be needed soon and preemptively load them into the TLB.
- The `allocate()` function checks whether it’s a self-allocation vs. a prefetch request to decide whether to proceed with a new entry insertion.
