#pragma once

/*
 * ================================================================================
 * UTOPIA HASH-COALESCE ALLOCATOR
 * ================================================================================
 *
 * Derived from UtopiaAllocator (utopia.h).
 *
 * ADDITIONAL FEATURE: Implicit Huge Page Coalescing via Bitwise Intersection
 * --------------------------------------------------------------------------
 * When a 4KB allocation request arrives for a 2MB-aligned virtual address,
 * the allocator checks whether all 512 underlying 4KB pages can fit into the
 * same way across their respective sets in the 4KB RestSeg.  If they can, it
 * locks that way for the whole 2MB region and returns a 2MB huge page,
 * effectively *coalescing* 512 small pages into one large mapping without
 * any extra TLB pressure.
 *
 * The check is O(512) with an early-exit optimisation (fail-fast when the
 * candidate bitmask reaches zero).
 *
 * Data structure: per-set "way bitmap" (std::vector<uint64_t>)
 *   set_way_bitmap[set_index]  –  bit N is 1 ⟹ way N is free
 *
 * ALLOCATION HIERARCHY (5-Level Priority)
 * ----------------------------------------
 *   L0  Coalescing check  (new – 2MB from RestSeg-4KB bitmap intersection)
 *   L1  RestSeg-2MB       (fast translation, 2MB pages)
 *   L2  FlexSeg-THP       (2MB via buddy reservation)
 *   L3  RestSeg-4KB       (fast translation, 4KB pages)
 *   L4  FlexSeg-4KB       (buddy fallback)
 *
 * ================================================================================
 */

#include "debug_config.h"
#include "sim_log.h"
#include "memory_management/physical_memory_allocators/physical_memory_allocator.h"
#include "templates_traits_config.h"
#include "memory_management/physical_memory_allocators/buddy_policy_traits.h"
#include "memory_management/physical_memory_allocators/buddy.h"

#include <cmath>
#include <vector>
#include <unordered_map>
#include <map>
#include <tuple>
#include <bitset>
#include <mutex>
#include <algorithm>
#include <memory>
#include <fstream>
#include <iomanip>

/* ---- forward-include the shared helpers from utopia.h ---- */
#include "memory_management/physical_memory_allocators/utopia.h"

/*
 * ================================================================================
 * RestSegCoalesce – RestSeg with per-set way bitmap for coalescing
 * ================================================================================
 *
 * Extends the base RestSeg<Policy> logic with:
 *   • set_way_bitmap  – vertical bitmask: bit N ⟹ way N is free
 *   • updateWayBitmap – toggle individual bits
 *   • findCommonFreeWay – O(512) bitwise intersection check
 *   • tryInsertAtWay (RestSeg-level) – insert a page at a specific way
 *
 * The bitmap is kept in sync:
 *   • allocate()       → clears bit when a free way is claimed
 *   • tryInsertAtWay() → clears bit when the specific way is claimed
 *   • (eviction replaces valid→valid: bit stays 0, no update needed)
 */
template <typename Policy>
class RestSegCoalesce {
private:
    int id;                      ///< RestSeg identifier (1-indexed)
    UInt64 size_bytes;           ///< Total size in bytes
    int page_size_bits;          ///< Page size in bits (12=4KB, 21=2MB, 30=1GB)
    UInt32 associativity;        ///< Number of ways per set
    UInt32 num_sets;             ///< Number of sets = size / (assoc * page_size)

    std::vector<RestSegSet> sets;  ///< The actual set-associative structure
    IntPtr base_physical_address;  ///< Base physical address of this RestSeg

    // ============ Way Bitmap for Coalescing ============
    /**
     * Vertical bitmask per set.
     *   set_way_bitmap[set_index] : bit N = 1 → way N is free
     *                               bit N = 0 → way N is occupied
     * Supports up to 64-way associativity.
     */
    std::vector<uint64_t> set_way_bitmap;

    // ============ Statistics ============
    UInt64 accesses = 0;
    UInt64 hits = 0;
    UInt64 conflicts = 0;
    UInt64 allocations = 0;

    // ============ Fingerprint Statistics ============
    mutable UInt64 fingerprint_checks = 0;
    mutable UInt64 fingerprint_matches = 0;
    mutable UInt64 fingerprint_false_positives = 0;
    mutable UInt64 fingerprint_true_positives = 0;

    // ============ Region Preferred Way Table ============
    static constexpr int RLE_REGION_BITS = 18;
    static constexpr UInt64 RLE_PAGES_PER_REGION = 1ULL << RLE_REGION_BITS;

    struct RegionWayPref {
        int way = -1;
        UInt32 failures = 0;
        UInt32 successes = 0;
        UInt64 total_allocations = 0;
        UInt64 rle_misses = 0;
    };

    std::unordered_map<UInt64, RegionWayPref> region_way_pref;
    static constexpr UInt32 REGION_PREF_FAILURE_THRESHOLD = 32;

    mutable UInt64 region_way_attempts = 0;
    mutable UInt64 region_way_hits = 0;
    mutable UInt64 region_way_relearns = 0;

    // ============ Global Way Preference ============
    std::vector<UInt32> free_sets_per_way;
    int global_way = 0;
    UInt64 global_way_rotations = 0;
    UInt64 global_way_attempts = 0;
    UInt64 global_way_hits = 0;
    UInt64 global_way_fallbacks = 0;

    double congestion_free_frac = 0.10;
    UInt32 rotate_cooldown = 0;
    UInt32 rotate_cooldown_max = 1024;
    UInt64 alloc_counter = 0;

    // ============ Thread Safety ============
    mutable std::mutex lock;

    // ============ Per-Core Metadata ============
    std::vector<IntPtr> tag_bases;
    std::vector<IntPtr> fingerprint_bases;

    int tag_entry_bytes;
    int fingerprint_bits;
    bool fingerprint_enabled;

    // ============ Per-App Radix Way Table ============
    std::unique_ptr<RadixWayTableManager> radix_way_tables;

    mutable UInt64 radix_lookups = 0;
    mutable UInt64 radix_hits = 0;
    mutable UInt64 radix_misses = 0;

    // ============ Coalesced 2MB Radix Way Table ============
    //
    // A separate radix tree indexed by 2MB VPN (address >> 21).
    // Stores {valid, way} per coalesced 2MB region.
    //
    // For assoc=8: way_bits=3, leaf_entries=8192, each leaf spans
    // 8192 × 2MB = 16GB.  VPN space = 2^27 → needs only 2 internal
    // levels + 1 leaf = 3 levels total (vs 4 for 4KB radix).
    //
    // During lookupWayRadix(), this tree is checked FIRST.  On a hit
    // the way is returned immediately (3 cache-line reads).  On a miss
    // the per-page 4KB radix tree is walked (4 cache-line reads).
    //
    std::unique_ptr<RadixWayTableManager> radix_way_tables_2mb;
    mutable UInt64 coalesced_2mb_lookups = 0;
    mutable UInt64 coalesced_2mb_hits    = 0;
    mutable UInt64 coalesced_2mb_misses  = 0;

    mutable std::unique_ptr<SimLog> m_log;

public:
    /* ------------------------------------------------------------------ */
    /*  Construction                                                       */
    /* ------------------------------------------------------------------ */
    RestSegCoalesce(int _id, UInt64 size_mb, int _page_size_bits, UInt32 _assoc,
                    int num_cores, int _fingerprint_bits = 4, bool _fingerprint_enabled = true)
        : id(_id),
          page_size_bits(_page_size_bits),
          associativity(_assoc),
          fingerprint_bits(_fingerprint_bits),
          fingerprint_enabled(_fingerprint_enabled)
    {
        size_bytes = size_mb * 1024ULL * 1024ULL;
        UInt64 page_size = 1ULL << page_size_bits;
        num_sets = size_bytes / (associativity * page_size);

        sets.reserve(num_sets);
        for (UInt32 i = 0; i < num_sets; i++) {
            sets.emplace_back(associativity);
        }

        tag_entry_bytes = (48 - page_size_bits - static_cast<int>(std::ceil(std::log2(num_sets)))) / 8;
        if (tag_entry_bytes < 1) tag_entry_bytes = 1;

        tag_bases.resize(num_cores, 0);
        fingerprint_bases.resize(num_cores, 0);

        free_sets_per_way.assign(associativity, num_sets);
        global_way = 0;

        radix_way_tables = std::make_unique<RadixWayTableManager>(page_size_bits, associativity, nullptr);

        // Separate 2MB radix tree for coalesced regions (page_size_bits=21)
        radix_way_tables_2mb = std::make_unique<RadixWayTableManager>(21, associativity, nullptr);

        // ============ Coalescing bitmap initialisation ============
        // All ways start free → bits [0..assoc-1] are 1
        uint64_t all_free = (associativity >= 64) ? ~0ULL : ((1ULL << associativity) - 1);
        set_way_bitmap.assign(num_sets, all_free);

        std::string page_size_str = (page_size_bits == 21) ? "2MB" :
                                    (page_size_bits == 12) ? "4KB" :
                                    (page_size_bits == 30) ? "1GB" : std::to_string(page_size_bits);
        std::string log_name = "RestSegCoalesce-" + page_size_str;
        m_log = std::make_unique<SimLog>(log_name, -1, DEBUG_UTOPIA);

        verifyIndexingLogic();
    }

    void setRadixAllocator(std::function<IntPtr(size_t)> alloc_fn) {
        radix_way_tables     = std::make_unique<RadixWayTableManager>(page_size_bits, associativity, alloc_fn);
        radix_way_tables_2mb = std::make_unique<RadixWayTableManager>(21, associativity, alloc_fn);
    }

    ~RestSegCoalesce() { /* metadata freed by kernel region */ }

    /* ------------------------------------------------------------------ */
    /*  Way-bitmap helpers                                                 */
    /* ------------------------------------------------------------------ */

    /**
     * @brief Toggle a single bit in the way bitmap for a set.
     * @param set_index Set whose bitmap to update
     * @param way       Way index (0..assoc-1)
     * @param is_free   true → set bit to 1 (free), false → set bit to 0 (occupied)
     */
    void updateWayBitmap(UInt32 set_index, int way, bool is_free) {
        if (way < 0 || way >= static_cast<int>(associativity)) return;
        if (set_index >= num_sets) return;
        if (is_free) {
            set_way_bitmap[set_index] |=  (1ULL << way);
        } else {
            set_way_bitmap[set_index] &= ~(1ULL << way);
        }
    }

    /**
     * @brief Coalescing check: can all 512 pages of a 2MB region share one way?
     *
     * Iterates through all 512 4KB pages of the 2MB region starting at
     * @p start_vpn, ANDing together their per-set free-way bitmaps.
     * If any common way survives, returns its index; otherwise returns -1.
     *
     * @param start_vpn VPN of the first 4KB page in the region
     * @return Index of the common free way, or -1
     */
    int findCommonFreeWay(UInt64 start_vpn) {
        // Start with all ways as candidates
        uint64_t candidate_mask = (associativity >= 64) ? ~0ULL : ((1ULL << associativity) - 1);

        for (int i = 0; i < 512; i++) {
            UInt64 current_va = (start_vpn + i) << 12;
            UInt64 tag;
            UInt32 set_index;
            splitAddress(current_va, tag, set_index);

            // Bitwise intersection – keep only ways free in THIS set too
            candidate_mask &= set_way_bitmap[set_index];

            // Fail-fast optimisation
            if (candidate_mask == 0) return -1;
        }

        // Return lowest-index common way
        return __builtin_ctzll(candidate_mask);
    }

    /* ------------------------------------------------------------------ */
    /*  Coalesced 2MB Way helpers (separate 2MB radix tree)                 */
    /* ------------------------------------------------------------------ */

    /**
     * @brief Record that a 2MB region has been coalesced into a single way.
     *
     * Inserts a mapping into the dedicated 2MB radix tree.
     *
     * @param region_start_vpn  Start VPN of the 2MB region (aligned_address >> 12)
     * @param app_id            Application / process ID
     * @param way               The common way shared by all 512 pages
     */
    void setCoalesced2MBWay(UInt64 region_start_vpn, int app_id, int way) {
        UInt64 vpn_2mb = region_start_vpn >> 9;  // 4KB VPN → 2MB VPN  (>> 9 == / 512)
        radix_way_tables_2mb->set_mapping(vpn_2mb, app_id, way);
    }

    /**
     * @brief Look up whether a 2MB region is coalesced.
     * @param region_start_vpn  Start VPN of the 2MB region (aligned_address >> 12)
     * @return The common way, or -1 if the region is not coalesced.
     */
    int lookupCoalesced2MBWay(UInt64 region_start_vpn, int app_id) const {
        UInt64 vpn_2mb = region_start_vpn >> 9;
        int way_out = -1;
        if (radix_way_tables_2mb->lookup_way(vpn_2mb, app_id, way_out))
            return way_out;
        return -1;
    }

    /**
     * @brief Clear a coalesced 2MB mapping (e.g. on de-coalescing / eviction).
     */
    void clearCoalesced2MBWay(UInt64 region_start_vpn, int app_id) {
        UInt64 vpn_2mb = region_start_vpn >> 9;
        radix_way_tables_2mb->clear_mapping(vpn_2mb, app_id);
    }

    UInt64 getCoalesced2MBLookups() const { return coalesced_2mb_lookups; }
    UInt64 getCoalesced2MBHits()    const { return coalesced_2mb_hits; }
    UInt64 getCoalesced2MBMisses()  const { return coalesced_2mb_misses; }

    RadixWayTableManager* getRadixWayTableManager2MB() { return radix_way_tables_2mb.get(); }
    const RadixWayTableManager* getRadixWayTableManager2MB() const { return radix_way_tables_2mb.get(); }

    int getRadix2MBLevels() const {
        return radix_way_tables_2mb ? radix_way_tables_2mb->getNumLevels() : 0;
    }

    /* ------------------------------------------------------------------ */
    /*  Address splitting / reconstruction (identical to RestSeg)          */
    /* ------------------------------------------------------------------ */

    void splitAddress(IntPtr address, UInt64& tag, UInt32& set_index) const {
        UInt64 page_num = address >> page_size_bits;
        tag = page_num / num_sets;
        UInt32 raw_index = page_num % num_sets;
        set_index = (raw_index ^ (static_cast<UInt32>(tag) % num_sets)) % num_sets;
    }

    IntPtr reconstructAddress(UInt64 tag, UInt32 set_index) const {
        UInt32 raw_index = (set_index ^ (static_cast<UInt32>(tag) % num_sets)) % num_sets;
        UInt64 page_num = tag * num_sets + raw_index;
        return page_num << page_size_bits;
    }

private:
    void verifyIndexingLogic() const {
        std::vector<IntPtr> test_vas = {
            0x12345678000ULL, 0xABCDE000ULL, 0x0ULL,
            0x7FFFFFFFFFFF000ULL,
            (1ULL << page_size_bits) * num_sets,
            (1ULL << page_size_bits) * (num_sets + 1),
        };
        for (IntPtr va : test_vas) {
            UInt64 tag; UInt32 set_idx;
            splitAddress(va, tag, set_idx);
            IntPtr reconstructed = reconstructAddress(tag, set_idx);
            IntPtr expected = va & ~((1ULL << page_size_bits) - 1);
            if (reconstructed != expected) {
                throw std::runtime_error(
                    "UtopiaHashCoalesce XOR-Folding Indexing Error: VA=" +
                    std::to_string(va) + " expected=" + std::to_string(expected) +
                    " got=" + std::to_string(reconstructed));
            }
        }
    }

    void maybeRotateGlobalWay() {
        if (rotate_cooldown > 0) { rotate_cooldown--; return; }
        double free_frac = static_cast<double>(free_sets_per_way[global_way]) / static_cast<double>(num_sets);
        if (free_frac >= congestion_free_frac) return;

        int best = global_way;
        UInt32 best_free = free_sets_per_way[global_way];
        for (int w = 0; w < static_cast<int>(associativity); w++) {
            if (free_sets_per_way[w] > best_free) {
                best_free = free_sets_per_way[w]; best = w;
            } else if (free_sets_per_way[w] == best_free && w != global_way) {
                int next_after_current = (global_way + 1) % associativity;
                if (w == next_after_current) best = w;
            }
        }
        if (best != global_way) {
            global_way = best;
            global_way_rotations++;
            rotate_cooldown = rotate_cooldown_max;
        }
    }

public:
    /* ------------------------------------------------------------------ */
    /*  Core operations                                                    */
    /* ------------------------------------------------------------------ */

    bool inRestSeg(IntPtr address, int app_id, bool count_stats = true) {
        std::lock_guard<std::mutex> guard(lock);
        UInt64 tag; UInt32 set_index;
        splitAddress(address, tag, set_index);
        uint8_t fp = computeFingerprint(tag, fingerprint_bits);
        if (count_stats) accesses++;

        UInt64 fp_checks = 0, fp_matches = 0, fp_fp = 0, fp_tp = 0;
        int way = sets[set_index].findEntryWithFingerprintShadow(
                      tag, app_id + 1, fp, fp_checks, fp_matches, fp_fp, fp_tp);
        if (count_stats) {
            fingerprint_checks += fp_checks;
            fingerprint_matches += fp_matches;
            fingerprint_false_positives += fp_fp;
            fingerprint_true_positives += fp_tp;
        }
        if (way >= 0) { if (count_stats) hits++; sets[set_index].touchEntry(way); return true; }
        return false;
    }

    int lookupWay(IntPtr address, int app_id) const {
        std::lock_guard<std::mutex> guard(lock);
        UInt64 tag; UInt32 set_index;
        splitAddress(address, tag, set_index);
        return sets[set_index].findEntry(tag, app_id + 1);
    }

    bool lookupWayRadix(IntPtr address, int app_id, int& way_out) const {
        UInt64 vpn = address >> page_size_bits;
        radix_lookups++;
        UInt64 tag; UInt32 set_index;
        splitAddress(address, tag, set_index);
        m_log->debug("[lookupWayRadix] VA=", SimLog::hex(address), " VPN=", SimLog::hex(vpn),
                     " app_id=", app_id, " set=", set_index, " tag=", SimLog::hex(tag));

        // ── Step 1: Walk the 2MB coalesced radix tree (fewer levels) ──
        // For assoc=8: 2 internal levels + 1 leaf = 3 cache-line reads.
        UInt64 vpn_2mb = address >> 21;
        coalesced_2mb_lookups++;
        if (radix_way_tables_2mb->lookup_way(vpn_2mb, app_id, way_out)) {
            radix_hits++;
            coalesced_2mb_hits++;
            return true;
        }
        coalesced_2mb_misses++;

        // ── Step 2: Walk the 4KB per-page radix tree ──
        bool found = radix_way_tables->lookup_way(vpn, app_id, way_out);
        if (found) { radix_hits++; } else { radix_misses++; }
        return found;
    }

    int getRadixLevels() const {
        return radix_way_tables ? radix_way_tables->getNumLevels() : 0;
    }
    RadixWayTableManager* getRadixWayTableManager() { return radix_way_tables.get(); }
    const RadixWayTableManager* getRadixWayTableManager() const { return radix_way_tables.get(); }

    UInt64 getRadixLookups() const { return radix_lookups; }
    UInt64 getRadixHits() const { return radix_hits; }
    UInt64 getRadixMisses() const { return radix_misses; }
    UInt64* getRadixLookupsPtr() { return &radix_lookups; }
    UInt64* getRadixHitsPtr() { return &radix_hits; }
    UInt64* getRadixMissesPtr() { return &radix_misses; }

    /* ------------------------------------------------------------------ */
    /*  allocate() – with bitmap sync                                      */
    /* ------------------------------------------------------------------ */

    /**
     * Allocate a page in this RestSeg.
     *
     * Bitmap sync: whenever a *new* page claims a previously-free way the
     * corresponding bit is cleared via updateWayBitmap(..., false).
     * Evictions replace valid→valid so the bit stays 0 (no update).
     */
    std::tuple<bool, bool, IntPtr> allocate(IntPtr address, int app_id, bool force_evict = false) {
        std::lock_guard<std::mutex> guard(lock);

        UInt64 tag; UInt32 set_index;
        splitAddress(address, tag, set_index);

        UInt64 vpn   = address >> page_size_bits;
        UInt64 owner = app_id + 1;
        uint8_t fp   = computeFingerprint(tag, fingerprint_bits);

        global_way_attempts++;
        alloc_counter++;

        UInt64 region_id = vpn >> RLE_REGION_BITS;
        int region_pref_way = -1;
        bool use_region_pref = (page_size_bits == 12);

        if (use_region_pref) {
            auto pref_it = region_way_pref.find(region_id);
            if (pref_it != region_way_pref.end()) region_pref_way = pref_it->second.way;
        }

        int  used_way = -1;
        bool was_hit = false, used_free_way = false, success = false, evicted = false;
        UInt64 evicted_tag = 0, evicted_owner_val = 0;
        bool success_from_region_pref = false;

        // Priority 1: region preferred way
        if (region_pref_way >= 0) {
            region_way_attempts++;
            if (sets[set_index].tryInsertAtWay(tag, owner, fp, region_pref_way, used_way, was_hit)) {
                success = true; success_from_region_pref = true;
                if (!was_hit) { used_free_way = true; region_way_hits++; }
            }
        }

        // Priority 2: global_way
        if (!success && region_pref_way != global_way) {
            if (sets[set_index].tryInsertAtWay(tag, owner, fp, global_way, used_way, was_hit)) {
                success = true;
                if (!was_hit) { used_free_way = true; global_way_hits++; }
            }
        }

        // Priority 3: any free way / evict
        if (!success) {
            global_way_fallbacks++;
            auto result = sets[set_index].insert(tag, owner, fp, force_evict, -1, used_way);
            success         = std::get<0>(result);
            evicted         = std::get<1>(result);
            evicted_tag     = std::get<2>(result);
            evicted_owner_val = std::get<3>(result);
            was_hit         = std::get<5>(result);
            used_free_way   = std::get<6>(result);
        }

        // Region preference failure tracking
        if (use_region_pref && !success_from_region_pref && region_pref_way >= 0) {
            auto& pref = region_way_pref[region_id];
            if (++pref.failures >= REGION_PREF_FAILURE_THRESHOLD) {
                pref.way = -1; pref.failures = 0; region_way_relearns++;
            }
        }

        if (!success) return std::make_tuple(false, false, static_cast<IntPtr>(0));

        // ---- Bitmap & stats for NEW allocations only ----
        if (!was_hit) {
            allocations++;

            // *** CRITICAL SYNC: mark way as occupied in bitmap ***
            if (used_free_way && used_way >= 0 && used_way < static_cast<int>(associativity)) {
                updateWayBitmap(set_index, used_way, false);
                if (free_sets_per_way[used_way] > 0) free_sets_per_way[used_way]--;
            }

            if (use_region_pref) {
                auto& pref = region_way_pref[region_id];
                pref.total_allocations++;
                if (!success_from_region_pref) pref.rle_misses++;
                pref.way = used_way; pref.successes++; pref.failures = 0;
            }
            maybeRotateGlobalWay();
        }

        // Handle eviction
        if (evicted) {
            conflicts++;
            IntPtr evicted_addr = reconstructAddress(evicted_tag, set_index);
            UInt64 evicted_vpn  = evicted_addr >> page_size_bits;
            int evicted_app_id  = evicted_owner_val - 1;
            radix_way_tables->clear_mapping(evicted_vpn, evicted_app_id);
            radix_way_tables->set_mapping(vpn, app_id, used_way);
            return std::make_tuple(true, true, evicted_addr);
        }

        // No eviction – update radix for new alloc
        if (!was_hit) {
            radix_way_tables->set_mapping(vpn, app_id, used_way);
        }
        return std::make_tuple(true, false, static_cast<IntPtr>(0));
    }

    /* ------------------------------------------------------------------ */
    /*  tryInsertAtWay() – RestSeg-level helper (with bitmap sync)         */
    /* ------------------------------------------------------------------ */

    /**
     * @brief Insert a page at a *specific* way in its mapped set.
     *
     * Used by the coalescing path to force-place all 512 pages of a 2MB
     * region into the same way.
     *
     * @param address Virtual address of the 4KB page
     * @param app_id  Application / process ID
     * @param way     The specific way to use
     * @return true if insertion succeeded (hit or way was free)
     */
    bool tryInsertAtWay(IntPtr address, int app_id, int way, bool update_radix = true) {
        std::lock_guard<std::mutex> guard(lock);

        UInt64  tag; UInt32 set_index;
        splitAddress(address, tag, set_index);

        UInt64  owner = app_id + 1;
        uint8_t fp    = computeFingerprint(tag, fingerprint_bits);

        int  used_way = -1;
        bool was_hit  = false;

        bool ok = sets[set_index].tryInsertAtWay(tag, owner, fp, way, used_way, was_hit);
        if (!ok) return false;

        if (!was_hit && used_way >= 0) {
            allocations++;
            // *** CRITICAL SYNC: mark way as occupied in bitmap ***
            updateWayBitmap(set_index, used_way, false);
            if (used_way < static_cast<int>(associativity) && free_sets_per_way[used_way] > 0)
                free_sets_per_way[used_way]--;

            // Update radix table (skipped for coalesced 2MB inserts)
            if (update_radix) {
                UInt64 vpn = address >> page_size_bits;
                radix_way_tables->set_mapping(vpn, app_id, used_way);
            }
        }
        return true;
    }

    /* ------------------------------------------------------------------ */
    /*  Physical-address calculations (unchanged from RestSeg)             */
    /* ------------------------------------------------------------------ */

    IntPtr calculatePhysicalAddress(IntPtr address, int app_id) const {
        std::lock_guard<std::mutex> guard(lock);
        UInt64 tag; UInt32 set_index;
        splitAddress(address, tag, set_index);
        int way = sets[set_index].findEntry(tag, app_id + 1);
        if (way < 0) return static_cast<IntPtr>(-1);
        int factor = 1 << (page_size_bits - 12);
        return base_physical_address + (set_index * associativity + way) * factor;
    }

    int getWayForAddress(IntPtr address, int app_id) const {
        std::lock_guard<std::mutex> guard(lock);
        UInt64 tag; UInt32 set_index;
        splitAddress(address, tag, set_index);
        return sets[set_index].findEntry(tag, app_id + 1);
    }

    IntPtr calculatePhysicalAddressForWay(IntPtr address, int way) const {
        UInt64 tag; UInt32 set_index;
        splitAddress(address, tag, set_index);
        int factor = 1 << (page_size_bits - 12);
        IntPtr page_offset = address & ((1ULL << page_size_bits) - 1);
        IntPtr base_phys = base_physical_address + (set_index * associativity + way) * factor;
        return (base_phys << 12) | page_offset;
    }

    IntPtr calculatePPNFromWay(IntPtr address, int way) const {
        UInt64 tag; UInt32 set_index;
        splitAddress(address, tag, set_index);
        int factor = 1 << (page_size_bits - 12);
        return base_physical_address + (set_index * associativity + way) * factor;
    }

    void touchWayLRU(IntPtr address, int way) {
        std::lock_guard<std::mutex> guard(lock);
        UInt64 tag; UInt32 set_index;
        splitAddress(address, tag, set_index);
        sets[set_index].touchEntry(way);
    }

    /* ------------------------------------------------------------------ */
    /*  Metadata address calculations                                      */
    /* ------------------------------------------------------------------ */

    IntPtr calculateTagAddress(IntPtr address, int core_id) const {
        UInt64 tag; UInt32 set_index;
        splitAddress(address, tag, set_index);
        if (core_id < 0 || core_id >= static_cast<int>(tag_bases.size())) return 0;
        return tag_bases[core_id] + set_index * associativity * tag_entry_bytes;
    }

    IntPtr calculateFingerprintAddress(IntPtr address, int core_id) const {
        UInt64 tag; UInt32 set_index;
        splitAddress(address, tag, set_index);
        if (core_id < 0 || core_id >= static_cast<int>(fingerprint_bases.size())) return 0;
        return fingerprint_bases[core_id] + set_index * associativity;
    }

    void calculateRadixLevelAddresses(IntPtr address, int app_id,
                                      std::vector<std::pair<IntPtr, bool>>& level_addresses) const {
        level_addresses.clear();
        if (!radix_way_tables) return;
        UInt64 vpn = address >> page_size_bits;
        std::vector<std::tuple<int, IntPtr, bool>> traversal;
        radix_way_tables->getTraversalAddresses(vpn, app_id, traversal);
        for (const auto& entry : traversal)
            level_addresses.push_back(std::make_pair(std::get<1>(entry), std::get<2>(entry)));
    }

    /**
     * @brief Traversal addresses for a coalesced 2MB lookup.
     *
     * Walks the separate 2MB radix tree (fewer levels than 4KB tree).
     * For assoc=8: 2 internal + 1 leaf = 3 cache-line reads total.
     *
     * Use this for latency modeling when a coalesced 2MB hit occurs.
     */
    void calculateRadixLevelAddressesCoalesced(IntPtr address, int app_id,
                                              std::vector<std::pair<IntPtr, bool>>& level_addresses) const {
        level_addresses.clear();
        if (!radix_way_tables_2mb) return;
        UInt64 vpn_2mb = address >> 21;
        std::vector<std::tuple<int, IntPtr, bool>> traversal;
        radix_way_tables_2mb->getTraversalAddresses(vpn_2mb, app_id, traversal);
        for (const auto& entry : traversal)
            level_addresses.push_back(std::make_pair(std::get<1>(entry), std::get<2>(entry)));
    }

    int getRadixLeafEntries() const {
        return radix_way_tables ? radix_way_tables->getLeafEntries() : 0;
    }

    uint8_t getStoredFingerprint(IntPtr address, int app_id, int way) const {
        std::lock_guard<std::mutex> guard(lock);
        UInt64 tag; UInt32 set_index;
        splitAddress(address, tag, set_index);
        if (way < 0 || way >= static_cast<int>(associativity)) return 0xFF;
        const auto* entry = sets[set_index].getEntry(way);
        if (!entry || !entry->valid) return 0xFF;
        return entry->fingerprint;
    }

    std::vector<int> findMatchingFingerprints(IntPtr address, uint8_t lookup_fingerprint) const {
        std::lock_guard<std::mutex> guard(lock);
        UInt64 tag; UInt32 set_index;
        splitAddress(address, tag, set_index);
        std::vector<int> matching_ways;
        const auto& set = sets[set_index];
        for (UInt32 w = 0; w < associativity; w++) {
            const auto* entry = set.getEntry(w);
            if (entry && entry->valid && entry->fingerprint == lookup_fingerprint)
                matching_ways.push_back(w);
        }
        return matching_ways;
    }

    uint8_t computeLookupFingerprint(IntPtr address) const {
        UInt64 tag; UInt32 set_index;
        splitAddress(address, tag, set_index);
        return computeFingerprint(tag, fingerprint_bits);
    }

    /* ------------------------------------------------------------------ */
    /*  RLE stats dump                                                     */
    /* ------------------------------------------------------------------ */
    void dumpRegionRLEStats() const {
        if (page_size_bits != 12) return;
        std::lock_guard<std::mutex> guard(lock);
        std::string csv_path = Policy::get_output_directory() + "/region_rle_stats_restseg_coalesce" + std::to_string(id) + ".csv";
        std::ofstream csv_file(csv_path);
        if (!csv_file.is_open()) return;
        csv_file << "region_id,total_allocations,rle_misses,rle_hit_percent\n";
        if (region_way_pref.empty()) { csv_file.close(); return; }

        std::vector<std::pair<UInt64, const RegionWayPref*>> sorted;
        for (const auto& [rid, pref] : region_way_pref)
            if (pref.total_allocations > 0) sorted.push_back({rid, &pref});
        std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b){ return a.first < b.first; });

        for (const auto& [rid, pref] : sorted) {
            double hit_pct = pref->total_allocations > 0 ?
                100.0 * (pref->total_allocations - pref->rle_misses) / pref->total_allocations : 0.0;
            csv_file << rid << "," << pref->total_allocations << "," << pref->rle_misses
                     << "," << std::fixed << std::setprecision(2) << hit_pct << "\n";
        }
        csv_file.close();
    }

    /* ------------------------------------------------------------------ */
    /*  Accessors                                                          */
    /* ------------------------------------------------------------------ */

    int getId() const { return id; }
    int getPageSizeBits() const { return page_size_bits; }
    UInt32 getAssociativity() const { return associativity; }
    UInt32 getNumSets() const { return num_sets; }
    UInt64 getSizeBytes() const { return size_bytes; }
    UInt64 getSizeMB() const { return size_bytes / (1024 * 1024); }

    void setBase(IntPtr base) { base_physical_address = base; }
    IntPtr getBase() const { return base_physical_address; }

    void setMetadataBases(int core_id, IntPtr tag_base, IntPtr fp_base) {
        if (core_id >= 0 && core_id < static_cast<int>(tag_bases.size())) {
            tag_bases[core_id] = tag_base;
            fingerprint_bases[core_id] = fp_base;
        }
    }

    UInt64 getTagArraySize() const { return num_sets * associativity * tag_entry_bytes; }
    UInt64 getFingerprintArraySize() const { return num_sets * associativity * 1; }
    bool isFingerprintEnabled() const { return fingerprint_enabled; }

    UInt64 getAccesses() const { return accesses; }
    UInt64 getHits() const { return hits; }
    UInt64 getConflicts() const { return conflicts; }
    UInt64 getAllocations() const { return allocations; }

    UInt64 getFingerprintChecks() const { return fingerprint_checks; }
    UInt64 getFingerprintMatches() const { return fingerprint_matches; }
    UInt64 getFingerprintFalsePositives() const { return fingerprint_false_positives; }
    UInt64 getFingerprintTruePositives() const { return fingerprint_true_positives; }
    UInt64* getFingerprintChecksPtr() { return &fingerprint_checks; }
    UInt64* getFingerprintMatchesPtr() { return &fingerprint_matches; }
    UInt64* getFingerprintFalsePositivesPtr() { return &fingerprint_false_positives; }
    UInt64* getFingerprintTruePositivesPtr() { return &fingerprint_true_positives; }
    int getFingerprintBits() const { return fingerprint_bits; }

    UInt64 getRegionWayAttempts() const { return region_way_attempts; }
    UInt64 getRegionWayHits() const { return region_way_hits; }
    UInt64 getRegionWayRelearns() const { return region_way_relearns; }
    UInt64* getRegionWayAttemptsPtr() { return &region_way_attempts; }
    UInt64* getRegionWayHitsPtr() { return &region_way_hits; }
    UInt64* getRegionWayRelearnsPtr() { return &region_way_relearns; }

    // Coalesced 2MB radix tree stat pointer accessors (const getters already above)
    UInt64* getCoalesced2MBLookupsPtr() { return &coalesced_2mb_lookups; }
    UInt64* getCoalesced2MBHitsPtr()    { return &coalesced_2mb_hits; }
    UInt64* getCoalesced2MBMissesPtr()  { return &coalesced_2mb_misses; }

    UInt64 getRegionWayPrefCount() const {
        std::lock_guard<std::mutex> guard(lock);
        UInt64 count = 0;
        for (const auto& [rid, pref] : region_way_pref)
            if (pref.way >= 0) count++;
        return count;
    }

    int getGlobalWay() const { return global_way; }
    UInt64 getGlobalWayRotations() const { return global_way_rotations; }
    UInt64 getGlobalWayAttempts() const { return global_way_attempts; }
    UInt64 getGlobalWayHits() const { return global_way_hits; }
    UInt64 getGlobalWayFallbacks() const { return global_way_fallbacks; }
    UInt64* getGlobalWayRotationsPtr() { return &global_way_rotations; }
    UInt64* getGlobalWayAttemptsPtr() { return &global_way_attempts; }
    UInt64* getGlobalWayHitsPtr() { return &global_way_hits; }
    UInt64* getGlobalWayFallbacksPtr() { return &global_way_fallbacks; }

    double getWayFreeFraction(int way) const {
        if (way < 0 || way >= static_cast<int>(associativity)) return 0.0;
        return static_cast<double>(free_sets_per_way[way]) / static_cast<double>(num_sets);
    }
    double getGlobalWayFreeFraction() const { return getWayFreeFraction(global_way); }
    double getCongestionFreeFrac() const { return congestion_free_frac; }
    void setCongestionFreeFrac(double frac) { congestion_free_frac = frac; }
    UInt32 getRotateCooldownMax() const { return rotate_cooldown_max; }
    void setRotateCooldownMax(UInt32 max) { rotate_cooldown_max = max; }

    bool verifyFreeSetsPerWay() const {
        std::lock_guard<std::mutex> guard(lock);
        std::vector<UInt32> actual(associativity, 0);
        for (UInt32 s = 0; s < num_sets; s++)
            for (UInt32 w = 0; w < associativity; w++) {
                const auto* e = sets[s].getEntry(w);
                if (!e || !e->valid) actual[w]++;
            }
        for (UInt32 w = 0; w < associativity; w++)
            if (free_sets_per_way[w] != actual[w]) return false;
        return true;
    }

    UInt64 countValidEntries() const {
        std::lock_guard<std::mutex> guard(lock);
        UInt64 count = 0;
        for (const auto& set : sets) count += set.countValid();
        return count;
    }
    UInt64 getTotalCapacity() const { return num_sets * associativity; }
    double getUtilization() const {
        return static_cast<double>(countValidEntries()) / getTotalCapacity();
    }
};


/*
 * ================================================================================
 * UtopiaHashCoalesce – Main Allocator with Coalescing Optimisation
 * ================================================================================
 */
template <typename Policy>
class UtopiaHashCoalesce : public PhysicalMemoryAllocator, private Policy
{
    using BuddyPolicy = typename BuddyPolicyFor<Policy>::type;
    using BuddyType   = Buddy<BuddyPolicy>;

public:
    struct Stats {
        UInt64 restseg_2mb_allocations = 0;
        UInt64 restseg_4kb_allocations = 0;
        UInt64 restseg_evictions       = 0;

        UInt64 flexseg_thp_allocations = 0;
        UInt64 flexseg_4kb_allocations = 0;
        UInt64 flexseg_thp_reserved    = 0;
        UInt64 flexseg_thp_promoted    = 0;
        UInt64 flexseg_thp_demoted     = 0;

        UInt64 total_allocations       = 0;
        UInt64 migrations              = 0;
        UInt64 coalescing_promotions   = 0;   ///< 2MB pages created via coalescing
        UInt64 coalescing_hits         = 0;   ///< fast-path hits on already-coalesced regions

        UInt64 restseg_allocations     = 0;
        UInt64 flexseg_allocations     = 0;
    };

    enum class MigrationPolicyType { None = 0, CostTopK = 1 };

    struct CostTopKEntry {
        bool   valid = false;
        UInt64 vpn   = 0;
        UInt16 score_snapshot = 0;
        UInt8  sticky   = 0;
        UInt8  cooldown = 0;
    };

    struct CostTopKConfig {
        bool enabled            = false;
        int  page_size_bits     = 12;
        int  score_bits         = 10;
        int  ptw_base_inc       = 1;
        int  dram_inc_cap       = 3;
        int  topk_entries       = 16;
        int  sticky_threshold   = 2;
        int  cooldown_epochs    = 2;
        UInt64 rank_epoch_translations   = 1024;
        UInt64 credit_epoch_translations = 8192;
        int  credits_max        = 4;
        int  credits_refill     = 1;
        int  migrate_threshold  = 32;
        int  post_migrate_score_shift = 2;
    };

    struct CostTopKState {
        std::unordered_map<UInt64, UInt16> vpn_cost_score;
        std::vector<CostTopKEntry> topk_table;
        UInt16 topk_min_score   = 0;
        int    topk_min_idx     = -1;
        UInt64 translation_counter   = 0;
        UInt64 rank_epoch_counter    = 0;
        UInt64 credit_epoch_counter  = 0;
        int    credits          = 0;
        UInt64 score_updates    = 0;
        UInt64 topk_inserts     = 0;
        UInt64 topk_replacements= 0;
        UInt64 migration_attempts  = 0;
        UInt64 migrations_issued   = 0;
        UInt64 gated_credits    = 0;
        UInt64 gated_threshold  = 0;
        UInt64 gated_sticky     = 0;
        UInt64 gated_cooldown   = 0;
        IntPtr last_migration_ppn = 0;
    };

private:
    Stats stats;
    std::vector<std::unique_ptr<RestSegCoalesce<Policy>>> restsegs;
    BuddyType* buddy_allocator;

    int num_restsegs;
    int restseg_2mb_index = -1;
    int restseg_4kb_index = -1;
    double thp_promotion_threshold;

    int tlb_eviction_threshold;
    int pte_eviction_threshold;

    std::map<UInt64, std::tuple<UInt64, std::bitset<512>, bool>> thp_regions;

    MigrationPolicyType migration_policy_type = MigrationPolicyType::None;

    bool   last_in_restseg         = false;
    bool   last_caused_eviction    = false;
    IntPtr last_evicted_address    = 0;
    int    last_evicted_page_size_bits = 12;

    CostTopKConfig cost_topk_config;
    CostTopKState  cost_topk_state;

#if ENABLE_UTOPIA_MIGRATION_CSV
    std::ofstream migration_csv_file;
#endif

public:
    Stats&       getStats()       { return stats; }
    const Stats& getStats() const { return stats; }

    /* ================================================================== */
    /*  Constructor                                                        */
    /* ================================================================== */
    UtopiaHashCoalesce(String name, int memory_size, int max_order,
                       int kernel_size, String frag_type)
        : PhysicalMemoryAllocator(name, memory_size, kernel_size)
    {
        static_assert(is_complete<BuddyPolicyFor<Policy>>::value,
                      "BuddyPolicyFor<Policy> is incomplete. Include buddy_policy.h first.");

        num_restsegs            = Policy::read_config_int("perf_model/utopia/RestSegs");
        tlb_eviction_threshold  = Policy::read_config_int("perf_model/utopia/tlb_eviction_thr");
        pte_eviction_threshold  = Policy::read_config_int("perf_model/utopia/pte_eviction_thr");
        thp_promotion_threshold = Policy::read_config_int("perf_model/utopia/thp_promotion_threshold") / 100.0;
        if (thp_promotion_threshold < 0.0 || thp_promotion_threshold > 1.0)
            thp_promotion_threshold = 0.75;

        int num_cores = Policy::get_num_cores();

        std::cout << "[UtopiaHashCoalesce] Creating buddy allocator..." << std::endl;
        buddy_allocator = new BuddyType(memory_size, max_order, kernel_size, frag_type);

        std::cout << "[UtopiaHashCoalesce] Creating " << num_restsegs << " RestSegCoalesce instances..." << std::endl;

        int fingerprint_bits = 4;
        try { fingerprint_bits = Policy::read_config_int("perf_model/utopia/fingerprint_bits");
              if (fingerprint_bits < 1 || fingerprint_bits > 8) fingerprint_bits = 4;
        } catch (...) {}
        std::cout << "[UtopiaHashCoalesce] fingerprint_bits=" << fingerprint_bits << std::endl;

        bool fingerprint_enabled = true;
        try { int v = Policy::read_config_int("perf_model/utopia/fingerprint_enabled");
              fingerprint_enabled = (v != 0);
        } catch (...) {}
        std::cout << "[UtopiaHashCoalesce] Fingerprint " << (fingerprint_enabled ? "ENABLED" : "DISABLED") << std::endl;

        for (int i = 0; i < num_restsegs; i++) {
            int rs_size      = Policy::read_config_int_array("perf_model/utopia/RestSeg/size", i);
            int rs_page_size = Policy::read_config_int_array("perf_model/utopia/RestSeg/page_size", i);
            int rs_assoc     = Policy::read_config_int_array("perf_model/utopia/RestSeg/assoc", i);

            auto rs = std::make_unique<RestSegCoalesce<Policy>>(
                          i + 1, rs_size, rs_page_size, rs_assoc, num_cores,
                          fingerprint_bits, fingerprint_enabled);

            UInt64 rs_bytes = rs->getSizeMB() * 1024ULL * 1024ULL;
            UInt64 base_ppn = this->handle_page_table_allocations(rs_bytes);
            UInt64 page_size_in_4kb_pages = 1ULL << (rs_page_size - 12);
            UInt64 aligned_base_ppn = ((base_ppn + page_size_in_4kb_pages - 1) /
                                        page_size_in_4kb_pages) * page_size_in_4kb_pages;
            rs->setBase(aligned_base_ppn);

            UInt64 tag_size = rs->getTagArraySize();
            UInt64 fp_size  = rs->getFingerprintArraySize();
            for (int c = 0; c < num_cores; c++) {
                UInt64  tag_ppn = this->handle_page_table_allocations(tag_size);
                IntPtr  tag_addr = tag_ppn * 4096;
                UInt64  fp_ppn  = this->handle_page_table_allocations(fp_size);
                IntPtr  fp_addr = fp_ppn * 4096;
                rs->setMetadataBases(c, tag_addr, fp_addr);
            }

            auto radix_alloc = [this](size_t size) -> IntPtr {
                UInt64 ppn = this->handle_page_table_allocations(size);
                return static_cast<IntPtr>(ppn * 4096);
            };
            rs->setRadixAllocator(radix_alloc);

            std::cout << "[UtopiaHashCoalesce] RestSegCoalesce[" << i << "]: size=" << rs_size
                      << "MB page_size=" << rs_page_size << " assoc=" << rs_assoc
                      << " base_ppn=" << aligned_base_ppn << std::endl;

            if (rs_page_size == 21) restseg_2mb_index = i;
            else if (rs_page_size == 12) restseg_4kb_index = i;

            restsegs.push_back(std::move(rs));
        }

        // ============ Cost-based Top-K Migration ============
        try {
            int en = Policy::read_config_int("perf_model/utopia/migration_cost_topk/enabled");
            cost_topk_config.enabled = (en != 0);
            migration_policy_type = cost_topk_config.enabled ? MigrationPolicyType::CostTopK
                                                             : MigrationPolicyType::None;
        } catch (...) { cost_topk_config.enabled = false; migration_policy_type = MigrationPolicyType::None; }

        if (cost_topk_config.enabled) {
            try { cost_topk_config.page_size_bits = Policy::read_config_int("perf_model/utopia/migration_cost_topk/page_size_bits"); } catch (...) {}
            try { cost_topk_config.score_bits = Policy::read_config_int("perf_model/utopia/migration_cost_topk/score_bits"); } catch (...) {}
            try { cost_topk_config.ptw_base_inc = Policy::read_config_int("perf_model/utopia/migration_cost_topk/ptw_base_increment"); } catch (...) {}
            try { cost_topk_config.dram_inc_cap = Policy::read_config_int("perf_model/utopia/migration_cost_topk/dram_weighted_increment_cap"); } catch (...) {}
            try { cost_topk_config.topk_entries = Policy::read_config_int("perf_model/utopia/migration_cost_topk/topk_entries"); } catch (...) {}
            try { cost_topk_config.sticky_threshold = Policy::read_config_int("perf_model/utopia/migration_cost_topk/sticky_threshold"); } catch (...) {}
            try { cost_topk_config.cooldown_epochs = Policy::read_config_int("perf_model/utopia/migration_cost_topk/cooldown_epochs"); } catch (...) {}
            try { cost_topk_config.rank_epoch_translations = Policy::read_config_int("perf_model/utopia/migration_cost_topk/ranking_epoch_translations"); } catch (...) {}
            try { cost_topk_config.credit_epoch_translations = Policy::read_config_int("perf_model/utopia/migration_cost_topk/credit_epoch_translations"); } catch (...) {}
            try { cost_topk_config.credits_max = Policy::read_config_int("perf_model/utopia/migration_cost_topk/credits_max"); } catch (...) {}
            try { cost_topk_config.credits_refill = Policy::read_config_int("perf_model/utopia/migration_cost_topk/credits_refill"); } catch (...) {}
            try { cost_topk_config.migrate_threshold = Policy::read_config_int("perf_model/utopia/migration_cost_topk/migrate_threshold"); } catch (...) {}
            try { cost_topk_config.post_migrate_score_shift = Policy::read_config_int("perf_model/utopia/migration_cost_topk/post_migrate_score_shift"); } catch (...) {}

            cost_topk_state.topk_table.resize(cost_topk_config.topk_entries);
            cost_topk_state.credits = cost_topk_config.credits_max;

            std::cout << "[UtopiaHashCoalesce] Cost-based Top-K migration ENABLED: K="
                      << cost_topk_config.topk_entries << " threshold=" << cost_topk_config.migrate_threshold
                      << " credits_max=" << cost_topk_config.credits_max << std::endl;
        }

        Policy::on_init(name, memory_size, kernel_size, this);

        this->log("UtopiaHashCoalesce initialized: RestSeg-2MB idx=", restseg_2mb_index,
                  " RestSeg-4KB idx=", restseg_4kb_index,
                  " THP threshold=", thp_promotion_threshold);

#if ENABLE_UTOPIA_MIGRATION_CSV
        std::string csv_path = Policy::get_output_directory() + "/utopia_hash_coalesce_migrations.csv";
        migration_csv_file.open(csv_path);
        if (migration_csv_file.is_open())
            migration_csv_file << "vpn,old_ppn,new_ppn,evicted_vpn,app_id,target_page_bits" << std::endl;
#endif
    }

    ~UtopiaHashCoalesce() {
#if ENABLE_UTOPIA_MIGRATION_CSV
        if (migration_csv_file.is_open()) migration_csv_file.close();
#endif
        delete buddy_allocator;
    }

    void dumpAllRLEStats() {
        for (int i = 0; i < num_restsegs; i++)
            if (restsegs[i]) restsegs[i]->dumpRegionRLEStats();
    }
    void dumpFinalStats() override { dumpAllRLEStats(); }

    /* ================================================================== */
    /*  THP helpers (identical to UtopiaAllocator)                         */
    /* ================================================================== */
protected:
    std::pair<UInt64, bool> tryAllocateTHP(UInt64 address, UInt64 core_id) {
        UInt64 region_2mb = address >> 21;
        int offset_4kb = (address >> 12) & 0x1FF;

        if (thp_regions.find(region_2mb) == thp_regions.end()) {
            auto reserved = buddy_allocator->reserve_2mb_page(address, core_id);
            if (std::get<0>(reserved) == static_cast<UInt64>(-1))
                return std::make_pair(static_cast<UInt64>(-1), false);
            thp_regions[region_2mb] = std::make_tuple(std::get<0>(reserved), std::bitset<512>(), false);
            stats.flexseg_thp_reserved++;
        }

        auto& region = thp_regions[region_2mb];
        auto& [base_ppn, bitset, promoted] = region;

        if (promoted) return std::make_pair(base_ppn, true);
        if (bitset.test(offset_4kb)) return std::make_pair(base_ppn + offset_4kb, false);

        bitset.set(offset_4kb);
        double utilization = static_cast<double>(bitset.count()) / 512.0;
        if (utilization >= thp_promotion_threshold) {
            promoted = true;
            stats.flexseg_thp_promoted++;
            return std::make_pair(base_ppn, true);
        }
        return std::make_pair(base_ppn + offset_4kb, false);
    }

    bool demoteTHPRegion() {
        std::vector<std::pair<UInt64, double>> utilizations;
        for (auto& [region_idx, region] : thp_regions) {
            auto& [base_ppn, bitset, promoted] = region;
            if (promoted) continue;
            utilizations.push_back({region_idx, static_cast<double>(bitset.count()) / 512.0});
        }
        if (utilizations.empty()) return false;
        std::sort(utilizations.begin(), utilizations.end(),
                  [](auto& a, auto& b){ return a.second < b.second; });

        UInt64 victim = utilizations[0].first;
        auto& [base_ppn, bitset, promoted] = thp_regions[victim];
        for (int i = 0; i < 512; i++)
            if (!bitset[i]) buddy_allocator->free(base_ppn + i, base_ppn + i + 1);
        stats.flexseg_thp_demoted++;
        thp_regions.erase(victim);
        return true;
    }

    IntPtr isLargePageReserved(IntPtr address) {
        UInt64 r = address >> 21;
        auto it = thp_regions.find(r);
        return (it != thp_regions.end()) ? std::get<0>(it->second) : static_cast<IntPtr>(-1);
    }

    void clearTHPBit(IntPtr address) {
        UInt64 r = address >> 21;
        auto it = thp_regions.find(r);
        if (it != thp_regions.end()) {
            UInt64 offset = (address >> 12) & 0x1FF;
            std::get<1>(it->second).reset(offset);
        }
    }

    bool isInRestSeg(IntPtr address, int app_id) {
        for (int i = 0; i < num_restsegs; i++)
            if (restsegs[i]->inRestSeg(address, app_id, false)) return true;
        return false;
    }

    /* ================================================================== */
    /*  Core Allocation – with L0 Coalescing Check                         */
    /* ================================================================== */
public:

    /**
     * @brief Allocate a physical page for a virtual address.
     *
     * Allocation hierarchy (priority order):
     *   L0  Coalescing check (NEW) – align down to 2MB, try bitmap intersection
     *   L1  RestSeg-2MB
     *   L2  FlexSeg-THP (2MB via buddy reservation)
     *   L3  RestSeg-4KB
     *   L4  FlexSeg-4KB fallback
     */
    std::pair<UInt64, UInt64> allocate(UInt64 size, UInt64 address = 0,
                                       UInt64 core_id = -1,
                                       bool is_pagetable_allocation = false,
                                       bool is_instruction_allocation = false) override
    {
        stats.total_allocations++;

        this->log_debug("┌─ allocate(): VA=", SimLog::hex(address), " core=", core_id,
                        " is_pt=", is_pagetable_allocation, " alloc#", stats.total_allocations);

        // Page table allocations → buddy 4KB
        if (is_pagetable_allocation) {
            auto page = buddy_allocator->allocate(size, address, core_id);
            stats.flexseg_4kb_allocations++; stats.flexseg_allocations++;
            return std::make_pair(page, 12ULL);
        }

        // Instruction allocations → reserved area
        if (is_instruction_allocation) {
            return this->allocateInstruction(size);
        }

        // ============ LEVEL 0: Coalescing Optimisation ============
        // For any 4KB request, align down to the 2MB boundary and check
        // whether all 512 underlying 4KB pages can share one way.
        if (size == 4096) {
            if (restseg_4kb_index >= 0) {
                // Align down to 2MB boundary – coalescing works for any page in the region
                UInt64 aligned_address = address & ~((2ULL * 1024 * 1024) - 1);
                UInt64 start_vpn = aligned_address >> 12;  // start VPN = radix-leaf key
                auto* target_seg = restsegs[restseg_4kb_index].get();

                // Fast-path: if this region is already coalesced, return immediately
                // (checks the coalesced header in the radix leaf — one cache-line read)
                int existing_way = target_seg->lookupCoalesced2MBWay(start_vpn, core_id);
                if (existing_way >= 0) {
                    stats.coalescing_hits++;
                    stats.restseg_allocations++;
                    last_in_restseg      = true;
                    last_caused_eviction = false;
                    last_evicted_address = 0;
                    IntPtr base_ppn = target_seg->calculatePhysicalAddress(aligned_address, core_id);
                    this->log_debug("└─ [L0] ✓ Coalesced hit: VA=", SimLog::hex(address),
                                    " region=", SimLog::hex(aligned_address),
                                    " -> PPN=", base_ppn, " (way=", existing_way, ")");
                    return std::make_pair(static_cast<UInt64>(base_ppn), 21ULL);
                }

                // Try to coalesce: find a way free across all 512 sets in the region
                int common_way = target_seg->findCommonFreeWay(start_vpn);

                if (common_way != -1) {
                    this->log_debug("├─ [L0] Coalescing: common_way=", common_way,
                                    " for 512 pages starting at VPN=", SimLog::hex(start_vpn));

                    // Force-place all 512 pages into `common_way` (skip per-page radix updates)
                    for (int i = 0; i < 512; i++) {
                        UInt64 page_addr = aligned_address + (static_cast<UInt64>(i) * 4096);
                        target_seg->tryInsertAtWay(page_addr, core_id, common_way, false);
                    }

                    // Store coalesced 2MB entry in the radix leaf header
                    target_seg->setCoalesced2MBWay(start_vpn, core_id, common_way);

                    stats.restseg_2mb_allocations++;
                    stats.restseg_allocations++;
                    stats.coalescing_promotions++;

                    last_in_restseg      = true;
                    last_caused_eviction = false;
                    last_evicted_address = 0;

                    // Calculate PPN for the aligned base address
                    IntPtr base_ppn = target_seg->calculatePhysicalAddress(aligned_address, core_id);

                    this->log_debug("└─ [L0] ✓ Coalesced 2MB: VA=", SimLog::hex(address),
                                    " region=", SimLog::hex(aligned_address),
                                    " -> PPN=", base_ppn, " (way=", common_way, ")");

                    return std::make_pair(static_cast<UInt64>(base_ppn), 21ULL);
                }
                this->log_trace("│  └─ [L0] ✗ Coalescing failed (no common way)");
            }
        }

        // ============ LEVEL 1: Try RestSeg-2MB ============
        if (restseg_2mb_index >= 0) {
            this->log_trace("├─ [L1] Trying RestSeg-2MB...");
            auto [success, evicted, evicted_addr] =
                restsegs[restseg_2mb_index]->allocate(address, core_id, false);
            (void)evicted; (void)evicted_addr;

            if (success) {
                stats.restseg_2mb_allocations++; stats.restseg_allocations++;
                last_in_restseg = true; last_caused_eviction = false; last_evicted_address = 0;
                IntPtr ppn = restsegs[restseg_2mb_index]->calculatePhysicalAddress(address, core_id);
                this->log_debug("└─ [L1] ✓ RestSeg-2MB: PPN=", ppn);
                return std::make_pair(static_cast<UInt64>(ppn), 21ULL);
            }
            this->log_trace("│  └─ [L1] ✗ RestSeg-2MB full");
        }

        // ============ LEVEL 2: FlexSeg-THP ============
        this->log_trace("├─ [L2] Trying FlexSeg-THP...");
        {
            auto [phys, promoted] = tryAllocateTHP(address, core_id);
            if (phys != static_cast<UInt64>(-1)) {
                last_in_restseg = false; last_caused_eviction = false; last_evicted_address = 0;
                if (promoted) {
                    stats.flexseg_thp_allocations++; stats.flexseg_allocations++;
                    this->log_debug("└─ [L2] ✓ FlexSeg-THP (promoted): PPN=", phys);
                    return std::make_pair(phys, 21ULL);
                } else {
                    stats.flexseg_4kb_allocations++; stats.flexseg_allocations++;
                    this->log_debug("└─ [L2] ✓ FlexSeg-THP (4KB in 2MB): PPN=", phys);
                    return std::make_pair(phys, 12ULL);
                }
            }
            this->log_trace("│  └─ [L2] ✗ THP reservation failed");
        }

        // ============ LEVEL 3: RestSeg-4KB ============
        if (restseg_4kb_index >= 0) {
            this->log_trace("├─ [L3] Trying RestSeg-4KB...");
            auto [success, evicted, evicted_addr] =
                restsegs[restseg_4kb_index]->allocate(address, core_id, false);
            (void)evicted; (void)evicted_addr;

            if (success) {
                stats.restseg_4kb_allocations++; stats.restseg_allocations++;
                last_in_restseg = true; last_caused_eviction = false; last_evicted_address = 0;
                IntPtr ppn = restsegs[restseg_4kb_index]->calculatePhysicalAddress(address, core_id);
                this->log_debug("└─ [L3] ✓ RestSeg-4KB: PPN=", ppn);
                return std::make_pair(static_cast<UInt64>(ppn), 12ULL);
            }
            this->log_trace("│  └─ [L3] ✗ RestSeg-4KB full");
        }

        // ============ LEVEL 4: FlexSeg-4KB fallback ============
        this->log_trace("├─ [L4] Falling back to FlexSeg-4KB...");
        {
            last_in_restseg = false; last_caused_eviction = false; last_evicted_address = 0;
            auto page = buddy_allocator->allocate(size, address, core_id);
            if (page != static_cast<UInt64>(-1)) {
                stats.flexseg_4kb_allocations++; stats.flexseg_allocations++;
                this->log_debug("└─ [L4] ✓ FlexSeg-4KB: PPN=", page);
                return std::make_pair(page, 12ULL);
            }
        }

        this->log("└─ [ERROR] Out of memory! VA=", SimLog::hex(address));
        return std::make_pair(static_cast<UInt64>(-1), 12ULL);
    }

    /* ================================================================== */
    /*  Remaining API (mirrors UtopiaAllocator)                            */
    /* ================================================================== */

    std::vector<Range> allocate_ranges(IntPtr start_va, IntPtr end_va, int app_id) override {
        return std::vector<Range>();
    }

    UInt64 givePageFast(UInt64 bytes, UInt64 address = 0, UInt64 core_id = -1) override {
        return buddy_allocator->allocate(bytes, address, core_id);
    }

    void deallocate(UInt64 address, UInt64 core_id = -1) override { /* TODO */ }

    void fragment_memory(double target) override {
        buddy_allocator->fragmentMemory(target);
    }

    // ============ RestSeg Access ============

    RestSegCoalesce<Policy>* getRestSeg2MB() {
        return (restseg_2mb_index >= 0) ? restsegs[restseg_2mb_index].get() : nullptr;
    }
    RestSegCoalesce<Policy>* getRestSeg4KB() {
        return (restseg_4kb_index >= 0) ? restsegs[restseg_4kb_index].get() : nullptr;
    }
    RestSegCoalesce<Policy>* getRestSeg(int index) {
        return (index >= 0 && index < num_restsegs) ? restsegs[index].get() : nullptr;
    }

    int    getNumRestSegs()         const { return num_restsegs; }
    int    getRestSeg2MBIndex()     const { return restseg_2mb_index; }
    int    getRestSeg4KBIndex()     const { return restseg_4kb_index; }
    double getTHPPromotionThreshold() const { return thp_promotion_threshold; }
    int    getTLBThreshold()        const { return tlb_eviction_threshold; }
    int    getPTEThreshold()        const { return pte_eviction_threshold; }

    bool   wasLastInRestSeg()       const { return last_in_restseg; }
    bool   getLastAllocatedInRestSeg() const { return last_in_restseg; }
    bool   didLastCauseEviction()   const { return last_caused_eviction; }
    IntPtr getLastEvictedAddress()  const { return last_evicted_address; }
    int    getLastEvictedPageSizeBits() const { return last_evicted_page_size_bits; }

    bool checkIsInRestSeg(IntPtr address, int app_id) { return isInRestSeg(address, app_id); }

    // ============ Migration ============

    IntPtr migratePage(IntPtr address, IntPtr old_ppn, int target_page_size_bits, int app_id) {
        if (isInRestSeg(address, app_id)) return 0;
        UInt64 region_2mb = address >> 21;
        if (thp_regions.find(region_2mb) != thp_regions.end()) return 0;

        last_caused_eviction = false; last_evicted_address = 0; last_evicted_page_size_bits = 12;

        for (int i = 0; i < num_restsegs; i++) {
            if (restsegs[i]->getPageSizeBits() == target_page_size_bits) {
                auto [success, evicted, evicted_addr] = restsegs[i]->allocate(address, app_id, true);
                if (success) {
                    stats.migrations++;
                    if (evicted) {
                        stats.restseg_evictions++;
                        last_caused_eviction = true;
                        last_evicted_address = evicted_addr;
                        last_evicted_page_size_bits = restsegs[i]->getPageSizeBits();
                    }
                    buddy_allocator->free(old_ppn, old_ppn);
                    this->deletePageTableEntry(address, app_id);
                    IntPtr new_ppn = restsegs[i]->calculatePhysicalAddress(address, app_id);

#if ENABLE_UTOPIA_MIGRATION_CSV
                    if (migration_csv_file.is_open()) {
                        UInt64 vpn = address >> 12;
                        UInt64 evicted_vpn = evicted ? (evicted_addr >> 12) : 0;
                        migration_csv_file << "0x" << std::hex << vpn
                                           << ",0x" << old_ppn << ",0x" << new_ppn
                                           << ",0x" << evicted_vpn << "," << std::dec << app_id
                                           << "," << target_page_size_bits << std::endl;
                    }
#endif
                    return new_ppn;
                }
            }
        }
        return 0;
    }

    MigrationPolicyType getMigrationPolicyType() const { return migration_policy_type; }

    // ============ Cost-based Top-K API ============

    UInt16 getCostScore(UInt64 vpn) const {
        auto it = cost_topk_state.vpn_cost_score.find(vpn);
        return (it != cost_topk_state.vpn_cost_score.end()) ? it->second : 0;
    }

    void setCostScore(UInt64 vpn, UInt16 score) {
        UInt16 max_s = (1 << cost_topk_config.score_bits) - 1;
        cost_topk_state.vpn_cost_score[vpn] = std::min(score, max_s);
    }

    UInt16 bumpCostScore(UInt64 vpn, UInt16 inc) {
        UInt16 max_s = (1 << cost_topk_config.score_bits) - 1;
        UInt16 old_s = getCostScore(vpn);
        UInt16 new_s = std::min(static_cast<UInt16>(old_s + inc), max_s);
        cost_topk_state.vpn_cost_score[vpn] = new_s;
        cost_topk_state.score_updates++;
        return new_s;
    }

    void updateCostTopK(UInt64 vpn, UInt16 score, bool count_stats) {
        if (!cost_topk_config.enabled) return;
        auto& table = cost_topk_state.topk_table;
        int k = cost_topk_config.topk_entries;

        for (int i = 0; i < k; i++) {
            if (table[i].valid && table[i].vpn == vpn) {
                table[i].score_snapshot = score;
                if (table[i].sticky < 3) table[i].sticky++;
                recomputeTopKMin(); return;
            }
        }
        for (int i = 0; i < k; i++) {
            if (!table[i].valid) {
                table[i] = {true, vpn, score, 1, 0};
                if (count_stats) cost_topk_state.topk_inserts++;
                recomputeTopKMin(); return;
            }
        }
        if (score > cost_topk_state.topk_min_score && cost_topk_state.topk_min_idx >= 0) {
            int idx = cost_topk_state.topk_min_idx;
            table[idx] = {true, vpn, score, 1, 0};
            if (count_stats) cost_topk_state.topk_replacements++;
            recomputeTopKMin();
        }
    }

    void processEpochs(bool count_stats) {
        if (!cost_topk_config.enabled) return;
        cost_topk_state.translation_counter++;
        cost_topk_state.rank_epoch_counter++;
        cost_topk_state.credit_epoch_counter++;

        if (cost_topk_state.rank_epoch_counter >= cost_topk_config.rank_epoch_translations) {
            cost_topk_state.rank_epoch_counter = 0;
            for (auto& e : cost_topk_state.topk_table) {
                if (e.valid) { e.score_snapshot >>= 1; if (e.sticky > 0) e.sticky--; if (e.cooldown > 0) e.cooldown--; }
            }
            recomputeTopKMin();
        }
        if (cost_topk_state.credit_epoch_counter >= cost_topk_config.credit_epoch_translations) {
            cost_topk_state.credit_epoch_counter = 0;
            cost_topk_state.credits = std::min(cost_topk_state.credits + cost_topk_config.credits_refill,
                                               cost_topk_config.credits_max);
        }
    }

    std::pair<bool, IntPtr> considerCostBasedMigration(IntPtr va, IntPtr current_ppn, int page_size_bits,
                                                       int app_id, UInt64 vpn, bool count_stats) {
        if (!cost_topk_config.enabled) return {false, 0};
        if (page_size_bits != cost_topk_config.page_size_bits) return {false, 0};
        if (count_stats) cost_topk_state.migration_attempts++;

        int found_idx = -1;
        for (int i = 0; i < cost_topk_config.topk_entries; i++)
            if (cost_topk_state.topk_table[i].valid && cost_topk_state.topk_table[i].vpn == vpn)
                { found_idx = i; break; }
        if (found_idx < 0) return {false, 0};

        auto& entry = cost_topk_state.topk_table[found_idx];
        if (entry.score_snapshot < static_cast<UInt16>(cost_topk_config.migrate_threshold))
            { if (count_stats) cost_topk_state.gated_threshold++; return {false, 0}; }
        if (entry.sticky < static_cast<UInt8>(cost_topk_config.sticky_threshold))
            { if (count_stats) cost_topk_state.gated_sticky++; return {false, 0}; }
        if (entry.cooldown > 0) { if (count_stats) cost_topk_state.gated_cooldown++; return {false, 0}; }
        if (cost_topk_state.credits <= 0) { if (count_stats) cost_topk_state.gated_credits++; return {false, 0}; }

        int target_psb = 12;
        if (restseg_4kb_index >= 0) target_psb = restsegs[restseg_4kb_index]->getPageSizeBits();
        else if (restseg_2mb_index >= 0) target_psb = 21;
        else return {false, 0};

        IntPtr new_ppn = migratePage(va, current_ppn, target_psb, app_id);
        if (new_ppn != 0) {
            cost_topk_state.credits--;
            entry.cooldown = cost_topk_config.cooldown_epochs;
            UInt16 old_score = getCostScore(vpn);
            UInt16 new_score = old_score >> cost_topk_config.post_migrate_score_shift;
            setCostScore(vpn, new_score); entry.score_snapshot = new_score;
            cost_topk_state.last_migration_ppn = new_ppn;
            if (count_stats) cost_topk_state.migrations_issued++;
            return {true, new_ppn};
        }
        return {false, 0};
    }

    IntPtr getLastMigrationPPN() const { return cost_topk_state.last_migration_ppn; }
    bool   isCostTopKEnabled()   const { return cost_topk_config.enabled; }
    const CostTopKConfig& getCostTopKConfig() const { return cost_topk_config; }
    CostTopKState&       getCostTopKState()       { return cost_topk_state; }
    const CostTopKState& getCostTopKState() const { return cost_topk_state; }

private:
    void recomputeTopKMin() {
        cost_topk_state.topk_min_score = UINT16_MAX;
        cost_topk_state.topk_min_idx   = -1;
        for (int i = 0; i < cost_topk_config.topk_entries; i++) {
            if (cost_topk_state.topk_table[i].valid &&
                cost_topk_state.topk_table[i].score_snapshot < cost_topk_state.topk_min_score) {
                cost_topk_state.topk_min_score = cost_topk_state.topk_table[i].score_snapshot;
                cost_topk_state.topk_min_idx   = i;
            }
        }
        if (cost_topk_state.topk_min_idx < 0) cost_topk_state.topk_min_score = 0;
    }

public:
    BuddyType* getBuddyAllocator() { return buddy_allocator; }
    static const UInt64 HASH_PRIME = 124183;
};

// ============ Default type alias for Sniper integration ============
#include "memory_management/policies/utopia_coalesce_policy.h"
#include "memory_management/policies/buddy_policy.h"

/// Default UtopiaHashCoalesce allocator using Sniper integration policy
using UtopiaCoalesce = UtopiaHashCoalesce<Sniper::UtopiaCoalesce::MetricsPolicy>;
