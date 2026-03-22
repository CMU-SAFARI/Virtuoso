#ifndef UTOPIA_TLB_H
#define UTOPIA_TLB_H

#include "fixed_types.h"
#include "subsecond_time.h"
#include "stats.h"
#include "sim_log.h"
#include <vector>
#include <cstdint>

namespace ParametricDramDirectoryMSI
{

/**
 * @brief Utopia TLB (UTLB) - A compact TLB for RestSeg translations
 * 
 * ============================================================================
 *                         UTLB ARCHITECTURE
 * ============================================================================
 * 
 * The UTLB is a specialized TLB optimized for Utopia's RestSeg translations.
 * Unlike conventional TLBs that store full physical page numbers (PPNs), the
 * UTLB exploits RestSeg's range-based translation property:
 * 
 *   PA = VA + restseg[seg_id].base_offset
 * 
 * This means we only need to store:
 *   - VPN tag (after removing set index bits)
 *   - RestSeg ID (1 bit for 2 RestSegs)
 *   - Way index in RestSeg data array (4 bits for up to 16-way)
 * 
 * ============================================================================
 *                         ENTRY FORMAT
 * ============================================================================
 * 
 *   Standard TLB Entry (~96 bits):
 *   ┌──────────────────┬──────────────────┬──────────────────┐
 *   │     VPN Tag      │       PPN        │     Metadata     │
 *   │     36 bits      │     40 bits      │     20 bits      │
 *   └──────────────────┴──────────────────┴──────────────────┘
 * 
 *   UTLB Entry (~34 bits for 2K entries, 8-way):
 *   ┌──────────────────┬─────────┬───────────┬───────┬───────┐
 *   │     VPN Tag      │ Seg ID  │ Way Index │ Valid │ PgSz  │
 *   │     28 bits      │  1 bit  │   4 bits  │ 1 bit │ 1 bit │
 *   └──────────────────┴─────────┴───────────┴───────┴───────┘
 * 
 *   Capacity improvement: ~2.8x more entries for same SRAM budget!
 * 
 * ============================================================================
 *                         LOOKUP FLOW
 * ============================================================================
 * 
 *   1. Hash VPN to get set index
 *   2. Compare VPN tag against all ways in the set
 *   3. On hit: Retrieve seg_id and way_idx
 *   4. Compute PA using RestSeg base offset:
 *      PA = restseg[seg_id].calculatePhysicalAddress(VA)
 * 
 * ============================================================================
 */

/**
 * @brief A single UTLB entry - compact representation for RestSeg translations
 */
struct UTLBEntry {
    uint64_t vpn_tag;       ///< VPN tag (after removing set index bits)
    uint8_t  seg_id;        ///< RestSeg ID (0 or 1)
    uint8_t  way_idx;       ///< Way index in RestSeg data array (0-15)
    uint8_t  page_size_bit; ///< Page size: 0 = 4KB (12 bits), 1 = 2MB (21 bits)
    bool     valid;         ///< Valid bit
    
    UTLBEntry() : vpn_tag(0), seg_id(0), way_idx(0), page_size_bit(0), valid(false) {}
    
    /// Get the actual page size in bits (12 for 4KB, 21 for 2MB)
    int getPageSizeBits() const { return page_size_bit ? 21 : 12; }
    
    /// Calculate the bits per entry for capacity analysis
    static int getBitsPerEntry(int num_sets, int num_restsegs, int max_ways) {
        // VPN tag bits = 48 - page_offset - set_index_bits
        // For 4KB pages with 256 sets: 48 - 12 - 8 = 28 bits
        int set_index_bits = 0;
        int temp = num_sets;
        while (temp > 1) { temp >>= 1; set_index_bits++; }
        
        int vpn_tag_bits = 48 - 12 - set_index_bits;  // Use 4KB as base
        int seg_id_bits = 0;
        temp = num_restsegs;
        while (temp > 1) { temp >>= 1; seg_id_bits++; }
        
        int way_idx_bits = 0;
        temp = max_ways;
        while (temp > 1) { temp >>= 1; way_idx_bits++; }
        
        // +1 for valid, +1 for page_size
        return vpn_tag_bits + seg_id_bits + way_idx_bits + 1 + 1;
    }
};

/**
 * @brief Result of a UTLB lookup
 */
struct UTLBLookupResult {
    bool     hit;           ///< True if translation found
    uint8_t  seg_id;        ///< RestSeg ID (valid if hit)
    uint8_t  way_idx;       ///< Way index in RestSeg (valid if hit)
    int      page_size;     ///< Page size in bits (valid if hit)
    
    UTLBLookupResult() : hit(false), seg_id(0), way_idx(0), page_size(0) {}
    
    UTLBLookupResult(bool hit, uint8_t seg_id, uint8_t way_idx, int page_size)
        : hit(hit), seg_id(seg_id), way_idx(way_idx), page_size(page_size) {}
};

/**
 * @brief The Utopia TLB class - a compact set-associative TLB for RestSeg
 */
class UtopiaTLB {
private:
    String m_name;
    core_id_t m_core_id;
    
    // Configuration
    UInt32 m_num_entries;       ///< Total number of entries
    UInt32 m_associativity;     ///< Ways per set
    UInt32 m_num_sets;          ///< Number of sets
    UInt32 m_set_index_bits;    ///< Bits needed for set index
    
    // The actual storage: vector of sets, each set is a vector of entries
    std::vector<std::vector<UTLBEntry>> m_sets;
    
    // LRU tracking: for each set, track access order of ways
    std::vector<std::vector<uint8_t>> m_lru_stack;
    
    // Latency
    SubsecondTime m_access_latency;
    
    // Statistics
    struct {
        UInt64 accesses;
        UInt64 hits;
        UInt64 misses;
        UInt64 evictions;
        UInt64 allocations;
    } m_stats;
    
    // Logging
    SimLog* m_log;
    
    // Helper functions
    UInt64 getSetIndex(IntPtr address, int page_size_bits) const;
    UInt64 getVPNTag(IntPtr address, int page_size_bits) const;
    void updateLRU(UInt32 set_idx, UInt32 way_idx);
    UInt32 getLRUVictim(UInt32 set_idx) const;

public:
    /**
     * @brief Construct a new UtopiaTLB
     * 
     * @param name Name for stats/logging
     * @param core_id Core ID this TLB belongs to
     * @param num_entries Total number of entries
     * @param associativity Number of ways per set
     * @param access_latency Lookup latency
     */
    UtopiaTLB(String name, core_id_t core_id, UInt32 num_entries, 
              UInt32 associativity, SubsecondTime access_latency);
    
    ~UtopiaTLB();
    
    /**
     * @brief Look up a virtual address in the UTLB
     * 
     * @param address Virtual address to look up
     * @param page_size_bits Page size in bits (12 for 4KB, 21 for 2MB)
     * @param count Whether to count this access in stats
     * @return UTLBLookupResult with hit/miss and RestSeg info
     */
    UTLBLookupResult lookup(IntPtr address, int page_size_bits, bool count = true);
    
    /**
     * @brief Allocate a new entry in the UTLB
     * 
     * @param address Virtual address
     * @param page_size_bits Page size in bits
     * @param seg_id RestSeg ID (0 or 1)
     * @param way_idx Way index in RestSeg data array
     * @param count Whether to count this in stats
     * @return True if an eviction occurred
     */
    bool allocate(IntPtr address, int page_size_bits, uint8_t seg_id, 
                  uint8_t way_idx, bool count = true);
    
    /**
     * @brief Invalidate an entry
     * 
     * @param address Virtual address to invalidate
     * @param page_size_bits Page size in bits
     * @return True if entry was found and invalidated
     */
    bool invalidate(IntPtr address, int page_size_bits);
    
    /**
     * @brief Flush all entries
     */
    void flush();
    
    // Getters
    String getName() const { return m_name; }
    SubsecondTime getLatency() const { return m_access_latency; }
    UInt32 getNumEntries() const { return m_num_entries; }
    UInt32 getAssociativity() const { return m_associativity; }
    UInt32 getNumSets() const { return m_num_sets; }
    
    /**
     * @brief Statistics structure for external access
     */
    struct Stats {
        UInt64 accesses;
        UInt64 hits;
        UInt64 misses;
        UInt64 evictions;
        UInt64 allocations;
    };
    
    /**
     * @brief Get statistics for this UTLB
     */
    Stats getStats() const {
        return Stats{m_stats.accesses, m_stats.hits, m_stats.misses, 
                     m_stats.evictions, m_stats.allocations};
    }
    
    /**
     * @brief Get the effective bits per entry for capacity analysis
     */
    int getBitsPerEntry() const {
        return UTLBEntry::getBitsPerEntry(m_num_sets, 2, 16);
    }
    
    /**
     * @brief Compare capacity with standard TLB
     * @param standard_tlb_bits_per_entry Bits per entry in standard TLB (typically ~96)
     * @return Capacity ratio (UTLB entries / standard entries for same SRAM)
     */
    float getCapacityRatio(int standard_tlb_bits_per_entry = 96) const {
        return static_cast<float>(standard_tlb_bits_per_entry) / getBitsPerEntry();
    }
};

} // namespace ParametricDramDirectoryMSI

#endif // UTOPIA_TLB_H
