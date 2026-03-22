#pragma once
/*------------------------------------------------------------------------------
 *  EagerPagingAllocator - Policy-based template design
 *  Allocates contiguous physical ranges eagerly using a buddy allocator.
 *  Used by RMM (Redundant Memory Mappings, Karakostas et al. ISCA 2015).
 *----------------------------------------------------------------------------*/
#include "debug_config.h"
#include "memory_management/physical_memory_allocators/physical_memory_allocator.h"
#include "memory_management/misc/vma.h"
#include <map>
#include <vector>
#include <tuple>
#include <utility>
#include <cmath>
#include <cassert>
#include <algorithm>

#include "templates_traits_config.h"
#include "memory_management/physical_memory_allocators/buddy_policy_traits.h"
#include "memory_management/physical_memory_allocators/buddy.h"

template <typename Policy>
class EagerPagingAllocator : public PhysicalMemoryAllocator, private Policy
{
    using BuddyPolicy = typename BuddyPolicyFor<Policy>::type;
    using BuddyType   = Buddy<BuddyPolicy>;

    struct Stats {
        UInt64 physical_ranges_per_vma = 0;
        UInt64 deviation_of_physical_ranges = 0;
        UInt64 total_allocated_vmas = 0;
        UInt64 total_allocated_physical_ranges = 0;
    } stats;

public:
    Stats& getStats() { return stats; }

    EagerPagingAllocator(String name,
                         int    memory_size,
                         int    max_order,
                         int    kernel_size,
                         String frag_type)
        : PhysicalMemoryAllocator(name, memory_size, kernel_size)
    {
        Policy::on_init(name, memory_size, kernel_size, this);

        static_assert(is_complete<BuddyPolicyFor<Policy>>::value,
            "BuddyPolicyFor<Policy> is incomplete - include buddy_policy.h first");

        buddy_allocator = new BuddyType(memory_size, max_order, kernel_size, frag_type);
    }

    ~EagerPagingAllocator() { delete buddy_allocator; }

    /*--------------------------- public interface ------------------------------*/

    std::pair<UInt64, UInt64> allocate(UInt64 size, UInt64 address = 0, UInt64 core_id = -1, bool is_pagetable_allocation = false, bool is_instruction_allocation = false) override
    {
        // Eager paging: allocate the requested size as a contiguous block
        // The buddy allocator returns contiguous pages for power-of-2 sizes
        UInt64 ppn = buddy_allocator->allocate(size, address, core_id);
#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
        Policy::log("[EagerPaging] allocate size=" + std::to_string(size) +
                     " addr=" + std::to_string(address) + " -> ppn=" + std::to_string(ppn));
#endif
        int page_size = 12; // default 4KB
        if (size >= (1 << 21)) page_size = 21; // 2MB
        return std::make_pair(ppn, static_cast<UInt64>(page_size));
    }

    UInt64 givePageFast(UInt64 bytes, UInt64 address = 0, UInt64 core_id = -1) override
    {
#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
        Policy::log("[EagerPaging] givePageFast bytes=" + std::to_string(bytes));
#endif
        return buddy_allocator->allocate(bytes, address, core_id);
    }

    void deallocate(UInt64 address, UInt64 core_id) override
    {
        /* Requires implementation */
    }

    std::vector<Range> allocate_ranges(IntPtr start_va, IntPtr end_va, int app_id) override
    {
        int remaining_pages = (end_va - start_va) / 4096;
        std::vector<Range> ranges;

#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
        Policy::log("[EagerPaging] allocate_ranges VPN " +
                     std::to_string(start_va/4096) + " - " + std::to_string(end_va/4096) +
                     " (" + std::to_string(remaining_pages) + " pages)");
#endif

        IntPtr current_vpn = start_va / 4096;

        // Eager paging (Karakostas et al. ISCA 2015): greedily allocate the
        // largest contiguous power-of-2 block that fits the remaining VMA.
        // The buddy allocator returns physically contiguous pages.
        int max_order = buddy_allocator->getMaxOrder();

        while (remaining_pages > 0)
        {
            // Find largest power-of-2 that fits, capped at buddy's max order
            int order = std::min(static_cast<int>(std::log2(remaining_pages)), max_order);
            int block_pages = 1 << order;
            UInt64 block_bytes = static_cast<UInt64>(block_pages) * 4096;

            // Try to allocate this block; if buddy is fragmented, try smaller
            UInt64 ppn = static_cast<UInt64>(-1);
            while (order >= 0 && ppn == static_cast<UInt64>(-1))
            {
                block_pages = 1 << order;
                block_bytes = static_cast<UInt64>(block_pages) * 4096;
                ppn = buddy_allocator->allocate(block_bytes, current_vpn * 4096, app_id);
                if (ppn == static_cast<UInt64>(-1))
                    order--;
            }

            if (ppn == static_cast<UInt64>(-1))
            {
                // Out of memory — cannot allocate even a single page
                std::cerr << "[EagerPaging] Out of memory at VPN " << current_vpn << std::endl;
                break;
            }

            Range range;
            range.vpn = current_vpn;
            range.bounds = block_pages;
            range.offset = ppn;
            ranges.push_back(range);

#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
            Policy::log("[EagerPaging] allocated " + std::to_string(block_pages) +
                         " contiguous pages at ppn=" + std::to_string(ppn) +
                         " for VPN " + std::to_string(current_vpn));
#endif

            current_vpn += block_pages;
            remaining_pages -= block_pages;
            stats.total_allocated_physical_ranges++;
        }

        stats.total_allocated_vmas++;
        return ranges;
    }

    UInt64 getFreePages() const  { return buddy_allocator->getFreePages(); }
    UInt64 getTotalPages() const { return buddy_allocator->getTotalPages(); }

    double getAverageSizeRatio() { return buddy_allocator->getAverageSizeRatio(); }
    double getLargePageRatio()   { return buddy_allocator->getLargePageRatio(); }

    void fragment_memory(double target_fragmentation) override
    {
        buddy_allocator->fragmentMemory(target_fragmentation);
    }

protected:
    BuddyType *buddy_allocator;
};
