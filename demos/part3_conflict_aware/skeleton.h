/**
 * @file skeleton.h
 * @brief Conflict-Aware Page Table Allocator -- WORKSHOP SKELETON
 *
 * ASPLOS 2026 Workshop: Hardware/OS Co-Design for Memory Management
 *
 * ============================================================================
 * INSTRUCTIONS
 * ============================================================================
 *
 * This file contains a skeleton of the conflict-aware allocator.  Your task
 * is to fill in the three methods marked with TODO.  The design is:
 *
 *   1. Track which DRAM banks have recent data-page traffic ("hot banks").
 *   2. When allocating a page for the page table, check whether the
 *      candidate page would land in a hot bank.
 *   3. If so, try a different page (up to MAX_RETRIES times).
 *   4. Fall back to the original candidate if no conflict-free page exists.
 *
 * DRAM Bank Mapping (simplified):
 *   - 16 banks, 64-page rows (256 KiB per row)
 *   - bank = (ppn >> 6) & 0xF
 *
 * When you are done, compare your implementation with the reference in
 * conflict_aware_allocator.h.
 *
 * ============================================================================
 */

#pragma once

#include "debug_config.h"
#include "memory_management/physical_memory_allocators/physical_memory_allocator.h"
#include "memory_management/physical_memory_allocators/buddy_policy_traits.h"
#include "memory_management/physical_memory_allocators/buddy.h"
#include "templates_traits_config.h"

#include <array>
#include <bitset>
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <tuple>
#include <vector>

// ============================================================================
// Configuration constants -- do NOT change these
// ============================================================================
static constexpr int CA_NUM_BANKS   = 16;    // Number of DRAM banks
static constexpr int CA_BANK_SHIFT  = 6;     // log2(pages per row) = 64 pages
static constexpr int CA_BANK_MASK   = 0xF;   // NUM_BANKS - 1
static constexpr int CA_HOT_THRESH  = 8;     // Accesses to mark a bank "hot"
static constexpr int CA_MAX_RETRIES = 16;    // Max buddy retries per PT alloc
static constexpr int CA_DECAY_INTERVAL = 1024; // Decay counters every N allocs

// ============================================================================
// ConflictAwareAllocator -- SKELETON
// ============================================================================

template <typename Policy>
class ConflictAwareAllocator : public PhysicalMemoryAllocator, private Policy
{
    using BuddyPolicy = typename BuddyPolicyFor<Policy>::type;
    using BuddyType   = Buddy<BuddyPolicy>;

private:
    std::map<UInt64, std::tuple<UInt64, std::bitset<512>, bool>> two_mb_map;
    BuddyType *buddy_allocator;
    double threshold_for_promotion;

    // --- Conflict-awareness state ---
    std::array<int, CA_NUM_BANKS> bank_counters;  // Per-bank data-access counter
    UInt64 total_data_allocs;                      // For periodic decay

    struct Stats {
        UInt64 pt_allocs_total     = 0;
        UInt64 pt_allocs_conflict  = 0;
        UInt64 pt_allocs_avoided   = 0;
        UInt64 pt_allocs_fallback  = 0;
        UInt64 four_kb_allocated   = 0;
        UInt64 two_mb_reserved     = 0;
        UInt64 two_mb_promoted     = 0;
        UInt64 two_mb_demoted      = 0;
        UInt64 total_allocations   = 0;
        UInt64 kernel_pages_used   = 0;
    } stats;

public:
    Stats& getStats() { return stats; }

    ConflictAwareAllocator(String name,
                           int memory_size,
                           int max_order,
                           int kernel_size,
                           String frag_type,
                           double threshold_for_promotion)
        : PhysicalMemoryAllocator(name, memory_size, kernel_size),
          threshold_for_promotion(threshold_for_promotion),
          total_data_allocs(0)
    {
        bank_counters.fill(0);
        Policy::on_init(name, memory_size, kernel_size, threshold_for_promotion, this);

        static_assert(is_complete<BuddyPolicyFor<Policy>>::value,
            "BuddyPolicyFor<Policy> is incomplete.");

        buddy_allocator = new BuddyType(memory_size, max_order, kernel_size, frag_type);

        std::cout << "[MimicOS] Conflict-Aware Allocator (SKELETON) initialized" << std::endl;
    }

    ~ConflictAwareAllocator()
    {
        std::cout << "[ConflictAware] PT allocs: " << stats.pt_allocs_total
                  << "  conflicts_detected: " << stats.pt_allocs_conflict
                  << "  avoided: " << stats.pt_allocs_avoided
                  << "  fallback: " << stats.pt_allocs_fallback << std::endl;
        delete buddy_allocator;
    }

    // ====================================================================
    // TODO 1: Compute the DRAM bank index for a physical page number.
    //
    // The mapping is:
    //   bank = (ppn >> CA_BANK_SHIFT) & CA_BANK_MASK
    //
    // This gives 16 banks with 64-page rows (256 KiB per row).
    //
    // Return the bank index as an int.
    // ====================================================================
    static int computeBank(UInt64 ppn)
    {
        // TODO: Implement the bank mapping.
        //
        // Hint: Use bit shifting and masking with the constants
        //       CA_BANK_SHIFT and CA_BANK_MASK defined above.
        //
        // Replace the line below with your implementation:
        (void)ppn;
        return 0;  // FIXME
    }

    /**
     * @brief Check whether a DRAM bank is currently "hot".
     */
    bool isBankHot(int bank) const
    {
        return bank_counters[bank] >= CA_HOT_THRESH;
    }

    // ====================================================================
    // TODO 2: Record a data-page allocation to update the hot-bank tracker.
    //
    // When a data page (not a page-table page) is allocated, we need to
    // track which DRAM bank it lands in so we can later avoid placing
    // page-table pages there.
    //
    // Steps:
    //   a) Compute the bank for the given ppn using computeBank().
    //   b) Increment bank_counters[bank].
    //   c) Increment total_data_allocs.
    //   d) Every CA_DECAY_INTERVAL allocations, decay all counters by
    //      dividing them by 2 (right-shift by 1).  This ensures the
    //      tracker forgets old history.
    // ====================================================================
    void recordDataAccess(UInt64 ppn)
    {
        // TODO: Implement the hot-bank tracking logic described above.
        //
        // Hint: The decay step should check:
        //   if (total_data_allocs % CA_DECAY_INTERVAL == 0) { ... }
        //
        (void)ppn;
        // FIXME: Your code here.
    }

    // ====================================================================
    // TODO 3: Override page-table allocation with conflict avoidance.
    //
    // This is the heart of the allocator.  When the page table needs a
    // new page, we want to pick one that does NOT land in a hot DRAM bank.
    //
    // Algorithm:
    //   1. Request a candidate page from buddy_allocator->allocate_page_table(bytes).
    //   2. If allocation failed (returned -1), return -1.
    //   3. Compute the bank for the candidate.
    //   4. If the bank is NOT hot, accept the candidate and return it.
    //   5. If the bank IS hot:
    //      a) Record the conflict (stats.pt_allocs_conflict++).
    //      b) Save the first candidate as fallback.
    //      c) Loop up to CA_MAX_RETRIES times:
    //         - Request another candidate from the buddy.
    //         - If its bank is not hot, accept it (stats.pt_allocs_avoided++).
    //         - Otherwise, track the candidate with the lowest bank counter
    //           as the "best fallback".
    //      d) If no conflict-free page was found, use the best fallback
    //         (stats.pt_allocs_fallback++).
    //   6. Return the chosen PPN.
    //
    // Don't forget to increment stats.pt_allocs_total and
    // stats.kernel_pages_used at the start.
    // ====================================================================
    UInt64 handle_page_table_allocations(UInt64 bytes, UInt64 core_id = (UInt64)-1) override
    {
        stats.pt_allocs_total++;
        stats.kernel_pages_used++;

        // TODO: Implement the conflict-aware allocation logic.
        //
        // For now, just delegate to the buddy without any conflict checks:
        UInt64 ppn = buddy_allocator->allocate_page_table(bytes);
        return ppn;
        // FIXME: Replace the two lines above with your implementation.
    }

    // ====================================================================
    // Standard allocate (data pages) -- provided for you
    // ====================================================================
    std::pair<UInt64, UInt64> allocate(UInt64 size,
                                       IntPtr address,
                                       UInt64 app_id,
                                       bool is_pagetable_allocation,
                                       bool is_instruction_allocation = false) override
    {
        stats.total_allocations++;

        if (is_pagetable_allocation) {
            UInt64 ppn = handle_page_table_allocations(size);
            assert(ppn != static_cast<UInt64>(-1));
            return std::make_pair(ppn, 12);
        }

        // Data allocation: use buddy and record the bank access
        UInt64 ppn = buddy_allocator->allocate_page(size);
        if (ppn != static_cast<UInt64>(-1)) {
            recordDataAccess(ppn);
            stats.four_kb_allocated++;
        }
        return std::make_pair(ppn, 12);
    }
};
