---
sidebar_position: 2
---

# Reservation-based Transparent Huge Pages

# Reservation-based Transparent Huge Page (THP) Allocator

This document provides an overview and explanation of the **ReservationTHPAllocator** class, a specialized physical memory allocator that reserves and promotes **Transparent Huge Pages (THPs)**. The code is designed to integrate with a **buddy allocator** (provided through the `Buddy` class) to manage physical memory. The ultimate goal is to optimize memory allocations by utilizing 2MB pages wherever possible while allowing fallback to 4KB pages when needed.

---

## Table of Contents

1. [Introduction](#introduction)  
2. [High-Level Design](#high-level-design)  
3. [Class Overview](#class-overview)  
    - [Constructor](#constructor)  
    - [Destructor](#destructor)  
4. [Key Methods](#key-methods)  
    - [demote_page()](#demote_page)  
    - [checkFor2MBAllocation()](#checkfor2mballocation)  
    - [allocate()](#allocate)  
    - [givePageFast()](#givepagefast)  
    - [deallocate()](#deallocate)  
    - [allocate_ranges()](#allocate_ranges)  
    - [getFreePages()](#getfreepages)  
    - [getTotalPages()](#gettotalpages)  
    - [getLargePageRatio()](#getlargepageratio)  
    - [getAverageSizeRatio()](#getaveragesizeratio)  
    - [fragment_memory()](#fragment_memory)  
5. [Logging and Debugging](#logging-and-debugging)  
6. [Dependencies](#dependencies)  
7. [How to Use](#how-to-use)  
8. [Example Usage](#example-usage)  
9. [License](#license)

---

## Introduction

Transparent Huge Pages (THPs) allow the system to manage memory in larger chunks (commonly **2MB** pages) rather than standard **4KB** pages. The `ReservationTHPAllocator` uses a reservation-based approach to allocate and promote 2MB pages when specific criteria (e.g., utilization thresholds) are met. If reservations cannot be fulfilled, the allocator falls back to allocating standard 4KB pages from the underlying **buddy allocator**.

**Key benefits** of using THPs:
1. **Reduced TLB Pressure**: Fewer TLB entries are required since each entry covers a larger memory range.
2. **Improved Performance**: Larger pages can reduce page-table lookups and cache misses in certain workloads.
3. **Memory Efficiency**: Depending on usage, THPs can reduce fragmentation. However, if pages are underutilized, demotion or fallback to 4KB pages prevents excessive waste.

---

## High-Level Design

1. **Check for 2MB Page**: When a request arrives for a specific **virtual address** (and that address is not for a page table), the allocator checks whether a 2MB region is already reserved in `two_mb_map`.
2. **Reserve or Allocate 2MB Page**:  
    - If the region does not exist, the allocator attempts to reserve a 2MB chunk from the buddy allocator.  
    - If successful, it records metadata in `two_mb_map` (e.g., the physical start address, a bitset to track used 4KB pages, and promotion status).
3. **Mark 4KB as Allocated**: Within the 2MB reservation, a **4KB offset** is marked as allocated in the bitset.
4. **Promote if Threshold is Met**: If the ratio of used pages in the 2MB region exceeds `threshold_for_promotion`, the entire 2MB region is promoted (flag set to `true`).
5. **Fallback**: If a 2MB reservation cannot be satisfied, the allocator falls back to standard 4KB allocations from the buddy allocator.
6. **Demotion**: When memory pressure is high or if an allocation fails, the allocator may **demote** the least-utilized 2MB region back into 4KB frames, freeing those pages in the buddy system.

---

## Class Overview

### Constructor

```cpp
ReservationTHPAllocator::ReservationTHPAllocator(
     String name,
     int memory_size,
     int max_order,
     int kernel_size,
     String frag_type,
     float _threshold_for_promotion
) : PhysicalMemoryAllocator(name, memory_size, kernel_size),
     threshold_for_promotion(_threshold_for_promotion)
{
     // ...
}
```

**Parameters:**

- `name`: A string identifier for the allocator instance.
- `memory_size`: Total size of the memory in pages (depends on how your system or simulation tracks memory).
- `max_order`: The maximum order of pages that the underlying buddy allocator can handle (often related to the largest block size).
- `kernel_size`: The size of the kernel or reserved portion of memory.
- `frag_type`: Indicates a fragmentation type or strategy for the buddy allocator.
- `_threshold_for_promotion`: A floating-point number specifying the fraction of the 2MB region that must be utilized before promotion occurs.

**Initialization:**

- Opens `reservation_thp.log` for logging.
- Instantiates internal statistics counters (e.g., `stats.four_kb_allocated`, `stats.two_mb_promoted`, etc.).
- Initializes an internal instance of the buddy allocator: `buddy_allocator`.

### Destructor

```cpp
ReservationTHPAllocator::~ReservationTHPAllocator()
{
     delete buddy_allocator;
}
```

- Deallocates the `buddy_allocator` instance.
- Ensures a graceful cleanup of dynamically allocated resources.

---

## Key Methods

### demote_page()

```cpp
bool ReservationTHPAllocator::demote_page();
```

**Purpose:** Frees a 2MB region (selected by lowest utilization) and returns those pages back to the buddy allocator as 4KB frames.

**Process:**
- Collect all non-promoted 2MB regions in `two_mb_map`.
- Calculate the utilization of each region.
- Sort by utilization and pick the least-utilized region for demotion.
- For each unused 4KB page in that 2MB region, free it in the buddy allocator.
- Erase the region from `two_mb_map`.
- Increment `stats.two_mb_demoted`.
- Return true upon successful demotion; false if no suitable candidate exists.

### checkFor2MBAllocation()

```cpp
std::pair<UInt64,bool> ReservationTHPAllocator::checkFor2MBAllocation(UInt64 address, UInt64 core_id);
```

**Purpose:** Checks if a 2MB page can be (or has already been) reserved for the given virtual address.

**Returns:** A pair `(physicalAddress, isPromoted)`.

- `physicalAddress`: Either the corresponding 4KB physical address in the 2MB region or -1 if reservation fails.
- `isPromoted`: A boolean indicating if the entire 2MB region was promoted as a result of allocation.

**Process:**
- Computes the 2MB region index from address by shifting right by 21 bits.
- If the region does not exist in `two_mb_map`, attempts to reserve a 2MB page via `buddy_allocator->reserve_2mb_page(...)`.
- If successful, adds an entry in `two_mb_map` with a 512-bit bitset (each bit representing one 4KB page).
- Marks the specific 4KB page as allocated in the bitset.
- Checks if `(bitset.count() / 512.0) > threshold_for_promotion`.
- If above threshold, sets promotion flag to true and increments `stats.two_mb_promoted`.
- Otherwise, continues using 4KB pages within that 2MB region.

### allocate()

```cpp
std::pair<UInt64, UInt64> ReservationTHPAllocator::allocate(
     UInt64 size,
     UInt64 address,
     UInt64 core_id,
     bool is_pagetable_allocation
);
```

**Purpose:** Allocates size bytes of memory at the given address on behalf of `core_id`. Supports promotion/fallback/demotion logic.

**Returns:** A pair `(physicalAddress, pageSizeFlag)`:

- `physicalAddress`: The actual physical address allocated.
- `pageSizeFlag`:
  - 21 if the allocation was promoted to a 2MB page.
  - 12 if it is a regular 4KB page.
  (These flag values are illustrative in this code; you could use more robust enumerations in production.)

**Process:**

- If `is_pagetable_allocation == true`, directly requests a 4KB page from the buddy allocator.
- Otherwise, calls `checkFor2MBAllocation()`:
  - If successful, decide if it was promoted (`pageSizeFlag = 21`) or not (`pageSizeFlag = 12`).
  - If unsuccessful, fall back to `buddy_allocator->allocate(...)`.
  - If the buddy allocator fails for a 4KB fallback, calls `demote_page()` to free up space and retries.
- Returns the final allocation outcome.

### givePageFast()

```cpp
UInt64 ReservationTHPAllocator::givePageFast(UInt64 bytes, UInt64 address, UInt64 core_id);
```

**Purpose:** Provides a fast path allocation for straightforward 4KB pages via the buddy allocator, bypassing any THP logic.

**Use Cases:** May be handy for special allocations where THP logic is not needed or during urgent allocations requiring minimal overhead.

### deallocate()

```cpp
void ReservationTHPAllocator::deallocate(UInt64 region_begin, UInt64 core_id);
```

**Purpose:** Placeholder for deallocation logic.

**Status:** Not yet implemented.

**Intended Behavior:**
- Would free a region or part of a region back to the allocator (buddy or THP map).
- In a fully implemented system, this should handle partial or complete deallocation of 2MB regions or 4KB pages.

### allocate_ranges()

```cpp
std::vector<Range> ReservationTHPAllocator::allocate_ranges(UInt64 size, UInt64 core_id);
```

**Purpose:** Future extension for allocating multiple contiguous or non-contiguous ranges at once.

**Status:** Not implemented in this code snippet.

**Returns:** An empty `std::vector<Range>` as a placeholder.

### getFreePages()

```cpp
UInt64 ReservationTHPAllocator::getFreePages() const;
```

**Purpose:** Returns the number of 4KB pages available in the buddy allocator.

### getTotalPages()

```cpp
UInt64 ReservationTHPAllocator::getTotalPages() const;
```

**Purpose:** Returns the total number of pages (again, in 4KB units) managed by the buddy allocator.

### getLargePageRatio()

```cpp
double ReservationTHPAllocator::getLargePageRatio();
```

**Purpose:** Retrieves the ratio of large pages (e.g., 2MB blocks) to total pages from the buddy allocator.

### getAverageSizeRatio()

```cpp
double ReservationTHPAllocator::getAverageSizeRatio();
```

**Purpose:** Reports the average size ratio of allocated blocks within the buddy allocator.

**Details:** Implementation depends on the Buddy class’s internal logic.

### fragment_memory()

```cpp
void ReservationTHPAllocator::fragment_memory(double fragmentation);
```

**Purpose:** Artificially induce fragmentation in the buddy allocator for testing or simulation.

---

## Logging and Debugging

- The `#define DEBUG_RESERVATION_THP` macro toggles detailed debug statements.
- Logs are written to `reservation_thp.log`.

**Debug output includes:**
- Allocation requests and outcomes.
- Region promotions and demotions.
- Bitset state for reserved 2MB regions.

---

## Dependencies

**Buddy Allocator (`buddy_allocator`)**

The class relies on Buddy, which must implement:

- `reserve_2mb_page(...)`
- `allocate(...)`
- `free(...)`
- `fragmentMemory(...)`
- `getFreePages()`
- `getTotalPages()`
- `getLargePageRatio()`
- `getAverageSizeRatio()`

**Standard C++ Libraries:**

- `<vector>`, `<list>`, `<utility>`, `<bitset>`, `<iostream>`, `<fstream>`, `<algorithm>`, etc.

**Other Includes:**

- `physical_memory_allocator.h`
- `reserve_thp.h`

---

## How to Use

**Include Headers:**

```cpp
#include "physical_memory_allocator.h"
#include "reserve_thp.h"
#include "buddy_allocator.h"
// ... other relevant headers
```

**Instantiate the Allocator:**

```cpp
ReservationTHPAllocator thpAllocator(
     "THP_Allocator",
     memory_size_in_4KB_pages,
     max_order,
     kernel_size_in_4KB_pages,
     "some_fragmentation_type",
     0.7f // threshold_for_promotion
);
```

**Perform Allocations:**

```cpp
// Example: Allocate 4KB at virtual address 0x100000 on core 0
auto result = thpAllocator.allocate(4096, 0x100000, 0, false);

// Check outcome
if (result.first == -1) {
     std::cerr << "Allocation failed!\n";
} else {
     std::cout << "Allocated physical address: " 
                  << result.first 
                  << ", pageSizeFlag: " 
                  << result.second 
                  << std::endl;
}
```

**Check for Promotion:**

If enough 4KB pages in a 2MB region become allocated, the region will be promoted automatically once the threshold is exceeded.

**Demote Pages (Manually Triggered):**

```cpp
bool success = thpAllocator.demote_page();
if (success) {
     std::cout << "Successfully demoted one 2MB region." << std::endl;
} else {
     std::cout << "No 2MB region available for demotion." << std::endl;
}
```

---

## Example Usage

Below is a simplified snippet that demonstrates how one might use the ReservationTHPAllocator in a hypothetical system:

```cpp
#include <iostream>
#include "reserve_thp.h"
#include "physical_memory_allocator.h"

int main() {
     // Basic parameters
     int memory_size = 16384;   // 64 MB if each page is 4KB
     int max_order   = 11;      // For buddy system
     int kernel_size = 1024;    // 4 MB reserved for kernel
     float promotion_threshold = 0.75f; // 75% usage threshold

     // Instantiate the THP Allocator
     ReservationTHPAllocator thpAllocator(
          "THP_Allocator",
          memory_size,
          max_order,
          kernel_size,
          "default_fragmentation",
          promotion_threshold
     );

     // Request an allocation
     UInt64 virtualAddress = 0x100000; // Example virtual address
     auto [physicalAddress, pageFlag] = thpAllocator.allocate(4096, virtualAddress, 0, false);

     if (physicalAddress == static_cast<UInt64>(-1)) {
          std::cerr << "Allocation failed!" << std::endl;
     } else {
          std::cout << "Allocated 4KB at physical address 0x"
                        << std::hex << physicalAddress
                        << " with pageFlag="
                        << pageFlag
                        << std::endl;
     }

     // ... additional logic ...

     return 0;
}
```

---
