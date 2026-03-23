/**
 * @file conflict_aware_allocator.h
 * @brief Conflict-Aware Page Table Allocator for DRAM Bank Conflict Reduction
 *
 * ASPLOS 2026 Workshop: Hardware/OS Co-Design for Memory Management
 *
 * ============================================================================
 * DESIGN OVERVIEW
 * ============================================================================
 *
 * Problem:
 *   When page-table pages are allocated without regard to DRAM bank placement,
 *   they may share banks with frequently-accessed data pages.  During a page
 *   table walk (PTW), this causes row-buffer conflicts in DRAM, adding ~10-20
 *   ns per conflict.  For a 4-level radix walk with 4 DRAM accesses, this can
 *   add 40-80 ns of unnecessary latency.
 *
 * Solution:
 *   This allocator extends ReservationTHPAllocator and overrides
 *   handle_page_table_allocations().  Before accepting a candidate page from
 *   the buddy allocator, it computes the DRAM bank index and checks whether
 *   that bank is "hot" (has recent data traffic).  If so, it retries with the
 *   next free page, up to MAX_RETRIES attempts.
 *
 * DRAM Bank Mapping (simplified):
 *   bank = (ppn >> BANK_SHIFT) & BANK_MASK
 *
 *   With 16 banks and 64-page rows:
 *     BANK_SHIFT = 6  (skip 64 pages = 256 KiB per row)
 *     BANK_MASK  = 0xF (16 banks)
 *
 * Hot-Bank Tracking:
 *   A simple counter array (one per bank) is incremented on each data-page
 *   allocation and decayed periodically.  A bank is "hot" if its counter
 *   exceeds a configurable threshold.
 *
 * ============================================================================
 *
 * @author ASPLOS 2026 Workshop / Virtuoso
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
// Configuration constants
// ============================================================================

static constexpr int CA_NUM_BANKS   = 16;    // Number of DRAM banks
static constexpr int CA_BANK_SHIFT  = 6;     // log2(pages per row) = 64 pages
static constexpr int CA_BANK_MASK   = 0xF;   // NUM_BANKS - 1
static constexpr int CA_HOT_THRESH  = 8;     // Accesses to mark a bank "hot"
static constexpr int CA_MAX_RETRIES = 16;    // Max buddy retries per PT alloc
static constexpr int CA_DECAY_INTERVAL = 1024; // Decay counters every N allocs

// ============================================================================
// ConflictAwareAllocator
// ============================================================================

template <typename Policy>
class ConflictAwareAllocator : public PhysicalMemoryAllocator, private Policy
{
    using BuddyPolicy = typename BuddyPolicyFor<Policy>::type;
    using BuddyType   = Buddy<BuddyPolicy>;

private:
    // --- ReserveTHP-style 2MB reservation map ---
    //   region_2MB -> (start_ppn, usage_bitset, promoted_flag)
    std::map<UInt64, std::tuple<UInt64, std::bitset<512>, bool>> two_mb_map;
    BuddyType *buddy_allocator;
    double threshold_for_promotion;

    // --- Conflict-awareness state ---
    std::array<int, CA_NUM_BANKS> bank_counters;  // Per-bank access counter
    UInt64 total_data_allocs;                      // For periodic decay

    // --- Statistics ---
    struct Stats {
        UInt64 pt_allocs_total     = 0;  // Total PT page allocations
        UInt64 pt_allocs_conflict  = 0;  // Allocations that hit a hot bank initially
        UInt64 pt_allocs_avoided   = 0;  // Conflicts successfully avoided by retry
        UInt64 pt_allocs_fallback  = 0;  // Gave up after MAX_RETRIES
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

        std::cout << "[MimicOS] Conflict-Aware Allocator initialized" << std::endl;
        std::cout << "[MimicOS]   Banks=" << CA_NUM_BANKS
                  << " HotThreshold=" << CA_HOT_THRESH
                  << " MaxRetries=" << CA_MAX_RETRIES << std::endl;
    }

    ~ConflictAwareAllocator()
    {
        // Print conflict-avoidance stats
        std::cout << "[ConflictAware] PT allocs: " << stats.pt_allocs_total
                  << "  conflicts_detected: " << stats.pt_allocs_conflict
                  << "  avoided: " << stats.pt_allocs_avoided
                  << "  fallback: " << stats.pt_allocs_fallback << std::endl;
        delete buddy_allocator;
    }

    // ====================================================================
    // DRAM bank computation
    // ====================================================================

    /**
     * @brief Compute the DRAM bank index for a given physical page number.
     *
     * Uses a simplified linear bank mapping:
     *   bank = (ppn >> BANK_SHIFT) & BANK_MASK
     *
     * This models a system with 16 banks and 64-page (256 KiB) rows.
     */
    static int computeBank(UInt64 ppn)
    {
        return static_cast<int>((ppn >> CA_BANK_SHIFT) & CA_BANK_MASK);
    }

    /**
     * @brief Check whether a DRAM bank is currently "hot".
     *
     * A bank is hot if its access counter exceeds the threshold,
     * indicating significant recent data traffic.
     */
    bool isBankHot(int bank) const
    {
        return bank_counters[bank] >= CA_HOT_THRESH;
    }

    // ====================================================================
    // Hot-bank tracking
    // ====================================================================

    /**
     * @brief Record a data-page allocation, updating the bank counter.
     *
     * Called on every non-pagetable allocation so the allocator knows
     * which banks have recent data pressure.
     */
    void recordDataAccess(UInt64 ppn)
    {
        int bank = computeBank(ppn);
        bank_counters[bank]++;
        total_data_allocs++;

        // Periodic decay: halve all counters to forget stale history
        if (total_data_allocs % CA_DECAY_INTERVAL == 0) {
            for (auto &c : bank_counters) {
                c >>= 1;  // divide by 2
            }
        }
    }

    // ====================================================================
    // Page-table allocation with conflict avoidance
    // ====================================================================

    /**
     * @brief Allocate a page for the page table, avoiding hot DRAM banks.
     *
     * This is the core of the conflict-aware policy.  It:
     *   1. Requests a candidate page from the buddy allocator.
     *   2. Computes its DRAM bank.
     *   3. If the bank is hot, retries (up to MAX_RETRIES).
     *   4. Falls back to the first candidate if no conflict-free page found.
     *
     * @param bytes  Number of bytes to allocate (typically 4096).
     * @param core_id  Requesting core (unused in single-core demo).
     * @return Physical page number of the allocated page.
     */
    UInt64 handle_page_table_allocations(UInt64 bytes, UInt64 core_id = (UInt64)-1) override
    {
        stats.pt_allocs_total++;
        stats.kernel_pages_used++;

        // First candidate from buddy
        UInt64 first_ppn = buddy_allocator->allocate_page_table(bytes);
        if (first_ppn == static_cast<UInt64>(-1)) {
            return first_ppn;  // Out of memory
        }

        int bank = computeBank(first_ppn);

        // Fast path: bank is not hot -- accept immediately
        if (!isBankHot(bank)) {
            return first_ppn;
        }

        // Conflict detected -- try alternatives
        stats.pt_allocs_conflict++;

        UInt64 best_ppn = first_ppn;
        int best_bank_count = bank_counters[bank];

        for (int retry = 0; retry < CA_MAX_RETRIES; retry++) {
            UInt64 candidate = buddy_allocator->allocate_page_table(bytes);
            if (candidate == static_cast<UInt64>(-1)) {
                break;  // No more memory
            }

            int cand_bank = computeBank(candidate);

            // Accept if the bank is not hot
            if (!isBankHot(cand_bank)) {
                stats.pt_allocs_avoided++;
                return candidate;
            }

            // Track the least-hot candidate as fallback
            if (bank_counters[cand_bank] < best_bank_count) {
                best_ppn = candidate;
                best_bank_count = bank_counters[cand_bank];
            }
        }

        // All retries exhausted -- use the least-hot candidate
        stats.pt_allocs_fallback++;
        return best_ppn;
    }

    // ====================================================================
    // Standard allocate (data pages) -- delegates to buddy + 2MB logic
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
