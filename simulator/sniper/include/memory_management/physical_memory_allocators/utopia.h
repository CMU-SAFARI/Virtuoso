#pragma once

/*
 * ================================================================================
 * UTOPIA ALLOCATOR - Policy-Based Template Design
 * ================================================================================
 * 
 * Reference: "Utopia: Fast and Efficient Address Translation via Hybrid Restrictive
 *             & Flexible Virtual-to-Physical Address Mappings"
 *             Kanellopoulos et al., MICRO 2023
 *             https://arxiv.org/abs/2211.12205
 *             https://doi.org/10.1145/3613424.3623789
 *
 * OVERVIEW:
 * ---------
 * Utopia is a hardware-software co-designed physical memory allocator that enables
 * efficient address translation by providing predictable virtual-to-physical mappings
 * for large pages. It divides physical memory into two distinct regions:
 * 
 *   1. RestSegs (Restricted Segments):
 *      - Cache-like structures supporting configurable page sizes (4KB, 2MB, 1GB)
 *      - Set-associative addressing with configurable replacement (LRU)
 *      - Each RestSeg has fixed page size, associativity, and number of sets
 *      - Physical address is DIRECTLY COMPUTABLE from virtual address + way
 *      - Eliminates TLB misses for pages in RestSegs (direct translation)
 *      - Multiple RestSegs can coexist (e.g., one for 2MB, one for 4KB pages)
 * 
 *   2. FlexSeg (Flexible Segment):
 *      - Reserve-THP allocator supporting 2MB THP with 4KB fallback
 *      - Used when RestSegs are full or for standard allocations
 *      - Requires conventional page table walks for translation
 *      - Tracks 4KB sub-allocations within 2MB regions for THP promotion
 * 
 * ================================================================================
 * ALLOCATION HIERARCHY (4-Level Priority)
 * ================================================================================
 * 
 * The allocator implements a strict priority order to maximize both translation
 * speed and large page benefits. THP reservation takes precedence over RestSeg-4KB.
 * 
 *   ┌─────────────────────────────────────────────────────────────────────────┐
 *   │  LEVEL 1: RestSeg-2MB (BEST)                                            │
 *   │  ├── Benefits: Fast translation (NO page table walk) + 2MB large pages  │
 *   │  ├── Translation: PA = RestSeg_base + (set * assoc + way) * 2MB         │
 *   │  ├── Policy: No eviction on first-touch (force_evict=false)             │
 *   │  └── Fallback: If set is full (conflict), try L2                        │
 *   ├─────────────────────────────────────────────────────────────────────────┤
 *   │  LEVEL 2: FlexSeg-THP (2MB via Reserve-THP)                             │
 *   │  ├── Benefits: 2MB large pages (fewer TLB entries, better locality)     │
 *   │  ├── Translation: Conventional 4-level page table walk                  │
 *   │  ├── Returns: 2MB if promoted, 4KB otherwise (within 2MB reservation)   │
 *   │  ├── Policy: If 2MB region reserved, return immediately (even if 4KB)   │
 *   │  └── Fallback: If no 2MB region available from buddy, try L3            │
 *   ├─────────────────────────────────────────────────────────────────────────┤
 *   │  LEVEL 3: RestSeg-4KB                                                   │
 *   │  ├── Benefits: Fast translation (NO page table walk)                    │
 *   │  ├── Translation: PA = RestSeg_base + (set * assoc + way) * 4KB         │
 *   │  ├── Policy: No eviction on first-touch (force_evict=false)             │
 *   │  └── Fallback: If set is full (conflict), try L4                        │
 *   ├─────────────────────────────────────────────────────────────────────────┤
 *   │  LEVEL 4: FlexSeg-4KB (FALLBACK)                                        │
 *   │  ├── Benefits: Always available (buddy allocator)                       │
 *   │  ├── Translation: Conventional 4-level page table walk                  │
 *   │  └── Used when: All other options exhausted                             │
 *   └─────────────────────────────────────────────────────────────────────────┘
 * 
 * KEY DESIGN DECISIONS:
 * ---------------------
 *   1. No eviction on first-touch: RestSegs use force_evict=false, so a page
 *      is only placed in RestSeg if a free way is available. Eviction only
 *      happens during explicit migration (migratePage with force_evict=true).
 *   
 *   2. THP takes precedence: If L2 successfully reserves a 2MB region, we
 *      return immediately (even if returning a 4KB page within that region).
 *      This avoids the side-effect bug where tryAllocateTHP() would mutate
 *      state but then L3 would win the allocation.
 *   
 *   3. Migration at 4KB granularity: Pages migrate between FlexSeg and
 *      RestSeg at 4KB granularity only.
 * 
 * ALLOCATION FLOW:
 * ----------------
 *   allocate(VA, core_id)
 *     │
 *     ├──► [L1] Try RestSeg-2MB
 *     │      ├── Success? → Return PA (21-bit page, fast translation)
 *     │      └── Full?    → Continue to L2
 *     │
 *     ├──► [L2] Try FlexSeg-THP (Reserve-THP allocator)
 *     │      ├── Got 2MB? → Return PA (21-bit page, PTW required)
 *     │      └── No THP?  → Continue to L3
 *     │
 *     ├──► [L3] Try RestSeg-4KB
 *     │      ├── Success? → Return PA (12-bit page, fast translation)
 *     │      └── Full?    → Continue to L4
 *     │
 *     └──► [L4] FlexSeg-4KB fallback
 *            └── Return PA (12-bit page, PTW required)
 * 
 * KEY INSIGHT:
 * ------------
 * In RestSegs, the physical address can be computed directly from the virtual
 * address without accessing page tables:
 * 
 *   physical_addr = RestSeg_base + (set_index * associativity + way) * page_size
 * 
 * where:
 *   - set_index = (VA >> page_size_bits) % num_sets
 *   - way = looked up in per-core permission filter (small, fast structure)
 * 
 * This enables "TLB-less" translation for RestSeg pages, dramatically reducing
 * translation overhead for memory-intensive workloads.
 * 
 * CONFIGURATION EXAMPLE:
 * ----------------------
 *   [perf_model/utopia]
 *   RestSegs = 2
 *   thp_promotion_threshold = 75   # Promote to 2MB when 75% of region is used
 *   
 *   [perf_model/utopia/RestSeg]
 *   size[] = 4096, 1024      # MB per RestSeg
 *   page_size[] = 21, 12     # 2MB RestSeg and 4KB RestSeg
 *   assoc[] = 16, 32         # associativity
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

/*
 * ================================================================================
 * RestSegEntry - Single entry in the RestSeg set
 * ================================================================================
 * 
 * Each entry represents one physical page frame in the RestSeg. The entry stores:
 *   - Validity bit: whether this way contains a valid mapping
 *   - Tag: upper bits of virtual page number (for lookup verification)
 *   - Fingerprint: 4-bit XOR hash of VA for fast filtering before tag comparison
 *   - Owner: application ID that owns this page (0 = no owner, app_id+1 otherwise)
 *   - Reuse count: number of accesses since allocation (for profiling)
 *   - LRU counter: timestamp for LRU replacement policy
 * 
 * The physical address for this entry is implicitly determined by:
 *   physical_addr = RestSeg_base + (set_index * associativity + way_index) * page_size
 */

/**
 * @brief CRC-8 lookup table (polynomial 0x07, standard CRC-8)
 * 
 * This provides much better hash distribution than XOR folding.
 */
static const uint8_t crc8_table[256] = {
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
    0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65,
    0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5,
    0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85,
    0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2,
    0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2,
    0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32,
    0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42,
    0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C,
    0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC,
    0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C,
    0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C,
    0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B,
    0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B,
    0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB,
    0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB,
    0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3
};

/**
 * @brief Compute N-bit fingerprint from tag using CRC-8
 * @param tag Tag bits (not full VA - set index bits already used for indexing)
 * @param num_bits Number of bits in fingerprint (1-8, default 4)
 * @return N-bit fingerprint
 * 
 * Uses CRC-8 hash of tag bytes for better distribution than XOR folding.
 * The CRC-8 polynomial 0x07 provides good avalanche properties.
 * 
 * Note: We hash the TAG, not the full VA, because:
 * - Set index bits are already used for set selection
 * - Tag is what we compare in TAR, so fingerprint should summarize tag
 * - This avoids redundant information in the fingerprint
 */
inline uint8_t computeFingerprint(UInt64 tag, int num_bits = 4) {
    // Compute CRC-8 over the tag bytes (up to 8 bytes)
    uint8_t crc = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t byte = (tag >> (i * 8)) & 0xFF;
        if (byte == 0 && (tag >> (i * 8)) == 0) break;  // Stop at end of significant bytes
        crc = crc8_table[crc ^ byte];
    }
    
    // Mask to requested number of bits
    uint8_t mask = (1 << num_bits) - 1;
    return crc & mask;
}

struct RestSegEntry {
    bool valid = false;      ///< True if this entry contains a valid mapping
    UInt64 tag = 0;          ///< Tag bits from virtual address (VPN / num_sets)
    uint8_t fingerprint = 0; ///< N-bit CRC-8 fingerprint of VA for fast filtering
    UInt64 owner = 0;        ///< Owning app_id + 1 (0 means no owner/free)
    UInt64 reuse_count = 0;  ///< Number of accesses since allocation (profiling)
    UInt64 lru_counter = 0;  ///< LRU timestamp (higher = more recently used)
    
    /// Invalidate this entry, resetting all fields
    void invalidate() {
        valid = false;
        tag = 0;
        fingerprint = 0;
        owner = 0;
        reuse_count = 0;
    }
};

/*
 * ================================================================================
 * RadixWayTable - Per-App Radix Tree for VPN → {valid, way} Mapping
 * ================================================================================
 * 
 * This is a sparse radix tree that maps VPNs to RestSeg way indices for fast
 * "is this VPN in RestSeg?" lookups. It replaces the fingerprint + tag array
 * lookup path with a direct radix walk.
 * 
 * DESIGN:
 * -------
 *   - Variable-depth radix for 4KB pages based on leaf_entries
 *   - Each internal node has 512 child pointers (2^9)
 *   - Leaf nodes are exactly 4KB (one PT frame) storing packed {valid, way} entries
 *   - Number of entries per leaf = floor(32768 / (1 + log2(assoc)))
 *   - Sparse allocation: nodes allocated on demand via handle_page_table_allocations()
 * 
 * LEAF SIZING (key change from v1):
 * ---------------------------------
 *   - Leaf is exactly 4KB (4096 bytes = 32768 bits)
 *   - entry_bits = 1 (valid) + log2(assoc) (way)
 *   - leaf_entries = floor(32768 / entry_bits)
 *   - Examples:
 *     - assoc=8:  entry_bits=4, leaf_entries=8192 (covers 32MB VA)
 *     - assoc=16: entry_bits=5, leaf_entries=6553 (covers ~25.6MB VA)
 *     - assoc=32: entry_bits=6, leaf_entries=5461 (covers ~21.3MB VA)
 * 
 * INDEXING (key change from v1):
 * ------------------------------
 *   - leaf_index = vpn % leaf_entries (NOT bit-shift, leaf_entries not power-of-2)
 *   - vpn_above_leaf = vpn / leaf_entries
 *   - Upper levels use 9-bit chunks on vpn_above_leaf
 * 
 * CORRECTNESS:
 * ------------
 *   This table is CORRECTNESS-CRITICAL. It must be updated on:
 *   - RestSeg allocation: set_mapping(vpn, app_id, way)
 *   - RestSeg eviction: clear_mapping(vpn, app_id)
 *   - Migration: update mapping when page moves
 * 
 * LATENCY MODEL:
 * --------------
 *   - Each radix level = 1 cache line fetch (8-byte pointer)
 *   - Leaf access = 1 cache line fetch (64B containing the entry)
 *   - Radix nodes/leaves have simulated physical addresses for cache modeling
 */

// Forward declaration for Policy template
template <typename Policy> class RestSeg;

/**
 * @brief Aligned allocation helpers for radix nodes
 * 
 * RadixNode: 64-byte aligned (cache-line friendly)
 * RadixLeaf: 4KB aligned (PT-frame semantics)
 */
inline void* aligned_alloc_node(size_t size, size_t alignment) {
    void* ptr = nullptr;
#if defined(_WIN32)
    ptr = _aligned_malloc(size, alignment);
#else
    if (posix_memalign(&ptr, alignment, size) != 0) {
        ptr = nullptr;
    }
#endif
    return ptr;
}

inline void aligned_free_node(void* ptr) {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

/**
 * @brief Radix tree internal node (512 children)
 * 
 * Each internal node covers 9 bits of vpn_above_leaf.
 * Child pointers are allocated on demand (NEVER preallocated).
 * Has a simulated physical address for latency modeling.
 * 
 * Size: 512 * 8 = 4096 bytes for pointers + metadata = ~4KB total
 * Alignment: 64 bytes (cache-line aligned)
 */
struct alignas(64) RadixNode {
    static constexpr int RADIX_BITS = 9;           ///< Bits per radix level
    static constexpr int FANOUT = 1 << RADIX_BITS; ///< 512 children per node
    
    std::array<RadixNode*, FANOUT> children;       ///< Child pointers (nullptr = not allocated)
    bool is_leaf = false;                          ///< True if this is a leaf node
    IntPtr sim_phys_addr = 0;                      ///< Simulated physical address for latency
    
    RadixNode() : is_leaf(false), sim_phys_addr(0) {
        children.fill(nullptr);
    }
    
    // Custom aligned allocation
    static void* operator new(size_t size) {
        void* ptr = aligned_alloc_node(size, 64);  // 64-byte alignment
        if (!ptr) throw std::bad_alloc();
        return ptr;
    }
    
    static void operator delete(void* ptr) {
        aligned_free_node(ptr);
    }
    
    ~RadixNode() {
        if (!is_leaf) {
            for (auto* child : children) {
                delete child;
            }
        }
    }
};

/**
 * @brief Leaf node: exactly 4KB storing packed {valid, way} entries
 * 
 * Entry format: [valid:1][way:way_bits] packed contiguously
 * Number of entries = floor(32768 / (1 + way_bits))
 * 
 * IMPORTANT: leaf_entries is NOT necessarily a power of two!
 * Indexing uses modulo, not bit-shift.
 * 
 * Size: Exactly 4096 bytes (PT-frame sized)
 * Alignment: 4096 bytes (page-aligned, PT-frame semantics)
 */
class alignas(4096) RadixLeaf : public RadixNode {
public:
    static constexpr int LEAF_BYTES = 4096;        ///< Leaf is one 4KB page
    static constexpr int LEAF_BITS = LEAF_BYTES * 8;  ///< 32768 bits
    static constexpr int CACHE_LINE_BYTES = 64;    ///< Cache line size
    
private:
    int way_bits;                                  ///< log2(associativity)
    int bits_per_entry;                            ///< 1 + way_bits
    int leaf_entries;                              ///< floor(LEAF_BITS / bits_per_entry)
    std::array<uint8_t, LEAF_BYTES> data;          ///< Fixed 4KB storage
    
public:
    /**
     * @brief Get raw byte from internal storage (for testing/debugging)
     */
    uint8_t getDataByte(int byte_idx) const {
        if (byte_idx < 0 || byte_idx >= LEAF_BYTES) return 0;
        return data[byte_idx];
    }
    
    /**
     * @brief Construct a 4KB leaf node
     * @param _way_bits Number of bits to store way index (log2(assoc))
     */
    explicit RadixLeaf(int _way_bits) 
        : way_bits(_way_bits), bits_per_entry(1 + _way_bits) 
    {
        is_leaf = true;
        leaf_entries = LEAF_BITS / bits_per_entry;
        data.fill(0);
    }
    
    /**
     * @brief Get number of entries this leaf can hold
     */
    int getLeafEntries() const { return leaf_entries; }
    
    /**
     * @brief Get entry at index using safe bit extraction
     * @param index Entry index (0 to leaf_entries-1)
     * @param[out] valid Whether entry is valid
     * @param[out] way Way index (valid only if valid=true)
     */
    void getEntry(int index, bool& valid, int& way) const {
        if (index < 0 || index >= leaf_entries) {
            valid = false;
            way = -1;
            return;
        }
        
        // Calculate bit position in the 4KB array
        int bit_pos = index * bits_per_entry;
        int byte_pos = bit_pos / 8;
        int bit_offset = bit_pos % 8;
        
        // Extract bits - may span up to 2 bytes for entries up to 8 bits
        // (way_bits max ~7 for 128-way, so entry max 8 bits)
        uint32_t raw = 0;
        raw = data[byte_pos];
        if (byte_pos + 1 < LEAF_BYTES) {
            raw |= static_cast<uint32_t>(data[byte_pos + 1]) << 8;
        }
        if (byte_pos + 2 < LEAF_BYTES && bit_offset + bits_per_entry > 16) {
            raw |= static_cast<uint32_t>(data[byte_pos + 2]) << 16;
        }
        raw >>= bit_offset;
        
        // Mask to entry size
        uint32_t mask = (1u << bits_per_entry) - 1;
        uint32_t entry = raw & mask;
        
        // Entry format: [valid:1][way:way_bits]
        // valid is the MSB (bit at position way_bits)
        valid = (entry >> way_bits) & 1;
        way = entry & ((1 << way_bits) - 1);
    }
    
    /**
     * @brief Set entry at index using safe bit insertion
     * @param index Entry index (0 to leaf_entries-1)
     * @param valid Whether entry is valid
     * @param way Way index
     */
    void setEntry(int index, bool valid, int way) {
        if (index < 0 || index >= leaf_entries) return;
        
        // Build packed entry: [valid:1][way:way_bits]
        uint32_t entry = (way & ((1 << way_bits) - 1)) | (valid ? (1u << way_bits) : 0);
        
        // Calculate bit position
        int bit_pos = index * bits_per_entry;
        int byte_pos = bit_pos / 8;
        int bit_offset = bit_pos % 8;
        
        // Create masks
        uint32_t mask = (1u << bits_per_entry) - 1;
        uint32_t clear_mask = ~(mask << bit_offset);
        uint32_t set_mask = (entry & mask) << bit_offset;
        
        // Clear and set bits (handle up to 3 bytes)
        data[byte_pos] = (data[byte_pos] & (clear_mask & 0xFF)) | (set_mask & 0xFF);
        if (byte_pos + 1 < LEAF_BYTES) {
            data[byte_pos + 1] = (data[byte_pos + 1] & ((clear_mask >> 8) & 0xFF)) | ((set_mask >> 8) & 0xFF);
        }
        if (byte_pos + 2 < LEAF_BYTES && bit_offset + bits_per_entry > 16) {
            data[byte_pos + 2] = (data[byte_pos + 2] & ((clear_mask >> 16) & 0xFF)) | ((set_mask >> 16) & 0xFF);
        }
    }
    
    /**
     * @brief Clear entry at index (mark invalid)
     */
    void clearEntry(int index) {
        setEntry(index, false, 0);
    }
    
    /**
     * @brief Get cache line index for a given entry (for latency modeling)
     * @param index Entry index
     * @return Byte offset of the cache line containing this entry
     */
    int getCacheLineOffset(int index) const {
        int bit_pos = index * bits_per_entry;
        int byte_pos = bit_pos / 8;
        return (byte_pos / CACHE_LINE_BYTES) * CACHE_LINE_BYTES;
    }
    
    /**
     * @brief Self-test: verify bit extraction/insertion round-trips correctly
     * 
     * Tests:
     * 1. Basic round-trip for various indices
     * 2. Same-byte sharing (two entries in one byte)
     * 3. Cross-byte straddling (entry spans two bytes)
     * 4. Coexistence: multiple entries don't corrupt each other
     * 
     * @return true if all tests pass
     */
    bool selfTest() const {
        RadixLeaf test_leaf(way_bits);
        int max_way = (1 << way_bits) - 1;
        
        // Test a few entries at different positions
        std::vector<int> test_indices = {0, 1, leaf_entries/2, leaf_entries-2, leaf_entries-1};
        for (int idx : test_indices) {
            if (idx >= leaf_entries) continue;
            
            // Test valid=true with various ways
            for (int w : {0, 1, max_way/2, max_way}) {
                test_leaf.setEntry(idx, true, w);
                bool v_out;
                int w_out;
                test_leaf.getEntry(idx, v_out, w_out);
                if (!v_out || w_out != w) return false;
            }
            
            // Test valid=false
            test_leaf.setEntry(idx, false, 0);
            bool v_out;
            int w_out;
            test_leaf.getEntry(idx, v_out, w_out);
            if (v_out) return false;
        }
        return true;
    }
    
    /**
     * @brief Comprehensive self-test with detailed output
     * 
     * Call this once at startup to validate bit-packing correctness.
     * Prints results to stderr for inspection.
     */
    static void runDetailedSelfTest() {
        fprintf(stderr, "\n=== RadixLeaf Bit-Packing Self-Test ===\n\n");
        
        // ================================================================
        // TEST 1: Same-byte sharing for assoc=8 (bits_per_entry=4)
        // Two entries fit in one byte: idx=0 at bits[3:0], idx=1 at bits[7:4]
        // ================================================================
        {
            fprintf(stderr, "TEST 1: Same-byte sharing (assoc=8, bits_per_entry=4)\n");
            RadixLeaf leaf(3);  // way_bits=3 → bits_per_entry=4
            
            // Entry format: [valid:1][way:3] = 4 bits
            // idx=0: bit_pos=0, byte_pos=0, bit_offset=0 → bits [3:0]
            // idx=1: bit_pos=4, byte_pos=0, bit_offset=4 → bits [7:4]
            
            leaf.setEntry(0, true, 3);   // valid=1, way=3 → entry=0b1011 (0xB)
            leaf.setEntry(1, true, 5);   // valid=1, way=5 → entry=0b1101 (0xD)
            
            // Expected byte: [idx1:4bits][idx0:4bits] = 0xDB
            //   idx0 at bits[3:0] = 1011 (valid=1, way=3)
            //   idx1 at bits[7:4] = 1101 (valid=1, way=5)
            
            bool v0, v1;
            int w0, w1;
            leaf.getEntry(0, v0, w0);
            leaf.getEntry(1, v1, w1);
            
            // Access internal data for verification
            uint8_t byte0 = leaf.getDataByte(0);
            
            fprintf(stderr, "  setEntry(0, valid=1, way=3)\n");
            fprintf(stderr, "  setEntry(1, valid=1, way=5)\n");
            fprintf(stderr, "  getEntry(0) → valid=%d, way=%d (expected: valid=1, way=3)\n", v0, w0);
            fprintf(stderr, "  getEntry(1) → valid=%d, way=%d (expected: valid=1, way=5)\n", v1, w1);
            fprintf(stderr, "  data[0] = 0x%02X (binary: ", byte0);
            for (int b = 7; b >= 0; b--) fprintf(stderr, "%d", (byte0 >> b) & 1);
            fprintf(stderr, ")\n");
            fprintf(stderr, "  Expected: 0xDB (binary: 11011011)\n");
            fprintf(stderr, "  RESULT: %s\n\n", 
                    (v0 && w0 == 3 && v1 && w1 == 5 && byte0 == 0xDB) ? "PASS" : "FAIL");
        }
        
        // ================================================================
        // TEST 2: Cross-byte straddling for assoc=32 (bits_per_entry=6)
        // Find index where entry straddles two bytes
        // ================================================================
        {
            fprintf(stderr, "TEST 2: Cross-byte straddling (assoc=32, bits_per_entry=6)\n");
            RadixLeaf leaf(5);  // way_bits=5 → bits_per_entry=6
            
            // Entry at idx=1: bit_pos=6, byte_pos=0, bit_offset=6
            // Entry spans: byte[0] bits [7:6] (2 bits) + byte[1] bits [3:0] (4 bits)
            
            // valid=1, way=17 → entry = (1 << 5) | 17 = 32 + 17 = 49 = 0b110001
            leaf.setEntry(1, true, 17);
            
            bool v_out;
            int w_out;
            leaf.getEntry(1, v_out, w_out);
            
            uint8_t byte0 = leaf.getDataByte(0);
            uint8_t byte1 = leaf.getDataByte(1);
            
            fprintf(stderr, "  Entry 1: bit_pos=6, byte_pos=0, bit_offset=6\n");
            fprintf(stderr, "  Entry straddles: byte[0] bits[7:6] and byte[1] bits[3:0]\n");
            fprintf(stderr, "  setEntry(1, valid=1, way=17) → packed entry = 0b110001 (0x31)\n");
            fprintf(stderr, "  getEntry(1) → valid=%d, way=%d (expected: valid=1, way=17)\n", v_out, w_out);
            fprintf(stderr, "  data[0] = 0x%02X (bits[7:6] should contain entry bits[1:0] = 01)\n", byte0);
            fprintf(stderr, "  data[1] = 0x%02X (bits[3:0] should contain entry bits[5:2] = 1100)\n", byte1);
            fprintf(stderr, "  RESULT: %s\n\n", (v_out && w_out == 17) ? "PASS" : "FAIL");
        }
        
        // ================================================================
        // TEST 3: Coexistence - ensure adjacent entries don't corrupt
        // ================================================================
        {
            fprintf(stderr, "TEST 3: Coexistence test (assoc=16, bits_per_entry=5)\n");
            RadixLeaf leaf(4);  // way_bits=4 → bits_per_entry=5
            
            // Fill several consecutive entries and verify all survive
            std::vector<std::pair<int, int>> entries = {
                {0, 7}, {1, 11}, {2, 3}, {3, 15}, {4, 1}
            };
            
            for (const auto& e : entries) {
                leaf.setEntry(e.first, true, e.second);
            }
            
            bool all_pass = true;
            for (const auto& e : entries) {
                bool v;
                int w;
                leaf.getEntry(e.first, v, w);
                bool pass = (v && w == e.second);
                fprintf(stderr, "  getEntry(%d) → valid=%d, way=%d (expected: way=%d) %s\n",
                        e.first, v, w, e.second, pass ? "OK" : "FAIL");
                if (!pass) all_pass = false;
            }
            fprintf(stderr, "  RESULT: %s\n\n", all_pass ? "PASS" : "FAIL");
        }
        
        // ================================================================
        // TEST 4: Edge case - entries near byte boundaries
        // ================================================================
        {
            fprintf(stderr, "TEST 4: Edge cases near byte boundaries\n");
            bool all_pass = true;
            
            // Test various associativities
            for (int way_bits_test : {2, 3, 4, 5, 6}) {
                RadixLeaf leaf(way_bits_test);
                int bpe = 1 + way_bits_test;
                int max_way = (1 << way_bits_test) - 1;
                
                // Find entries that straddle bytes
                for (int idx = 0; idx < std::min(20, leaf.getLeafEntries()); idx++) {
                    int bit_pos = idx * bpe;
                    int bit_offset = bit_pos % 8;
                    
                    // This entry straddles if bit_offset + bpe > 8
                    if (bit_offset + bpe > 8) {
                        leaf.setEntry(idx, true, max_way);
                        bool v;
                        int w;
                        leaf.getEntry(idx, v, w);
                        if (!v || w != max_way) {
                            fprintf(stderr, "  FAIL: way_bits=%d, idx=%d (straddles at bit_offset=%d)\n",
                                    way_bits_test, idx, bit_offset);
                            all_pass = false;
                        }
                    }
                }
            }
            fprintf(stderr, "  RESULT: %s\n\n", all_pass ? "PASS" : "FAIL");
        }
        
        fprintf(stderr, "=== End of RadixLeaf Self-Test ===\n\n");
    }
    
    // Custom 4KB-aligned allocation for PT-frame semantics
    static void* operator new(size_t size) {
        void* ptr = aligned_alloc_node(size, 4096);  // 4KB alignment
        if (!ptr) throw std::bad_alloc();
        return ptr;
    }
    
    static void operator delete(void* ptr) {
        aligned_free_node(ptr);
    }
};

/**
 * @brief Per-application radix table for VPN → {valid, way} mapping
 * 
 * Key design: leaf_entries = floor(32768 / (1 + log2(assoc)))
 * Indexing: leaf_index = vpn % leaf_entries, vpn_above_leaf = vpn / leaf_entries
 * Upper levels use 9-bit chunks on vpn_above_leaf.
 */
class RadixWayTable {
public:
    static constexpr int RADIX_BITS = 9;           ///< Bits per internal level
    static constexpr int MAX_LEVELS = 4;           ///< Max internal levels
    static constexpr int LEAF_BITS = 32768;        ///< 4KB leaf
    
private:
    RadixNode* root = nullptr;                     ///< Root node (may be nullptr initially)
    int num_internal_levels;                       ///< Number of internal node levels (before leaf)
    int way_bits;                                  ///< log2(associativity)
    int leaf_entries;                              ///< Entries per leaf = floor(32768 / (1+way_bits))
    int page_size_bits;                            ///< Page size (12 for 4KB)
    mutable std::mutex table_lock;                 ///< Thread safety
    
    // Statistics
    mutable UInt64 lookups = 0;
    mutable UInt64 lookup_hits = 0;
    mutable UInt64 lookup_misses = 0;
    UInt64 insertions = 0;
    UInt64 deletions = 0;
    UInt64 nodes_allocated = 0;
    UInt64 leaves_allocated = 0;
    
    // For physical address allocation (latency modeling)
    std::function<IntPtr(size_t)> alloc_phys_page;  ///< Allocator callback
    
public:
    /**
     * @brief Construct a radix way table with 4KB leaves
     * @param _page_size_bits Page size (12 for 4KB, 21 for 2MB)
     * @param associativity RestSeg associativity (to compute way_bits and leaf_entries)
     * @param _alloc_phys_page Callback to allocate simulated physical pages
     */
    RadixWayTable(int _page_size_bits, int associativity, 
                  std::function<IntPtr(size_t)> _alloc_phys_page = nullptr)
        : page_size_bits(_page_size_bits), alloc_phys_page(_alloc_phys_page)
    {
        // Calculate way bits from associativity
        way_bits = 0;
        int temp = associativity;
        while (temp > 1) {
            way_bits++;
            temp >>= 1;
        }
        if (way_bits == 0) way_bits = 1;  // Minimum 1 bit for 1-way
        
        // Calculate leaf entries: floor(32768 / (1 + way_bits))
        int bits_per_entry = 1 + way_bits;
        leaf_entries = LEAF_BITS / bits_per_entry;
        
        // Calculate number of internal levels needed
        // VPN space for 4KB pages with 48-bit VA: 2^36 VPNs
        // Each leaf covers leaf_entries VPNs
        // So we need to index vpn_above_leaf which has up to 2^36 / leaf_entries values
        // We use 9-bit levels on vpn_above_leaf
        UInt64 max_vpn = 1ULL << (48 - page_size_bits);
        UInt64 max_vpn_above_leaf = (max_vpn + leaf_entries - 1) / leaf_entries;
        
        // Calculate bits needed for vpn_above_leaf
        int bits_needed = 0;
        UInt64 temp64 = max_vpn_above_leaf;
        while (temp64 > 0) {
            bits_needed++;
            temp64 >>= 1;
        }
        
        // Number of internal levels (9 bits each)
        num_internal_levels = (bits_needed + RADIX_BITS - 1) / RADIX_BITS;
        if (num_internal_levels > MAX_LEVELS) num_internal_levels = MAX_LEVELS;
        if (num_internal_levels < 1) num_internal_levels = 1;
        
        // Print configuration once
        static bool printed_config[3] = {false, false, false};
        int config_idx = (way_bits <= 3) ? 0 : (way_bits <= 4 ? 1 : 2);
        if (!printed_config[config_idx]) {
            printed_config[config_idx] = true;
            fprintf(stderr, "[RadixWayTable] assoc=%d way_bits=%d entry_bits=%d leaf_entries=%d "
                    "leaf_va_span=%lluMB internal_levels=%d\n",
                    associativity, way_bits, bits_per_entry, leaf_entries,
                    (unsigned long long)(leaf_entries * (1ULL << page_size_bits)) / (1024*1024),
                    num_internal_levels);
            
            // Run detailed self-test once
            static bool ran_selftest = false;
            if (!ran_selftest) {
                ran_selftest = true;
                RadixLeaf::runDetailedSelfTest();
            }
        }
    }
    
    ~RadixWayTable() {
        delete root;
    }
    
    // Prevent copying
    RadixWayTable(const RadixWayTable&) = delete;
    RadixWayTable& operator=(const RadixWayTable&) = delete;
    
    /**
     * @brief Get leaf_entries for this table
     */
    int getLeafEntries() const { return leaf_entries; }
    
    /**
     * @brief Compute indexing for a VPN
     * @param vpn Virtual page number (address >> page_size_bits)
     * @param[out] leaf_index Index within the leaf (0 to leaf_entries-1)
     * @param[out] vpn_above_leaf The VPN bits above the leaf (for internal node indexing)
     */
    void computeIndices(UInt64 vpn, int& leaf_index, UInt64& vpn_above_leaf) const {
        leaf_index = vpn % leaf_entries;
        vpn_above_leaf = vpn / leaf_entries;
    }
    
    /**
     * @brief Get index for a specific internal level
     * @param vpn_above_leaf Value from computeIndices
     * @param level Internal level (0 = root, num_internal_levels-1 = before leaf)
     * @return 9-bit index for this level
     */
    int getLevelIndex(UInt64 vpn_above_leaf, int level) const {
        int shift = (num_internal_levels - 1 - level) * RADIX_BITS;
        return (vpn_above_leaf >> shift) & ((1 << RADIX_BITS) - 1);
    }
    
    /**
     * @brief Lookup way for a VPN
     * @param vpn Virtual page number (address >> page_size_bits)
     * @param[out] way_out Way index if found
     * @return true if VPN is valid in table, false otherwise
     */
    bool lookup_way(UInt64 vpn, int& way_out) const {
        std::lock_guard<std::mutex> guard(table_lock);
        lookups++;
        
        way_out = -1;
        
        if (!root) {
            lookup_misses++;
            return false;
        }
        
        // Compute leaf_index and vpn_above_leaf
        int leaf_index;
        UInt64 vpn_above_leaf;
        computeIndices(vpn, leaf_index, vpn_above_leaf);
        
        RadixNode* node = root;
        
        // Walk internal nodes
        for (int level = 0; level < num_internal_levels; level++) {
            int index = getLevelIndex(vpn_above_leaf, level);
            
            node = node->children[index];
            if (!node) {
                lookup_misses++;
                return false;
            }
        }
        
        // At leaf level
        if (!node->is_leaf) {
            lookup_misses++;
            return false;
        }
        
        RadixLeaf* leaf = static_cast<RadixLeaf*>(node);
        
        // Sanity check: leaf_index must be in range
        assert(leaf_index >= 0 && leaf_index < leaf->getLeafEntries());
        
        bool valid;
        leaf->getEntry(leaf_index, valid, way_out);
        
        if (valid) {
            lookup_hits++;
            return true;
        } else {
            lookup_misses++;
            return false;
        }
    }
    
    /**
     * @brief Set mapping for a VPN
     * @param vpn Virtual page number (address >> page_size_bits)
     * @param way Way index in RestSeg
     */
    void set_mapping(UInt64 vpn, int way) {
        std::lock_guard<std::mutex> guard(table_lock);
        insertions++;
        
        // Compute leaf_index and vpn_above_leaf
        int leaf_index;
        UInt64 vpn_above_leaf;
        computeIndices(vpn, leaf_index, vpn_above_leaf);
        
        // Create root if needed
        if (!root) {
            root = new RadixNode();
            if (alloc_phys_page) {
                root->sim_phys_addr = alloc_phys_page(4096);  // Allocate 4KB for node
            }
            nodes_allocated++;
        }
        
        RadixNode* node = root;
        
        // Walk/create internal nodes
        for (int level = 0; level < num_internal_levels; level++) {
            int index = getLevelIndex(vpn_above_leaf, level);
            
            if (!node->children[index]) {
                if (level == num_internal_levels - 1) {
                    // Next level is leaf
                    RadixLeaf* new_leaf = new RadixLeaf(way_bits);
                    if (alloc_phys_page) {
                        new_leaf->sim_phys_addr = alloc_phys_page(4096);  // 4KB leaf
                    }
                    node->children[index] = new_leaf;
                    leaves_allocated++;
                    
                    // Run self-test on first leaf (debug)
                    static bool tested = false;
                    if (!tested) {
                        tested = true;
                        assert(new_leaf->selfTest() && "RadixLeaf self-test failed!");
                    }
                } else {
                    RadixNode* new_node = new RadixNode();
                    if (alloc_phys_page) {
                        new_node->sim_phys_addr = alloc_phys_page(4096);
                    }
                    node->children[index] = new_node;
                    nodes_allocated++;
                }
            }
            node = node->children[index];
        }
        
        // At leaf level
        RadixLeaf* leaf = static_cast<RadixLeaf*>(node);
        assert(leaf_index >= 0 && leaf_index < leaf->getLeafEntries());
        leaf->setEntry(leaf_index, true, way);
    }
    
    /**
     * @brief Clear mapping for a VPN (mark invalid)
     * @param vpn Virtual page number (address >> page_size_bits)
     */
    void clear_mapping(UInt64 vpn) {
        std::lock_guard<std::mutex> guard(table_lock);
        deletions++;
        
        if (!root) return;
        
        // Compute leaf_index and vpn_above_leaf
        int leaf_index;
        UInt64 vpn_above_leaf;
        computeIndices(vpn, leaf_index, vpn_above_leaf);
        
        RadixNode* node = root;
        
        // Walk to leaf
        for (int level = 0; level < num_internal_levels; level++) {
            int index = getLevelIndex(vpn_above_leaf, level);
            
            node = node->children[index];
            if (!node) return;  // Not present
        }
        
        // At leaf level
        if (!node->is_leaf) return;
        
        RadixLeaf* leaf = static_cast<RadixLeaf*>(node);
        if (leaf_index >= 0 && leaf_index < leaf->getLeafEntries()) {
            leaf->clearEntry(leaf_index);
        }
    }
    
    /**
     * @brief Get number of internal levels (plus 1 for leaf = total levels)
     */
    int getNumLevels() const { return num_internal_levels + 1; }
    
    /**
     * @brief Get number of internal levels only
     */
    int getNumInternalLevels() const { return num_internal_levels; }
    
    /**
     * @brief Get the simulated physical addresses for each level traversal
     * @param vpn Virtual page number
     * @param[out] addresses Vector of (level, phys_addr, is_leaf) tuples
     * 
     * For latency modeling: each address represents a cache line fetch.
     */
    void getTraversalAddresses(UInt64 vpn, 
                               std::vector<std::tuple<int, IntPtr, bool>>& addresses) const {
        std::lock_guard<std::mutex> guard(table_lock);
        addresses.clear();
        
        if (!root) return;
        
        int leaf_index;
        UInt64 vpn_above_leaf;
        computeIndices(vpn, leaf_index, vpn_above_leaf);
        
        RadixNode* node = root;
        
        // Internal levels
        for (int level = 0; level < num_internal_levels; level++) {
            int index = getLevelIndex(vpn_above_leaf, level);
            
            // Address of the pointer we're reading (8 bytes, within a cache line)
            IntPtr ptr_addr = node->sim_phys_addr + index * 8;
            IntPtr cache_line_addr = ptr_addr & ~63ULL;  // Align to 64B
            addresses.push_back(std::make_tuple(level, cache_line_addr, false));
            
            node = node->children[index];
            if (!node) break;
        }
        
        // Leaf level
        if (node && node->is_leaf) {
            RadixLeaf* leaf = static_cast<RadixLeaf*>(node);
            int cache_line_offset = leaf->getCacheLineOffset(leaf_index);
            IntPtr leaf_addr = leaf->sim_phys_addr + cache_line_offset;
            addresses.push_back(std::make_tuple(num_internal_levels, leaf_addr, true));
        }
    }
    
    /**
     * @brief Verify that two VPNs with same vpn_above_leaf but different leaf_index
     *        land in the same leaf
     */
    bool verifySameLeaf(UInt64 vpn1, UInt64 vpn2) const {
        int leaf_index1, leaf_index2;
        UInt64 vpn_above1, vpn_above2;
        computeIndices(vpn1, leaf_index1, vpn_above1);
        computeIndices(vpn2, leaf_index2, vpn_above2);
        return (vpn_above1 == vpn_above2);
    }
    
    /**
     * @brief Verify that adding leaf_entries to VPN changes the leaf
     */
    bool verifyDifferentLeaf(UInt64 vpn) const {
        int leaf_index1, leaf_index2;
        UInt64 vpn_above1, vpn_above2;
        computeIndices(vpn, leaf_index1, vpn_above1);
        computeIndices(vpn + leaf_entries, leaf_index2, vpn_above2);
        return (vpn_above1 != vpn_above2);
    }
    
    // Statistics accessors
    UInt64 getLookups() const { return lookups; }
    UInt64 getLookupHits() const { return lookup_hits; }
    UInt64 getLookupMisses() const { return lookup_misses; }
    UInt64 getInsertions() const { return insertions; }
    UInt64 getDeletions() const { return deletions; }
    UInt64 getNodesAllocated() const { return nodes_allocated; }
    UInt64 getLeavesAllocated() const { return leaves_allocated; }
    int getWayBits() const { return way_bits; }
};

/**
 * @brief Manager for per-app radix tables within a RestSeg
 * 
 * Each RestSeg has one RadixWayTableManager that creates and manages
 * per-app RadixWayTable instances on demand.
 */
class RadixWayTableManager {
private:
    std::unordered_map<int, std::unique_ptr<RadixWayTable>> app_tables;
    int page_size_bits;
    int associativity;
    int leaf_entries;  // Cached for quick access
    mutable std::mutex manager_lock;
    
    // Allocator for radix node physical addresses
    std::function<IntPtr(size_t)> alloc_phys_page;
    
public:
    RadixWayTableManager(int _page_size_bits, int _associativity,
                         std::function<IntPtr(size_t)> _alloc_phys_page = nullptr)
        : page_size_bits(_page_size_bits), associativity(_associativity),
          alloc_phys_page(_alloc_phys_page)
    {
        // Compute leaf_entries for caching
        int way_bits = 0;
        int temp = _associativity;
        while (temp > 1) { way_bits++; temp >>= 1; }
        if (way_bits == 0) way_bits = 1;
        leaf_entries = RadixWayTable::LEAF_BITS / (1 + way_bits);
    }
    
    /**
     * @brief Get or create radix table for an app
     * @param app_id Application ID
     * @return Pointer to the app's radix table
     */
    RadixWayTable* getTable(int app_id) {
        std::lock_guard<std::mutex> guard(manager_lock);
        
        auto it = app_tables.find(app_id);
        if (it != app_tables.end()) {
            return it->second.get();
        }
        
        // Create new table for this app
        auto table = std::make_unique<RadixWayTable>(page_size_bits, associativity, alloc_phys_page);
        RadixWayTable* ptr = table.get();
        app_tables[app_id] = std::move(table);
        return ptr;
    }
    
    /**
     * @brief Check if table exists for an app (without creating)
     */
    bool hasTable(int app_id) const {
        std::lock_guard<std::mutex> guard(manager_lock);
        return app_tables.find(app_id) != app_tables.end();
    }
    
    /**
     * @brief Lookup way across all app tables (for debugging)
     * @param vpn Virtual page number
     * @param app_id Application ID
     * @param[out] way_out Way index if found
     * @return true if found
     */
    bool lookup_way(UInt64 vpn, int app_id, int& way_out) {
        RadixWayTable* table = getTable(app_id);
        return table->lookup_way(vpn, way_out);
    }
    
    /**
     * @brief Set mapping in app's table
     */
    void set_mapping(UInt64 vpn, int app_id, int way) {
        RadixWayTable* table = getTable(app_id);
        table->set_mapping(vpn, way);
    }
    
    /**
     * @brief Clear mapping in app's table
     */
    void clear_mapping(UInt64 vpn, int app_id) {
        if (!hasTable(app_id)) return;
        RadixWayTable* table = getTable(app_id);
        table->clear_mapping(vpn);
    }
    
    /**
     * @brief Get number of radix levels (internal + leaf)
     */
    int getNumLevels() const {
        // Each leaf covers leaf_entries VPNs
        // vpn_above_leaf needs ceil(log2(max_vpn / leaf_entries)) bits
        // which are split into 9-bit chunks
        UInt64 max_vpn = 1ULL << (48 - page_size_bits);
        UInt64 max_vpn_above_leaf = (max_vpn + leaf_entries - 1) / leaf_entries;
        int bits_needed = 0;
        UInt64 temp = max_vpn_above_leaf;
        while (temp > 0) { bits_needed++; temp >>= 1; }
        int internal_levels = (bits_needed + RadixWayTable::RADIX_BITS - 1) / RadixWayTable::RADIX_BITS;
        if (internal_levels > RadixWayTable::MAX_LEVELS) internal_levels = RadixWayTable::MAX_LEVELS;
        if (internal_levels < 1) internal_levels = 1;
        return internal_levels + 1;  // +1 for leaf
    }
    
    /**
     * @brief Get leaf_entries for this configuration
     */
    int getLeafEntries() const { return leaf_entries; }
    
    /**
     * @brief Get traversal addresses for latency modeling
     */
    void getTraversalAddresses(UInt64 vpn, int app_id,
                               std::vector<std::tuple<int, IntPtr, bool>>& addresses) {
        RadixWayTable* table = getTable(app_id);
        table->getTraversalAddresses(vpn, addresses);
    }
};

/*
 * ================================================================================
 * RestSegSet - A single set in the RestSeg (contains 'assoc' entries)
 * ================================================================================
 * 
 * A RestSegSet is analogous to a cache set. It contains 'associativity' ways,
 * each of which can hold one page mapping. The set uses LRU replacement when
 * all ways are full.
 * 
 * Set Indexing (from paper Section 4.1):
 *   set_index = (VPN >> log2(page_size)) % num_sets
 * 
 * This means pages that differ only in their lower VPN bits will map to the
 * same set, creating potential conflicts. The associativity determines how
 * many such conflicting pages can coexist before eviction is required.
 * 
 * Physical Address Calculation:
 *   Given a (set_index, way) pair, the physical address is:
 *   phys_addr = RestSeg_base + (set_index * associativity + way) * page_size
 * 
 * This direct calculation is the key to Utopia's fast translation - no page
 * table walk is needed for RestSeg pages.
 */
class RestSegSet {
private:
    std::vector<RestSegEntry> entries;  ///< The ways in this set
    UInt32 associativity;               ///< Number of ways
    UInt64 global_lru_counter = 0;      ///< Monotonic counter for LRU tracking

public:
    /**
     * @brief Construct a RestSegSet with given associativity
     * @param assoc Number of ways in this set
     */
    RestSegSet(UInt32 assoc) : associativity(assoc) {
        entries.resize(assoc);
    }
    
    UInt32 getAssociativity() const { return associativity; }
    
    /**
     * @brief Get entry at specified way (mutable)
     * @param way Way index (0 to associativity-1)
     * @return Pointer to entry, or nullptr if way is out of bounds
     */
    RestSegEntry* getEntry(UInt32 way) {
        return (way < associativity) ? &entries[way] : nullptr;
    }
    
    const RestSegEntry* getEntry(UInt32 way) const {
        return (way < associativity) ? &entries[way] : nullptr;
    }
    
    /**
     * @brief Find entry matching both tag and owner
     * @param tag Tag bits from virtual address
     * @param owner Owner ID (app_id + 1)
     * @return Way index if found, -1 otherwise
     * 
     * This is the primary lookup used during address translation.
     * Both tag AND owner must match for a hit.
     */
    int findEntry(UInt64 tag, UInt64 owner) const {
        for (UInt32 i = 0; i < associativity; i++) {
            if (entries[i].valid && entries[i].tag == tag && entries[i].owner == owner) {
                return i;
            }
        }
        return -1;
    }
    
    /**
     * @brief Find entry and collect shadow fingerprint statistics
     * @param tag Tag bits from virtual address
     * @param owner Owner ID (app_id + 1)
     * @param fingerprint 4-bit fingerprint computed from VA
     * @param[out] fp_checks Number of fingerprint comparisons made
     * @param[out] fp_matches Number of fingerprint matches
     * @param[out] fp_false_positives Fingerprint matched but tag didn't
     * @param[out] fp_true_positives Fingerprint matched and tag matched (actual hit)
     * @return Way index if found, -1 otherwise
     * 
     * Uses SHADOW fingerprint comparison: the actual lookup still uses
     * full tag comparison, but we track what would happen if we used
     * fingerprints as a filter to evaluate their effectiveness.
     */
    int findEntryWithFingerprintShadow(UInt64 tag, UInt64 owner, uint8_t fingerprint,
                                        UInt64& fp_checks, UInt64& fp_matches, 
                                        UInt64& fp_false_positives, UInt64& fp_true_positives) const {
        int result = -1;
        
        for (UInt32 i = 0; i < associativity; i++) {
            if (!entries[i].valid) continue;
            
            fp_checks++;
            
            // Shadow fingerprint comparison (for statistics only)
            bool fp_match = (entries[i].fingerprint == fingerprint);
            bool tag_owner_match = (entries[i].tag == tag && entries[i].owner == owner);
            
            if (fp_match) {
                fp_matches++;
                if (tag_owner_match) {
                    fp_true_positives++;  // Fingerprint correctly identified a hit
                } else {
                    fp_false_positives++;  // Fingerprint matched but not the right entry
                }
            }
            // Note: if fp doesn't match but tag_owner matches, that's a fingerprint miss
            // (would have incorrectly filtered out a valid entry - but this shouldn't happen
            // if fingerprints are computed correctly)
            
            // Actual lookup still uses full tag+owner comparison
            if (tag_owner_match) {
                result = i;
                // Don't break - continue to collect full fingerprint stats
            }
        }
        return result;
    }
    
    /*
        8GB RestSeg with 16-way associativity has 512K sets, so each set has 16 ways.
        * Each way holds one 4KB page frame.
        * 
        * Total entries = num_sets * associativity
        *               = (RestSeg_size / page_size) * associativity
        *               = (8GB / 4KB) * 16
        *               = 2M entries
        * Tag size = (VA_bits - page_size_bits - log2(num_sets)) = (48 - 12 - 19) = 17 bits ~ 4MB tag storage
        * For the same memory size we need a 16MB page table (4-level) to cover 8GB with 4KB pages
        * Fingerprint size = 4 bits * 2M entries = 1MB fingerprint
        * Fingerprint = 4-bit CRC-8 hash of VA for fast filtering ~ True negatives always 100%, 98\% true positives
     */

    /**
     * @brief Find any entry with matching tag (regardless of owner)
     * @param tag Tag bits from virtual address
     * @return Way index if found, -1 otherwise
     * 
     * Used for checking if a page is in RestSeg for ANY application.
     * Useful for shared page detection.
     */
    int findEntryByTag(UInt64 tag) {
        for (UInt32 i = 0; i < associativity; i++) {
            if (entries[i].valid && entries[i].tag == tag) {
                return i;
            }
        }
        return -1;
    }
    
    /**
     * @brief Find first invalid (free) way
     * @return Way index of free entry, -1 if set is full
     */
    int findFreeWay() {
        for (UInt32 i = 0; i < associativity; i++) {
            if (!entries[i].valid) {
                return i;
            }
        }
        return -1;
    }
    
    /**
     * @brief Find LRU victim for eviction
     * @return Way index of LRU entry, -1 if no valid entries
     * 
     * Finds the valid entry with the smallest lru_counter value,
     * which indicates it was accessed least recently.
     */
    int findLRUVictim() {
        int victim = -1;
        UInt64 min_lru = UINT64_MAX;
        for (UInt32 i = 0; i < associativity; i++) {
            if (entries[i].valid && entries[i].lru_counter < min_lru) {
                min_lru = entries[i].lru_counter;
                victim = i;
            }
        }
        return victim;
    }
    
    /**
     * @brief Insert a new entry into this set
     * @param tag Tag bits from virtual address
     * @param owner Owner ID (app_id + 1)
     * @param fingerprint 4-bit fingerprint computed from VA
     * @param force_evict If true, evict LRU entry when set is full
     * @param preferred_way Preferred way for RLE contiguity (-1 = no preference)
     * @param[out] used_way Output: the way that was actually used for insertion
     * @return Tuple of (success, eviction_occurred, evicted_tag, evicted_owner, used_preferred)
     * 
     * Insertion priority:
     *   1. If entry already exists (hit), just update LRU and return success
     *   2. If preferred_way is specified and that way is free, use it
     *   3. If any free way available, use it
     *   4. If force_evict=true and set is full, evict LRU entry
     *   5. Otherwise, return failure
     * 
     * When eviction occurs, the evicted entry's tag and owner are returned
     * so the caller can migrate it to FlexSeg.
     * 
     * Return tuple: (success, eviction_occurred, evicted_tag, evicted_owner, used_preferred, was_hit, used_free_way)
     */
    std::tuple<bool, bool, UInt64, UInt64, bool, bool, bool> insert(UInt64 tag, UInt64 owner, uint8_t fingerprint, 
                                                         bool force_evict, int preferred_way, int& used_way) {
        used_way = -1;
        
        // First check if already present (hit case)
        int existing = findEntry(tag, owner);
        if (existing >= 0) {
            entries[existing].lru_counter = ++global_lru_counter;
            entries[existing].reuse_count++;
            used_way = existing;
            return std::make_tuple(true, false, 0ULL, 0ULL, false, true, false);  // was_hit=true, used_free_way=false
        }
        
        // Try preferred way first (for RLE contiguity)
        if (preferred_way >= 0 && preferred_way < static_cast<int>(associativity)) {
            if (!entries[preferred_way].valid) {
                // Preferred way is free - use it!
                entries[preferred_way].valid = true;
                entries[preferred_way].tag = tag;
                entries[preferred_way].fingerprint = fingerprint;
                entries[preferred_way].owner = owner;
                entries[preferred_way].lru_counter = ++global_lru_counter;
                entries[preferred_way].reuse_count = 0;
                used_way = preferred_way;
                return std::make_tuple(true, false, 0ULL, 0ULL, true, false, true);  // used_preferred=true, used_free_way=true
            }
            // Preferred way is occupied - fall through to normal allocation
            // (We don't force_evict from preferred_way to maintain LRU fairness)
        }
        
        // Try to find any free way
        int free_way = findFreeWay();
        if (free_way >= 0) {
            entries[free_way].valid = true;
            entries[free_way].tag = tag;
            entries[free_way].fingerprint = fingerprint;
            entries[free_way].owner = owner;
            entries[free_way].lru_counter = ++global_lru_counter;
            entries[free_way].reuse_count = 0;
            used_way = free_way;
            return std::make_tuple(true, false, 0ULL, 0ULL, false, false, true);  // used_free_way=true
        }
        
        // No free way - need eviction if force_evict is set
        if (!force_evict) {
            return std::make_tuple(false, false, 0ULL, 0ULL, false, false, false);
        }
        
        // Find and evict LRU victim
        int victim = findLRUVictim();
        if (victim < 0) {
            return std::make_tuple(false, false, 0ULL, 0ULL, false, false, false);
        }
        
        // Save evicted entry info for caller to handle migration
        UInt64 evicted_tag = entries[victim].tag;
        UInt64 evicted_owner = entries[victim].owner;
        
        // Replace with new entry
        entries[victim].valid = true;
        entries[victim].tag = tag;
        entries[victim].fingerprint = fingerprint;
        entries[victim].owner = owner;
        entries[victim].lru_counter = ++global_lru_counter;
        entries[victim].reuse_count = 0;
        used_way = victim;
        
        return std::make_tuple(true, true, evicted_tag, evicted_owner, false, false, false);  // eviction, used_free_way=false
    }
    
    // Legacy insert without fingerprint (for backward compatibility)
    std::tuple<bool, bool, UInt64, UInt64, bool, bool, bool> insert(UInt64 tag, UInt64 owner, bool force_evict) {
        int dummy_way;
        return insert(tag, owner, 0, force_evict, -1, dummy_way);
    }
    
    // Legacy insert with fingerprint but no preferred_way (for backward compatibility)
    std::tuple<bool, bool, UInt64, UInt64> insertLegacy(UInt64 tag, UInt64 owner, uint8_t fingerprint, bool force_evict) {
        int dummy_way;
        auto [success, evicted, evicted_tag, evicted_owner, used_pref, was_hit, used_free] = 
            insert(tag, owner, fingerprint, force_evict, -1, dummy_way);
        return std::make_tuple(success, evicted, evicted_tag, evicted_owner);
    }
    
    /**
     * @brief Check if a specific way is free (invalid) in this set
     * @param way Way index to check
     * @return true if way is free (invalid), false if occupied or out of bounds
     */
    bool isWayFree(int way) const {
        return (way >= 0 && way < (int)associativity && !entries[way].valid);
    }
    
    /**
     * @brief Try to insert at a specific way only, no fallback
     * @param tag Tag bits from virtual address
     * @param owner Owner ID (app_id + 1)
     * @param fingerprint 4-bit fingerprint computed from VA
     * @param way The specific way to try
     * @param[out] used_way Output: the way used (-1 if failed)
     * @param[out] was_hit Output: true if entry already existed
     * @return true if insertion succeeded (either hit or way was free)
     * 
     * This method does NOT scan for other free ways or evict.
     * It either uses the specified way or fails.
     */
    bool tryInsertAtWay(UInt64 tag, UInt64 owner, uint8_t fingerprint,
                        int way, int& used_way, bool& was_hit) {
        used_way = -1;
        was_hit = false;
        
        // First check if already present (hit case)
        int existing = findEntry(tag, owner);
        if (existing >= 0) {
            entries[existing].lru_counter = ++global_lru_counter;
            entries[existing].reuse_count++;
            used_way = existing;
            was_hit = true;
            return true;
        }
        
        // Check if specified way is valid and free
        if (way < 0 || way >= (int)associativity) return false;
        if (entries[way].valid) return false;
        
        // Way is free - use it
        entries[way].valid = true;
        entries[way].tag = tag;
        entries[way].fingerprint = fingerprint;
        entries[way].owner = owner;
        entries[way].lru_counter = ++global_lru_counter;
        entries[way].reuse_count = 0;
        used_way = way;
        return true;
    }
    
    /**
     * @brief Update LRU state on access (touch)
     * @param way Way index to touch
     * 
     * Called on every access to update recency information.
     */
    void touchEntry(int way) {
        if (way >= 0 && way < (int)associativity && entries[way].valid) {
            entries[way].lru_counter = ++global_lru_counter;
            entries[way].reuse_count++;
        }
    }
    
    /**
     * @brief Count number of valid entries in this set
     * @return Count of valid entries (0 to associativity)
     */
    UInt32 countValid() const {
        UInt32 count = 0;
        for (UInt32 i = 0; i < associativity; i++) {
            if (entries[i].valid) count++;
        }
        return count;
    }
};

/*
 * ================================================================================
 * RestSeg - Restricted Segment (set-associative page placement structure)
 * ================================================================================
 * 
 * A RestSeg is a region of physical memory managed as a set-associative structure,
 * similar to a CPU cache but for page mappings. Each RestSeg is configured with:
 *   - A fixed page size (e.g., 2MB or 1GB)
 *   - An associativity (number of ways per set)
 *   - A total capacity in MB
 * 
 * ADDRESS TRANSLATION IN RESTSEG (from paper Section 4.2):
 * ---------------------------------------------------------
 * The key innovation is that physical addresses can be computed directly:
 * 
 *   Virtual Address:  | Tag | Set Index | Page Offset |
 *                     |<--- VPN ------->|<-- offset -->|
 * 
 *   set_index = VPN % num_sets
 *   tag = VPN / num_sets
 *   
 *   Physical Address = RestSeg_base + (set_index * assoc + way) * page_size
 * 
 * This eliminates the need for page table walks for RestSeg pages!
 * 
 * METADATA STRUCTURES (from paper Section 4.3):
 * ----------------------------------------------
 * Each core maintains two small structures per RestSeg:
 * 
 *   1. Permission Filter (PF): A compact bit-vector indicating which sets
 *      contain entries owned by this core. Size ≈ (num_sets * log2(assoc)) bits.
 *      Enables quick "definitely not present" filtering.
 * 
 *   2. Tag Array (TA): Stores the tag bits for entries owned by this core.
 *      Only accessed after PF indicates possible presence.
 *      Size ≈ num_sets * assoc * tag_bits bytes.
 * 
 * TRANSLATION FLOW:
 * -----------------
 *   1. Check Permission Filter - if empty for this set, page NOT in RestSeg
 *   2. If PF indicates possible hit, check Tag Array
 *   3. If tag matches, compute physical address directly
 *   4. If miss, fall back to conventional page table walk (FlexSeg path)
 * 
 * EVICTION POLICY:
 * ----------------
 * When a set is full and a new page must be allocated:
 *   - LRU entry is evicted
 *   - Evicted page is migrated to FlexSeg (buddy allocator)
 *   - Page table is updated to point to new FlexSeg location
 *   - This is called a "conflict eviction"
 */
template <typename Policy>
class RestSeg {
private:
    int id;                      ///< RestSeg identifier (1-indexed)
    UInt64 size_bytes;           ///< Total size in bytes
    int page_size_bits;          ///< Page size in bits (12=4KB, 21=2MB, 30=1GB)
    UInt32 associativity;        ///< Number of ways per set
    UInt32 num_sets;             ///< Number of sets = size / (assoc * page_size)
    
    std::vector<RestSegSet> sets;  ///< The actual set-associative structure
    IntPtr base_physical_address;  ///< Base physical address of this RestSeg
    
    // ============ Statistics ============
    UInt64 accesses = 0;         ///< Total lookups
    UInt64 hits = 0;             ///< Successful lookups (page found)
    UInt64 conflicts = 0;        ///< Evictions due to set conflicts
    UInt64 allocations = 0;      ///< Successful allocations
    
    // ============ Fingerprint Statistics ============
    mutable UInt64 fingerprint_checks = 0;      ///< Total fingerprint comparisons
    mutable UInt64 fingerprint_matches = 0;     ///< Fingerprint matches (potential hits)
    mutable UInt64 fingerprint_false_positives = 0; ///< FP matched but tag didn't (false positive)
    mutable UInt64 fingerprint_true_positives = 0;  ///< FP matched and tag matched (true positive)
    
    // ============ Region Preferred Way Table ============
    // Simplified RLE: stores one preferred way per region (1GB regions).
    // On allocation: try region's preferred way first, then global_way, then fallback.
    // When allocation succeeds (new allocation), update region's preferred way.
    
    /// Region size: 1GB = 2^30 bytes = 2^18 pages (256K pages)
    static constexpr int RLE_REGION_BITS = 18;
    static constexpr UInt64 RLE_PAGES_PER_REGION = 1ULL << RLE_REGION_BITS;
    
    /**
     * @brief Per-region way preference entry
     */
    struct RegionWayPref {
        int way = -1;          ///< Preferred way for this region, -1 means unknown
        UInt32 failures = 0;   ///< Counts consecutive failed attempts to use preferred way
        UInt32 successes = 0;  ///< Counts successful uses of preferred way
        UInt64 total_allocations = 0;  ///< Total allocations in this region
        UInt64 rle_misses = 0;         ///< Allocations that didn't use preferred way
    };
    
    /// Region preferred way table: region_id -> RegionWayPref
    std::unordered_map<UInt64, RegionWayPref> region_way_pref;
    
    /// Failure threshold: clear preference after this many consecutive failures
    static constexpr UInt32 REGION_PREF_FAILURE_THRESHOLD = 32;
    
    // ============ Region Way Statistics ============
    mutable UInt64 region_way_attempts = 0;    ///< Times we tried region preferred way
    mutable UInt64 region_way_hits = 0;        ///< Times region preferred way succeeded (new alloc)
    mutable UInt64 region_way_relearns = 0;    ///< Times preference was cleared due to failures
    
    // ============ Global Way Preference (RLE-friendly placement) ============
    // Tracks per-way availability and maintains a global preferred way
    // that is shifted when it becomes congested.
    
    std::vector<UInt32> free_sets_per_way;  ///< Count of sets where each way is free (size=associativity)
    int global_way = 0;                      ///< Current preferred way index
    UInt64 global_way_rotations = 0;         ///< Number of times global_way was rotated
    UInt64 global_way_attempts = 0;          ///< Total allocation attempts
    UInt64 global_way_hits = 0;              ///< Allocations that successfully used global_way
    UInt64 global_way_fallbacks = 0;         ///< Allocations that couldn't use global_way
    
    // Congestion detection and rotation policy
    double congestion_free_frac = 0.10;      ///< Rotate if free fraction of global_way < this (10%)
    UInt32 rotate_cooldown = 0;              ///< Cooldown counter to avoid thrashing
    UInt32 rotate_cooldown_max = 1024;       ///< Only rotate at most once per N allocs
    UInt64 alloc_counter = 0;                ///< Total allocation counter
    
    // ============ Thread Safety ============
    mutable std::mutex lock;     ///< Mutex for thread-safe operations
    
    // ============ Per-Core Metadata ============
    // These simulate the hardware metadata structures.
    // Fingerprint array provides first-level filtering before tag lookup.
    std::vector<IntPtr> tag_bases;         ///< Base addresses of per-core tag arrays
    std::vector<IntPtr> fingerprint_bases; ///< Base addresses of per-core fingerprint arrays
    
    int tag_entry_bytes;         ///< Bytes per entry in tag array
    int fingerprint_bits;        ///< Number of bits in fingerprint (1-8)
    bool fingerprint_enabled;    ///< Whether fingerprint-first filtering is enabled

    // ============ Per-App Radix Way Table ============
    // NEW: Radix tree for VPN → {valid, way} mapping
    // This replaces fingerprint+tag lookup on the fast translation path
    std::unique_ptr<RadixWayTableManager> radix_way_tables;
    
    // Radix table statistics
    mutable UInt64 radix_lookups = 0;
    mutable UInt64 radix_hits = 0;
    mutable UInt64 radix_misses = 0;

    // Per-RestSeg logger for debugging
    mutable std::unique_ptr<SimLog> m_log;

public:
    /**
     * @brief Construct a RestSeg with specified parameters
     * @param _id Unique identifier for this RestSeg (1-indexed)
     * @param size_mb Total size in megabytes
     * @param _page_size_bits Page size as power of 2 (21=2MB, 30=1GB)
     * @param _assoc Associativity (number of ways per set)
     * @param num_cores Number of cores (for per-core metadata allocation)
     * 
     * The number of sets is calculated as:
     *   num_sets = size_bytes / (associativity * page_size)
     * 
     * Example: 4GB RestSeg with 2MB pages and 16-way associativity:
     *   num_sets = 4GB / (16 * 2MB) = 128 sets
     *   Each set can hold 16 pages, total capacity = 2048 pages = 4GB
     */
    RestSeg(int _id, UInt64 size_mb, int _page_size_bits, UInt32 _assoc, int num_cores, 
            int _fingerprint_bits = 4, bool _fingerprint_enabled = true)
        : id(_id),
          page_size_bits(_page_size_bits),
          associativity(_assoc),
          fingerprint_bits(_fingerprint_bits),
          fingerprint_enabled(_fingerprint_enabled)
    {
        size_bytes = size_mb * 1024ULL * 1024ULL;
        UInt64 page_size = 1ULL << page_size_bits;
        num_sets = size_bytes / (associativity * page_size);
        
        // Create all sets with specified associativity
        sets.reserve(num_sets);
        for (UInt32 i = 0; i < num_sets; i++) {
            sets.emplace_back(associativity);
        }
        
        // Calculate metadata sizes
        // Tag array: stores upper address bits not used for set indexing
        // Tag bits = 48 (VA bits) - page_offset_bits - set_index_bits
        tag_entry_bytes = (48 - page_size_bits - static_cast<int>(std::ceil(std::log2(num_sets)))) / 8;
        if (tag_entry_bytes < 1) tag_entry_bytes = 1;
        
        // Metadata bases will be set externally via setMetadataBases() using
        // handle_page_table_allocations() to get proper simulated physical addresses
        // Initialize vectors with correct size but zero values
        tag_bases.resize(num_cores, 0);
        fingerprint_bases.resize(num_cores, 0);
        
        // Initialize global-way availability tracking
        // Initially all ways are free in all sets
        free_sets_per_way.assign(associativity, num_sets);
        global_way = 0;
        
        // Initialize per-app radix way table manager WITHOUT allocator
        // The allocator will be set via setRadixAllocator() from the parent Utopia class
        // after handle_page_table_allocations becomes available
        radix_way_tables = std::make_unique<RadixWayTableManager>(page_size_bits, associativity, nullptr);
        
        // Initialize per-RestSeg logger
        // Name format: "RestSeg-2MB" or "RestSeg-4KB" based on page size
        std::string page_size_str = (page_size_bits == 21) ? "2MB" : 
                                    (page_size_bits == 12) ? "4KB" : 
                                    (page_size_bits == 30) ? "1GB" : std::to_string(page_size_bits);
        std::string log_name = "RestSeg-" + page_size_str;
        m_log = std::make_unique<SimLog>(log_name, -1, DEBUG_UTOPIA);
        
        // Verify XOR-folding indexing logic is reversible
        verifyIndexingLogic();
    }
    
    /**
     * @brief Set the allocator callback for radix node physical addresses
     * @param alloc_fn Function that allocates size bytes and returns simulated physical address
     * 
     * Must be called after construction, before first radix table access.
     * Typically called from Utopia constructor using handle_page_table_allocations.
     */
    void setRadixAllocator(std::function<IntPtr(size_t)> alloc_fn) {
        // Recreate the radix manager with the allocator
        radix_way_tables = std::make_unique<RadixWayTableManager>(page_size_bits, associativity, alloc_fn);
    }
    
    ~RestSeg() {
        // Note: dumpRegionRLEStats() is called from UtopiaAllocator destructor
        // to ensure Policy context is still valid when accessing output directory
        // Metadata is allocated from kernel region, not host memory - no delete needed
    }
    
    /**
     * @brief Dump per-region RLE statistics to a CSV file
     * 
     * For each 1GB region, outputs:
     *   - region_id: The region identifier
     *   - total_allocations: Total page allocations in this region
     *   - rle_misses: Allocations that didn't follow the preferred way
     *   - rle_hit_percent: Percentage of allocations that used preferred way
     */
    void dumpRegionRLEStats() const {
        // Only dump for 4KB pages (RLE tracking is disabled for larger pages)
        if (page_size_bits != 12) return;
        
        std::lock_guard<std::mutex> guard(lock);
        
        // Always try to write file, even if empty
        std::string csv_path = Policy::get_output_directory() + "/region_rle_stats_restseg" + std::to_string(id) + ".csv";
        std::ofstream csv_file(csv_path);
        
        if (!csv_file.is_open()) {
            std::cerr << "[RestSeg " << id << "] Failed to open " << csv_path << " for writing\n";
            return;
        }
        
        // Write header
        csv_file << "region_id,total_allocations,rle_misses,rle_hit_percent\n";
        
        if (region_way_pref.empty()) {
            csv_file.close();
            return;
        }
        
        // Collect and sort regions by ID for consistent output
        std::vector<std::pair<UInt64, const RegionWayPref*>> sorted_regions;
        for (const auto& [region_id, pref] : region_way_pref) {
            if (pref.total_allocations > 0) {
                sorted_regions.push_back({region_id, &pref});
            }
        }
        std::sort(sorted_regions.begin(), sorted_regions.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        
        // Write per-region stats
        for (const auto& [region_id, pref] : sorted_regions) {
            UInt64 total = pref->total_allocations;
            UInt64 misses = pref->rle_misses;
            double hit_percent = (total > 0) ? 100.0 * (total - misses) / total : 0.0;
            
            csv_file << region_id << ","
                     << total << ","
                     << misses << ","
                     << std::fixed << std::setprecision(2) << hit_percent << "\n";
        }
        
        csv_file.close();
    }
    
    // ============ Address Splitting ============
    
    /**
     * @brief Split virtual address into tag and set index using XOR-folding
     * @param address Virtual address to split
     * @param[out] tag Upper bits of VPN (VPN / num_sets)
     * @param[out] set_index XOR-folded index for better distribution
     * 
     * XOR-Folding Logic:
     *   raw_index = VPN % num_sets
     *   set_index = (raw_index ^ (tag % num_sets)) % num_sets
     * 
     * This uses entropy from the tag to randomize which set a page maps to,
     * breaking linear strides that would otherwise cause conflict misses.
     * The final modulo ensures set_index stays within bounds even for
     * non-power-of-2 num_sets.
     * 
     * Address format:
     *   | Tag (upper VPN bits) | Set Index (XOR-folded) | Page Offset |
     *   |<-------- tag ------->|<----- set_index ------>|<- ignored ->|
     */
    void splitAddress(IntPtr address, UInt64& tag, UInt32& set_index) const {
        UInt64 page_num = address >> page_size_bits;  // Remove page offset
        tag = page_num / num_sets;                    // Upper bits -> tag
        UInt32 raw_index = page_num % num_sets;       // Lower bits -> raw index
        
        // XOR-fold: mix in lower bits of tag for better distribution
        // Final modulo ensures index stays in bounds for non-power-of-2 num_sets
        set_index = (raw_index ^ (static_cast<UInt32>(tag) % num_sets)) % num_sets;
    }
    
    /**
     * @brief Reconstruct virtual address from tag and XOR-folded set index
     * @param tag Upper VPN bits
     * @param set_index XOR-folded set index (already in bounds)
     * @return Virtual address (page-aligned, offset = 0)
     * 
     * For non-power-of-2 num_sets, the XOR-folding with final modulo means
     * the mapping is not perfectly reversible. We use a simple approximation:
     *   raw_index = set_index ^ (tag % num_sets) % num_sets
     *   page_num = tag * num_sets + raw_index
     * 
     * This reconstructs the ORIGINAL raw_index, not the XOR'd set_index.
     */
    IntPtr reconstructAddress(UInt64 tag, UInt32 set_index) const {
        // Reverse the XOR and modulo
        UInt32 raw_index = (set_index ^ (static_cast<UInt32>(tag) % num_sets)) % num_sets;
        UInt64 page_num = tag * num_sets + raw_index;
        return page_num << page_size_bits;
    }
    
private:
    /**
     * @brief Verify that XOR-folding indexing logic is perfectly reversible
     * 
     * Called once in constructor to validate that splitAddress and
     * reconstructAddress are proper inverses of each other.
     */
    void verifyIndexingLogic() const {
        // Test with a variety of VPNs including edge cases
        std::vector<IntPtr> test_vas = {
            0x12345678000ULL,   // Arbitrary address
            0xABCDE000ULL,      // Another arbitrary
            0x0ULL,             // Zero
            0x7FFFFFFFFFFF000ULL, // Near max 48-bit VA
            (1ULL << page_size_bits) * num_sets,  // Exactly one tag increment
            (1ULL << page_size_bits) * (num_sets + 1),  // Tag=1, raw_index=1
        };
        
        for (IntPtr va : test_vas) {
            UInt64 tag;
            UInt32 set_idx;
            splitAddress(va, tag, set_idx);
            IntPtr reconstructed = reconstructAddress(tag, set_idx);
            IntPtr expected = va & ~((1ULL << page_size_bits) - 1);  // Page-align
            if (reconstructed != expected) {
                throw std::runtime_error(
                    "Utopia XOR-Folding Indexing Error: Mapping is not reversible! "
                    "VA=" + std::to_string(va) + 
                    " expected=" + std::to_string(expected) + 
                    " got=" + std::to_string(reconstructed));
            }
        }
    }
    
public:
    
    // ============ Core Operations ============
    
    /**
     * @brief Check if a page is present in this RestSeg
     * @param address Virtual address to look up
     * @param app_id Application/process ID
     * @param count_stats Whether to update access statistics
     * @return true if page is in RestSeg, false otherwise
     * 
     * This is the fast-path lookup. If successful, the physical address
     * can be computed directly without a page table walk.
     */
    bool inRestSeg(IntPtr address, int app_id, bool count_stats = true) {
        std::lock_guard<std::mutex> guard(lock);
        
        UInt64 tag;
        UInt32 set_index;
        splitAddress(address, tag, set_index);
        
        // Compute fingerprint from tag (not full VA - set index bits already used for indexing)
        uint8_t fp = computeFingerprint(tag, fingerprint_bits);
        
        if (count_stats) accesses++;
        
        // ================================================================
        // Standard Lookup (ground truth)
        // ================================================================
        UInt64 fp_checks = 0, fp_matches = 0, fp_false_positives = 0, fp_true_positives = 0;
        int way = sets[set_index].findEntryWithFingerprintShadow(tag, app_id + 1, fp,
                                                                  fp_checks, fp_matches, 
                                                                  fp_false_positives, fp_true_positives);
        
        // Update fingerprint statistics
        if (count_stats) {
            fingerprint_checks += fp_checks;
            fingerprint_matches += fp_matches;
            fingerprint_false_positives += fp_false_positives;
            fingerprint_true_positives += fp_true_positives;
        }
        
        if (way >= 0) {
            if (count_stats) hits++;
            sets[set_index].touchEntry(way);  // Update LRU
            return true;
        }
        return false;
    }
    
    /**
     * @brief Lookup and return the way index for an address in this RestSeg
     * @param address Virtual address to look up
     * @param app_id Application/process ID
     * @return Way index if found, -1 otherwise
     * 
     * This is used by the UTLB to cache the (seg_id, way) pair for an address.
     * Unlike inRestSeg, this does not update statistics or LRU state.
     */
    int lookupWay(IntPtr address, int app_id) const {
        std::lock_guard<std::mutex> guard(lock);
        
        UInt64 tag;
        UInt32 set_index;
        splitAddress(address, tag, set_index);
        
        return sets[set_index].findEntry(tag, app_id + 1);
    }
    
    /**
     * @brief Fast radix-based lookup for VPN → way mapping
     * @param address Virtual address to look up
     * @param app_id Application/process ID
     * @param[out] way_out Way index if found
     * @return true if VPN is valid in radix table, false otherwise
     * 
     * This is the NEW fast-path lookup using the per-app radix table.
     * It replaces fingerprint + tag array access on the translation path.
     * Does NOT update LRU state (caller should call touchEntry if needed).
     */
    bool lookupWayRadix(IntPtr address, int app_id, int& way_out) const {
        UInt64 vpn = address >> page_size_bits;
        radix_lookups++;
        
        // Compute set index for debug output
        UInt64 tag;
        UInt32 set_index;
        splitAddress(address, tag, set_index);
        
        m_log->debug("[lookupWayRadix] START: VA=", SimLog::hex(address),
                    " VPN=", SimLog::hex(vpn),
                    " app_id=", app_id,
                    " page_size_bits=", page_size_bits,
                    " set_index=", set_index,
                    " tag=", SimLog::hex(tag));
        
        bool found = radix_way_tables->lookup_way(vpn, app_id, way_out);
        
        if (found) {
            radix_hits++;
            
            // Compute physical address for the found way
            IntPtr phys_addr = base_physical_address + 
                              (static_cast<IntPtr>(set_index) * associativity + way_out) * 
                              (1ULL << page_size_bits);
            IntPtr ppn = phys_addr >> 12;  // PPN in 4KB units
            
            m_log->debug("[lookupWayRadix] HIT: VA=", SimLog::hex(address),
                        " VPN=", SimLog::hex(vpn),
                        " -> way=", way_out,
                        " set=", set_index,
                        " phys_addr=", SimLog::hex(phys_addr),
                        " ppn=", ppn,
                        " (radix_hits=", radix_hits,
                        " radix_lookups=", radix_lookups, ")");
        } else {
            radix_misses++;
            m_log->debug("[lookupWayRadix] MISS: VA=", SimLog::hex(address),
                        " VPN=", SimLog::hex(vpn),
                        " app_id=", app_id,
                        " set_index=", set_index,
                        " (radix_misses=", radix_misses,
                        " radix_lookups=", radix_lookups, ")");
        }
        
        return found;
    }
    
    /**
     * @brief Get number of radix levels for this RestSeg's radix table
     * @return Number of levels (3 for 2MB, 4 for 4KB)
     */
    int getRadixLevels() const {
        return radix_way_tables ? radix_way_tables->getNumLevels() : 0;
    }
    
    /**
     * @brief Get radix table manager for this RestSeg
     * @return Pointer to the RadixWayTableManager
     */
    RadixWayTableManager* getRadixWayTableManager() {
        return radix_way_tables.get();
    }
    
    const RadixWayTableManager* getRadixWayTableManager() const {
        return radix_way_tables.get();
    }
    
    // Radix statistics accessors
    UInt64 getRadixLookups() const { return radix_lookups; }
    UInt64 getRadixHits() const { return radix_hits; }
    UInt64 getRadixMisses() const { return radix_misses; }
    UInt64* getRadixLookupsPtr() { return &radix_lookups; }
    UInt64* getRadixHitsPtr() { return &radix_hits; }
    UInt64* getRadixMissesPtr() { return &radix_misses; }

    /**
     * @brief Allocate a page in this RestSeg
     * @param address Virtual address to allocate
     * @param app_id Application/process ID
     * @param force_evict If true, evict LRU entry when set is full
     * @return Tuple of (success, eviction_occurred, evicted_virtual_address)
     * 
     * Allocation behavior:
     *   - If page already in RestSeg: success, no eviction (hit)
     *   - Try adjacent RLE run's way first (for run extension)
     *   - If no adjacent run or its way is occupied, try global_way
     *   - If global_way occupied, fall back to any free way
     *   - If set is full and force_evict=false: failure
     *   - If set is full and force_evict=true: evict LRU, success, eviction
     * 
     * When eviction occurs, the evicted page's virtual address is returned
     * so it can be migrated to FlexSeg.
     */
    std::tuple<bool, bool, IntPtr> allocate(IntPtr address, int app_id, bool force_evict = false) {
        std::lock_guard<std::mutex> guard(lock);
        
        UInt64 tag;
        UInt32 set_index;
        splitAddress(address, tag, set_index);
        
        // Compute VPN for RLE tracking (address >> page_size_bits)
        UInt64 vpn = address >> page_size_bits;
        UInt64 owner = app_id + 1;
        
        // Compute fingerprint from tag (not full VA)
        uint8_t fp = computeFingerprint(tag, fingerprint_bits);
        
        // Track allocation attempts
        global_way_attempts++;
        alloc_counter++;
        
        // ================================================================
        // Step A: Get region preferred way (only for 4KB pages)
        // ================================================================
        UInt64 region_id = vpn >> RLE_REGION_BITS;
        int region_pref_way = -1;
        bool use_region_pref = (page_size_bits == 12);  // Only for 4KB pages
        
        if (use_region_pref) {
            auto pref_it = region_way_pref.find(region_id);
            if (pref_it != region_way_pref.end()) {
                region_pref_way = pref_it->second.way;
            }
        }
        
        // ================================================================
        // Step B: Try region preferred way first (4KB only), then global_way, then fallback
        // ================================================================
        int used_way = -1;
        bool was_hit = false;
        bool used_free_way = false;
        bool success = false;
        bool evicted = false;
        UInt64 evicted_tag = 0;
        UInt64 evicted_owner_val = 0;
        bool success_from_region_pref = false;
        
        // Priority 1: Try region preferred way if available
        if (region_pref_way >= 0) {
            region_way_attempts++;
            if (sets[set_index].tryInsertAtWay(tag, owner, fp, region_pref_way, used_way, was_hit)) {
                success = true;
                success_from_region_pref = true;
                if (!was_hit) {
                    used_free_way = true;
                    region_way_hits++;
                }
            }
        }
        
        // Priority 2: Try global_way (if region preferred way failed or not available)
        if (!success && region_pref_way != global_way) {
            if (sets[set_index].tryInsertAtWay(tag, owner, fp, global_way, used_way, was_hit)) {
                success = true;
                if (!was_hit) {
                    used_free_way = true;
                    global_way_hits++;
                }
            }
        }
        
        // Priority 3: Fall back to regular insert (find any free way, or evict)
        if (!success) {
            global_way_fallbacks++;
            
            auto result = sets[set_index].insert(tag, owner, fp, force_evict, -1, used_way);
            success = std::get<0>(result);
            evicted = std::get<1>(result);
            evicted_tag = std::get<2>(result);
            evicted_owner_val = std::get<3>(result);
            // used_preferred = std::get<4>(result);  // Not used here
            was_hit = std::get<5>(result);
            used_free_way = std::get<6>(result);
        }
        
        // ================================================================
        // Handle region preference failure (only for 4KB pages)
        // ================================================================
        if (use_region_pref && !success_from_region_pref && region_pref_way >= 0) {
            auto& pref = region_way_pref[region_id];
            if (++pref.failures >= REGION_PREF_FAILURE_THRESHOLD) {
                pref.way = -1;
                pref.failures = 0;
                region_way_relearns++;
            }
        }
        
        if (!success) {
            return std::make_tuple(false, false, static_cast<IntPtr>(0));
        }
        
        // ================================================================
        // Step C: Update free_sets_per_way and stats (only for new allocations)
        // ================================================================
        if (!was_hit) {
            allocations++;
            
            // Update free_sets_per_way if we consumed an invalid (free) entry
            if (used_free_way && used_way >= 0 && used_way < static_cast<int>(associativity)) {
                if (free_sets_per_way[used_way] > 0) {
                    free_sets_per_way[used_way]--;
                }
            }
            
            // ================================================================
            // Step D: Update region preferred way (only for 4KB pages)
            // ================================================================
            if (use_region_pref) {
                auto& pref = region_way_pref[region_id];
                pref.total_allocations++;
                
                // Track RLE miss: allocation didn't use the preferred way
                if (!success_from_region_pref) {
                    pref.rle_misses++;
                }
                
                pref.way = used_way;
                pref.successes++;
                pref.failures = 0;  // Reset failures on success
            }
            
            // ================================================================
            // Step E: Check for global_way congestion and maybe rotate
            // ================================================================
            maybeRotateGlobalWay();
        }
        
        // ================================================================
        // Step F: Handle eviction (split runs if needed)
        // ================================================================
        if (evicted) {
            conflicts++;
            // Reconstruct the evicted page's virtual address and VPN
            IntPtr evicted_addr = reconstructAddress(evicted_tag, set_index);
            UInt64 evicted_vpn = evicted_addr >> page_size_bits;
            int evicted_app_id = evicted_owner_val - 1;  // owner = app_id + 1
            
            // CRITICAL: Clear radix table entry for evicted page
            radix_way_tables->clear_mapping(evicted_vpn, evicted_app_id);
            
            // Note: No RLE map update needed - region preference is a placement
            // heuristic only and doesn't track individual page positions
            
            // Update radix table for newly allocated page
            radix_way_tables->set_mapping(vpn, app_id, used_way);
            
            return std::make_tuple(true, true, evicted_addr);
        }
        
        // ================================================================
        // Step G: Update radix table for successful allocation (no eviction)
        // ================================================================
        if (!was_hit) {
            // New allocation - add to radix table
            radix_way_tables->set_mapping(vpn, app_id, used_way);
        }
        
        return std::make_tuple(true, false, static_cast<IntPtr>(0));
    }
    
private:
    /**
     * @brief Check for global_way congestion and rotate to a less congested way if needed
     * 
     * Called after each new allocation. Rotates global_way when:
     *   - The free fraction of current global_way drops below congestion_free_frac
     *   - Cooldown period has passed (to avoid thrashing)
     * 
     * Rotation policy: choose the way with maximum free_sets_per_way[w].
     * On ties, prefer the next way after current (round-robin tie-break).
     */
    void maybeRotateGlobalWay() {
        // Decrement cooldown if active
        if (rotate_cooldown > 0) {
            rotate_cooldown--;
            return;
        }
        
        // Check if current global_way is congested
        double free_frac = static_cast<double>(free_sets_per_way[global_way]) / static_cast<double>(num_sets);
        if (free_frac >= congestion_free_frac) {
            return;  // Not congested, no rotation needed
        }
        
        // Find the way with maximum free sets
        int best = global_way;
        UInt32 best_free = free_sets_per_way[global_way];
        
        for (int w = 0; w < static_cast<int>(associativity); w++) {
            if (free_sets_per_way[w] > best_free) {
                best_free = free_sets_per_way[w];
                best = w;
            } else if (free_sets_per_way[w] == best_free && w != global_way) {
                // Tie-break: prefer next way after current (round-robin)
                int next_after_current = (global_way + 1) % associativity;
                if (w == next_after_current) {
                    best = w;
                }
            }
        }
        
        if (best != global_way) {
            // Log rotation event (debug)
            // std::cout << "[RestSeg " << id << "] Rotating global_way: " 
            //           << global_way << " (free=" << free_sets_per_way[global_way] 
            //           << ") -> " << best << " (free=" << best_free << ")" << std::endl;
            
            global_way = best;
            global_way_rotations++;
            rotate_cooldown = rotate_cooldown_max;
        }
    }
    
public:
    
    /**
     * @brief Calculate physical page number (PPN) for a page in this RestSeg
     * @param address Virtual address
     * @param app_id Application/process ID
     * @param count_rle_stats Whether to perform shadow RLE lookup for statistics
     * @return Physical Page Number (PPN) in 4KB units, or -1 if page not found
     * 
     * NOTE: Despite the name "PhysicalAddress", this returns a PPN in 4KB page units,
     *       NOT a byte address. The caller must shift left by 12 and add page offset
     *       to get the actual byte address: byte_addr = (ppn << 12) | page_offset
     * 
     * This is the KEY OPTIMIZATION of Utopia. The PPN is computed directly from 
     * (set_index, way) without page table access:
     * 
     *   ppn = RestSeg_base_ppn + (set_index * associativity + way) * factor
     * 
     * where factor = page_size / 4KB (e.g., 512 for 2MB pages, 1 for 4KB pages)
     */
    IntPtr calculatePhysicalAddress(IntPtr address, int app_id) const {
        std::lock_guard<std::mutex> guard(lock);
        
        UInt64 tag;
        UInt32 set_index;
        splitAddress(address, tag, set_index);
        
        // ================================================================
        // Standard Lookup (ground truth)
        // ================================================================
        int way = sets[set_index].findEntry(tag, app_id + 1);
        if (way < 0) {
            return static_cast<IntPtr>(-1);  // Not in RestSeg
        }
        
        // Direct physical address calculation (Utopia's key innovation)
        // Physical address = base + (set_index * assoc + way) * page_size
        // Factor converts from 4KB-relative addressing to actual page size
        int factor = 1 << (page_size_bits - 12);  // e.g., 512 for 2MB pages
        return base_physical_address + (set_index * associativity + way) * factor;
    }
    
    /**
     * @brief Get the way number for a given address (if present)
     * @param address Virtual address to look up
     * @param app_id Application/process ID
     * @return Way number if found, -1 if not in RestSeg
     * 
     * Used for tracking speculation correctness - to know which way was the correct one.
     */
    int getWayForAddress(IntPtr address, int app_id) const {
        std::lock_guard<std::mutex> guard(lock);
        
        UInt64 tag;
        UInt32 set_index;
        splitAddress(address, tag, set_index);
        
        return sets[set_index].findEntry(tag, app_id + 1);
    }

    /**
     * @brief Calculate speculative byte physical address for a specific way
     * @param address Virtual address (used to determine set_index and page offset)
     * @param way The way number to calculate address for
     * @return Full byte physical address for that way (NOT a PPN!)
     * 
     * NOTE: Unlike calculatePhysicalAddress() which returns PPN in 4KB units,
     *       this function returns a FULL BYTE ADDRESS including the page offset.
     *       Do NOT shift or add offset to this result.
     * 
     * Used for fingerprint-based speculation: when fingerprint matches,
     * we can speculatively compute the physical address for that way
     * and prefetch the data cache line before tag verification completes.
     * 
     * This does NOT verify if the way is actually valid or matches.
     */
    IntPtr calculatePhysicalAddressForWay(IntPtr address, int way) const {
        UInt64 tag;
        UInt32 set_index;
        splitAddress(address, tag, set_index);
        
        // Direct calculation without lookup
        int factor = 1 << (page_size_bits - 12);
        IntPtr page_offset = address & ((1ULL << page_size_bits) - 1);
        IntPtr base_phys = base_physical_address + (set_index * associativity + way) * factor;
        
        // Return full physical address including page offset
        return (base_phys << 12) | page_offset;  // base_physical_address is in 4KB pages
    }
    
    /**
     * @brief Calculate PPN directly from way (radix-based fast path)
     * @param address Virtual address (used to determine set_index)
     * @param way The way number (obtained from radix table lookup)
     * @return Physical Page Number (PPN) in 4KB units
     * 
     * This is the KEY API for radix-based translation:
     *   1. lookupWayRadix(address, app_id, way) → get way from radix table
     *   2. calculatePPNFromWay(address, way) → compute PPN directly
     * 
     * No tag array access required - the radix table gives us the way directly.
     */
    IntPtr calculatePPNFromWay(IntPtr address, int way) const {
        UInt64 tag;
        UInt32 set_index;
        splitAddress(address, tag, set_index);
        
        // Direct PPN calculation: base + (set * assoc + way) * factor
        int factor = 1 << (page_size_bits - 12);  // e.g., 512 for 2MB pages, 1 for 4KB
        return base_physical_address + (set_index * associativity + way) * factor;
    }
    
    /**
     * @brief Touch LRU for a way after successful radix lookup
     * @param address Virtual address (to determine set_index)
     * @param way The way to touch
     * 
     * Called after radix lookup to update LRU state.
     */
    void touchWayLRU(IntPtr address, int way) {
        std::lock_guard<std::mutex> guard(lock);
        
        UInt64 tag;
        UInt32 set_index;
        splitAddress(address, tag, set_index);
        
        sets[set_index].touchEntry(way);
    }
    
    // ============ Metadata Address Calculations ============
    // These methods calculate simulated addresses for accessing the
    // per-core metadata structures. Used for modeling cache/memory
    // access latencies for metadata lookups.
    
    /**
     * @brief Calculate address of tag array entry for a set
     * @param address Virtual address
     * @param core_id Core performing the lookup
     * @return Simulated memory address of tag array set base
     * 
     * Used to model the latency of tag array access.
     * Returns the base address of the set (all ways read in parallel).
     * The entire set (assoc * tag_entry_bytes) typically fits in one cache line.
     */
    IntPtr calculateTagAddress(IntPtr address, int core_id) const {
        UInt64 tag;
        UInt32 set_index;
        splitAddress(address, tag, set_index);
        
        if (core_id < 0 || core_id >= static_cast<int>(tag_bases.size())) {
            return 0;
        }
        // Return base of the set - all ways are read in parallel during lookup
        return tag_bases[core_id] + set_index * associativity * tag_entry_bytes;
    }
    
    /**
     * @brief Calculate address of fingerprint array entry for a set
     * @param address Virtual address
     * @param core_id Core performing the lookup
     * @return Simulated memory address of fingerprint array set base
     * 
     * Used to model the latency of fingerprint array access.
     * Returns the base address of the set (all fingerprints for all ways).
     * The entire set (assoc * 1 byte) easily fits in one cache line.
     * 
     * Fingerprint array layout:
     *   [Set 0: Way0 FP | Way1 FP | ... | WayN FP][Set 1: ...][...]
     */
    IntPtr calculateFingerprintAddress(IntPtr address, int core_id) const {
        UInt64 tag;
        UInt32 set_index;
        splitAddress(address, tag, set_index);
        
        if (core_id < 0 || core_id >= static_cast<int>(fingerprint_bases.size())) {
            return 0;
        }
        // Return base of the set - all fingerprints for this set
        // Each entry is 1 byte, so offset = set_index * associativity
        return fingerprint_bases[core_id] + set_index * associativity;
    }
    
    /**
     * @brief Calculate simulated addresses for radix walk levels
     * @param address Virtual address being looked up
     * @param app_id Application ID for radix table lookup
     * @param[out] level_addresses Vector of (cache_line_addr, is_leaf) pairs
     * 
     * For latency modeling, each radix level is a cache line fetch.
     * Uses the actual radix node simulated physical addresses allocated via
     * handle_page_table_allocations().
     * 
     * Returns addresses for all levels including leaf.
     */
    void calculateRadixLevelAddresses(IntPtr address, int app_id, 
                                      std::vector<std::pair<IntPtr, bool>>& level_addresses) const {
        level_addresses.clear();
        
        if (!radix_way_tables) return;
        
        UInt64 vpn = address >> page_size_bits;
        
        // Get traversal addresses from the radix table
        std::vector<std::tuple<int, IntPtr, bool>> traversal;
        radix_way_tables->getTraversalAddresses(vpn, app_id, traversal);
        
        for (const auto& entry : traversal) {
            IntPtr addr = std::get<1>(entry);
            bool is_leaf = std::get<2>(entry);
            level_addresses.push_back(std::make_pair(addr, is_leaf));
        }
    }
    
    /**
     * @brief Get leaf_entries for this RestSeg's radix table
     */
    int getRadixLeafEntries() const {
        return radix_way_tables ? radix_way_tables->getLeafEntries() : 0;
    }
    
    /**
     * @brief Get fingerprint for a specific entry (for lookup comparison)
     * @param address Virtual address
     * @param app_id Application ID
     * @param way Way index within the set
     * @return Fingerprint stored for that entry, or 0xFF if invalid
     */
    uint8_t getStoredFingerprint(IntPtr address, int app_id, int way) const {
        std::lock_guard<std::mutex> guard(lock);
        
        UInt64 tag;
        UInt32 set_index;
        splitAddress(address, tag, set_index);
        
        if (way < 0 || way >= static_cast<int>(associativity)) {
            return 0xFF;
        }
        
        const auto* entry = sets[set_index].getEntry(way);
        if (!entry || !entry->valid) {
            return 0xFF;  // Invalid entry marker
        }
        return entry->fingerprint;
    }
    
    /**
     * @brief Find ways that match the given fingerprint (for fingerprint-first filtering)
     * @param address Virtual address (to determine set)
     * @param lookup_fingerprint Fingerprint computed from lookup address
     * @return Vector of way indices that match the fingerprint
     */
    std::vector<int> findMatchingFingerprints(IntPtr address, uint8_t lookup_fingerprint) const {
        std::lock_guard<std::mutex> guard(lock);
        
        UInt64 tag;
        UInt32 set_index;
        splitAddress(address, tag, set_index);
        
        std::vector<int> matching_ways;
        const auto& set = sets[set_index];
        
        for (UInt32 way = 0; way < associativity; way++) {
            const auto* entry = set.getEntry(way);
            if (entry && entry->valid && entry->fingerprint == lookup_fingerprint) {
                matching_ways.push_back(way);
            }
        }
        
        return matching_ways;
    }
    
    /**
     * @brief Compute the lookup fingerprint for a given address
     * @param address Virtual address
     * @return 4-bit fingerprint for comparison
     * 
     * This extracts the tag from the address and computes CRC-8 fingerprint.
     * Used for fingerprint-first filtering during RestSeg lookup.
     */
    uint8_t computeLookupFingerprint(IntPtr address) const {
        UInt64 tag;
        UInt32 set_index;
        splitAddress(address, tag, set_index);
        return computeFingerprint(tag, fingerprint_bits);
    }
    
    // ============ Accessors ============
    
    int getId() const { return id; }                       ///< RestSeg identifier
    int getPageSizeBits() const { return page_size_bits; } ///< Page size as power of 2
    UInt32 getAssociativity() const { return associativity; } ///< Ways per set
    UInt32 getNumSets() const { return num_sets; }         ///< Number of sets
    UInt64 getSizeBytes() const { return size_bytes; }     ///< Total size in bytes
    UInt64 getSizeMB() const { return size_bytes / (1024 * 1024); } ///< Total size in MB
    
    void setBase(IntPtr base) { base_physical_address = base; } ///< Set base physical address
    
    /**
     * @brief Set metadata base addresses for a specific core
     * @param core_id Core index
     * @param tag_base Physical address (in bytes) for tag array
     * @param fp_base Physical address (in bytes) for fingerprint array
     * 
     * These should be allocated from the kernel region using handle_page_table_allocations()
     * to ensure they are valid simulated physical addresses.
     * 
     * Note: Permission filter has been removed - fingerprint array now provides
     * first-level filtering before tag lookup.
     */
    void setMetadataBases(int core_id, IntPtr tag_base, IntPtr fp_base) {
        if (core_id >= 0 && core_id < static_cast<int>(tag_bases.size())) {
            tag_bases[core_id] = tag_base;
            fingerprint_bases[core_id] = fp_base;
        }
    }
    
    /// Get size in bytes needed for tag array per core  
    UInt64 getTagArraySize() const {
        return num_sets * associativity * tag_entry_bytes;
    }
    
    /// Get size in bytes needed for fingerprint array per core
    /// One byte per entry (stores fingerprint for each way in each set)
    UInt64 getFingerprintArraySize() const {
        return num_sets * associativity * 1;  // 1 byte per entry
    }
    
    /// Check if fingerprint-first filtering is enabled
    bool isFingerprintEnabled() const { return fingerprint_enabled; }
    
    IntPtr getBase() const { return base_physical_address; }    ///< Get base physical address
    
    // ============ Statistics Accessors ============
    UInt64 getAccesses() const { return accesses; }        ///< Total lookup count
    UInt64 getHits() const { return hits; }                ///< Successful lookup count
    UInt64 getConflicts() const { return conflicts; }      ///< Eviction count due to conflicts
    UInt64 getAllocations() const { return allocations; }  ///< Successful allocation count
    
    // ============ Fingerprint Statistics Accessors ============
    UInt64 getFingerprintChecks() const { return fingerprint_checks; }
    UInt64 getFingerprintMatches() const { return fingerprint_matches; }
    UInt64 getFingerprintFalsePositives() const { return fingerprint_false_positives; }
    UInt64 getFingerprintTruePositives() const { return fingerprint_true_positives; }
    
    // Pointer accessors for stats registration
    UInt64* getFingerprintChecksPtr() { return &fingerprint_checks; }
    UInt64* getFingerprintMatchesPtr() { return &fingerprint_matches; }
    UInt64* getFingerprintFalsePositivesPtr() { return &fingerprint_false_positives; }
    UInt64* getFingerprintTruePositivesPtr() { return &fingerprint_true_positives; }
    
    /// Get number of bits used for fingerprint
    int getFingerprintBits() const { return fingerprint_bits; }
    
    // ============ Region Way Preference Statistics Accessors ============
    UInt64 getRegionWayAttempts() const { return region_way_attempts; }
    UInt64 getRegionWayHits() const { return region_way_hits; }
    UInt64 getRegionWayRelearns() const { return region_way_relearns; }
    
    // Pointer accessors for stats registration
    UInt64* getRegionWayAttemptsPtr() { return &region_way_attempts; }
    UInt64* getRegionWayHitsPtr() { return &region_way_hits; }
    UInt64* getRegionWayRelearnsPtr() { return &region_way_relearns; }
    
    /// Get current number of regions with preferred way set
    UInt64 getRegionWayPrefCount() const {
        std::lock_guard<std::mutex> guard(lock);
        UInt64 count = 0;
        for (const auto& [region_id, pref] : region_way_pref) {
            if (pref.way >= 0) count++;
        }
        return count;
    }
    
    // ============ Global Way Statistics Accessors ============
    int getGlobalWay() const { return global_way; }
    UInt64 getGlobalWayRotations() const { return global_way_rotations; }
    UInt64 getGlobalWayAttempts() const { return global_way_attempts; }
    UInt64 getGlobalWayHits() const { return global_way_hits; }
    UInt64 getGlobalWayFallbacks() const { return global_way_fallbacks; }
    
    // Pointer accessors for stats registration
    UInt64* getGlobalWayRotationsPtr() { return &global_way_rotations; }
    UInt64* getGlobalWayAttemptsPtr() { return &global_way_attempts; }
    UInt64* getGlobalWayHitsPtr() { return &global_way_hits; }
    UInt64* getGlobalWayFallbacksPtr() { return &global_way_fallbacks; }
    
    /// Get free fraction for a specific way
    double getWayFreeFraction(int way) const {
        if (way < 0 || way >= static_cast<int>(associativity)) return 0.0;
        return static_cast<double>(free_sets_per_way[way]) / static_cast<double>(num_sets);
    }
    
    /// Get current global_way free fraction
    double getGlobalWayFreeFraction() const {
        return getWayFreeFraction(global_way);
    }
    
    /// Get congestion threshold
    double getCongestionFreeFrac() const { return congestion_free_frac; }
    
    /// Set congestion threshold (for tuning)
    void setCongestionFreeFrac(double frac) { congestion_free_frac = frac; }
    
    /// Get rotate cooldown max
    UInt32 getRotateCooldownMax() const { return rotate_cooldown_max; }
    
    /// Set rotate cooldown max (for tuning)
    void setRotateCooldownMax(UInt32 max) { rotate_cooldown_max = max; }
    
    /// Debug: verify free_sets_per_way by scanning all sets (slow)
    bool verifyFreeSetsPerWay() const {
        std::lock_guard<std::mutex> guard(lock);
        std::vector<UInt32> actual(associativity, 0);
        for (UInt32 s = 0; s < num_sets; s++) {
            for (UInt32 w = 0; w < associativity; w++) {
                const auto* entry = sets[s].getEntry(w);
                if (!entry || !entry->valid) {
                    actual[w]++;
                }
            }
        }
        for (UInt32 w = 0; w < associativity; w++) {
            if (free_sets_per_way[w] != actual[w]) {
                return false;
            }
        }
        return true;
    }
    
    /**
     * @brief Count total valid entries across all sets
     * @return Number of valid (allocated) entries
     */
    UInt64 countValidEntries() const {
        std::lock_guard<std::mutex> guard(lock);
        UInt64 count = 0;
        for (const auto& set : sets) {
            count += set.countValid();
        }
        return count;
    }
    
    /**
     * @brief Get total capacity (max entries)
     * @return num_sets * associativity
     */
    UInt64 getTotalCapacity() const {
        return num_sets * associativity;
    }
    
    /**
     * @brief Get current utilization as fraction
     * @return valid_entries / total_capacity (0.0 to 1.0)
     */
    double getUtilization() const {
        return static_cast<double>(countValidEntries()) / getTotalCapacity();
    }
};


/*
 * ================================================================================
 * UtopiaAllocator - Main Template Class
 * ================================================================================
 * 
 * The UtopiaAllocator is the top-level allocator that orchestrates RestSegs and
 * FlexSeg to provide hybrid restrictive/flexible address mapping.
 * 
 * MEMORY LAYOUT:
 * --------------
 *   Physical Memory:
 *   +------------------+------------------+-----+------------------+
 *   |    RestSeg 0     |    RestSeg 1     | ... |     FlexSeg      |
 *   |   (e.g., 2MB)    |   (e.g., 1GB)    |     |  (Buddy 4KB)     |
 *   +------------------+------------------+-----+------------------+
 * 
 * ALLOCATION STRATEGY (from paper Section 4.4):
 * ----------------------------------------------
 *   1. Try RestSegs in REVERSE ORDER (largest pages first)
 *      - If a RestSeg has space, allocate there (fast translation)
 *   2. If all RestSegs are full (no free ways in target set):
 *      - Fall back to FlexSeg (buddy allocator, 4KB pages)
 *   3. Page table allocations always go to FlexSeg
 * 
 * PROMOTION/MIGRATION HEURISTICS (from paper Section 4.5):
 * ---------------------------------------------------------
 * Pages can be promoted from FlexSeg to RestSeg based on:
 *   - TLB Heuristic: Pages with high TLB miss counts
 *   - PTE Heuristic: Pages with high access counts in PTEs
 *   - PageFault Heuristic: Pages that cause frequent page faults
 * 
 * When promoting to a full RestSeg set:
 *   - LRU page in that set is evicted to FlexSeg
 *   - Page tables are updated accordingly
 * 
 * TEMPLATE DESIGN:
 * ----------------
 * Policy provides simulator-specific hooks:
 *   - read_config_*(): Configuration file access
 *   - get_num_cores(): Core count for metadata
 *   - on_init(): Stats registration, logging setup
 *   - log(): Debug output
 * 
 * This allows the same allocator code to be used with different
 * simulators or in standalone testing.
 */
template <typename Policy>
class UtopiaAllocator : public PhysicalMemoryAllocator, private Policy
{
    /// Buddy allocator policy derived from the main Policy
    using BuddyPolicy = typename BuddyPolicyFor<Policy>::type;
    /// Buddy allocator type - single allocator for ALL FlexSeg allocations
    using BuddyType = Buddy<BuddyPolicy>;

public:
    // ============ Statistics ============
    /**
     * @brief Statistics tracking for Utopia allocator performance
     * 
     * Allocation hierarchy (priority order):
     *   1. RestSeg-2MB: Fast translation + large pages
     *   2. FlexSeg-THP: Large pages with conventional translation
     *   3. RestSeg-4KB: Fast translation, small pages
     *   4. FlexSeg-4KB: Standard fallback
     */
    struct Stats {
        // RestSeg statistics
        UInt64 restseg_2mb_allocations = 0;  ///< 2MB pages in RestSeg
        UInt64 restseg_4kb_allocations = 0;  ///< 4KB pages in RestSeg
        UInt64 restseg_evictions = 0;        ///< Pages evicted from RestSegs
        
        // FlexSeg statistics  
        UInt64 flexseg_thp_allocations = 0;  ///< 2MB THP in FlexSeg
        UInt64 flexseg_4kb_allocations = 0;  ///< 4KB pages in FlexSeg
        UInt64 flexseg_thp_reserved = 0;     ///< 2MB regions reserved
        UInt64 flexseg_thp_promoted = 0;     ///< 2MB regions promoted to THP
        UInt64 flexseg_thp_demoted = 0;      ///< 2MB regions demoted back to 4KB
        
        // General statistics
        UInt64 total_allocations = 0;        ///< Total allocation requests
        UInt64 migrations = 0;               ///< Pages migrated between segments
        UInt64 coalescing_promotions = 0;    ///< Pages promoted via coalescing
        
        // Legacy compatibility
        UInt64 restseg_allocations = 0;      ///< Total RestSeg allocations
        UInt64 flexseg_allocations = 0;      ///< Total FlexSeg allocations
    };

    // ============ Migration Policy Types ============
    /**
     * @brief Policy types for migration decisions
     * 
     * Simplified: Only None and CostTopK remain. 
     * First-touch allocation is handled by the allocation hierarchy:
     *   RestSeg-2MB → FlexSeg-THP → RestSeg-4KB → FlexSeg-4KB
     */
    enum class MigrationPolicyType {
        None = 0,            ///< No automatic migration  
        CostTopK = 1         ///< Cost-based Top-K migration
    };

    // ============ Cost-based Top-K Migration ============
    /**
     * @brief Entry in the Top-K "expensive VPN" CAM structure
     * 
     * Tracks VPNs with the highest translation costs for migration priority.
     */
    struct CostTopKEntry {
        bool valid = false;           ///< Entry is in use
        UInt64 vpn = 0;               ///< Virtual page number (4KB aligned >> 12)
        UInt16 score_snapshot = 0;    ///< Score at last ranking epoch
        UInt8 sticky = 0;             ///< Stability counter (0-3), higher = more stable
        UInt8 cooldown = 0;           ///< Epochs remaining before eligible for migration
    };

    /**
     * @brief Configuration for Cost-based Top-K migration
     */
    struct CostTopKConfig {
        bool enabled = false;         ///< Master enable
        int page_size_bits = 12;      ///< Only track this page size (12 = 4KB)
        
        // Score computation
        int score_bits = 10;          ///< Bits for saturating counter (max 1023)
        int ptw_base_inc = 1;         ///< Base increment per PTW
        int dram_inc_cap = 3;         ///< Max additional increment for DRAM accesses
        
        // Top-K table
        int topk_entries = 16;        ///< K = number of tracked VPNs
        int sticky_threshold = 2;     ///< Required stickiness to migrate
        int cooldown_epochs = 2;      ///< Epochs before re-eligible
        
        // Epoch configuration
        UInt64 rank_epoch_translations = 1024;   ///< Translations per ranking epoch
        UInt64 credit_epoch_translations = 8192; ///< Translations per credit epoch
        
        // Credit throttling
        int credits_max = 4;          ///< Maximum credits
        int credits_refill = 1;       ///< Credits refilled per credit epoch
        
        // Migration
        int migrate_threshold = 32;   ///< Min score to trigger migration
        int post_migrate_score_shift = 2; ///< score >>= shift after migration
    };

    /**
     * @brief Runtime state for Cost-based Top-K migration
     */
    struct CostTopKState {
        // VPN -> cost score map (10-bit saturating)
        std::unordered_map<UInt64, UInt16> vpn_cost_score;
        
        // Top-K table
        std::vector<CostTopKEntry> topk_table;
        UInt16 topk_min_score = 0;    ///< Minimum score in Top-K (for quick reject)
        int topk_min_idx = -1;        ///< Index of min score entry
        
        // Epoch counters
        UInt64 translation_counter = 0;      ///< Total translations since start
        UInt64 rank_epoch_counter = 0;       ///< Counter for ranking epoch
        UInt64 credit_epoch_counter = 0;     ///< Counter for credit epoch
        
        // Credits
        int credits = 0;                     ///< Current migration credits
        
        // Stats
        UInt64 score_updates = 0;
        UInt64 topk_inserts = 0;
        UInt64 topk_replacements = 0;
        UInt64 migration_attempts = 0;
        UInt64 migrations_issued = 0;
        UInt64 gated_credits = 0;
        UInt64 gated_threshold = 0;
        UInt64 gated_sticky = 0;
        UInt64 gated_cooldown = 0;
        
        // Last migration result
        IntPtr last_migration_ppn = 0;
    };

private:
    Stats stats;                                        ///< Allocation statistics
    std::vector<std::unique_ptr<RestSeg<Policy>>> restsegs; ///< RestSeg instances
    BuddyType* buddy_allocator;                         ///< Single buddy for ALL FlexSeg allocations
    
    int num_restsegs;                    ///< Number of RestSegs configured
    int restseg_2mb_index = -1;          ///< Index of 2MB RestSeg (-1 if none)
    int restseg_4kb_index = -1;          ///< Index of 4KB RestSeg (-1 if none)
    double thp_promotion_threshold;      ///< Threshold for THP promotion (0.0-1.0)
    
    int tlb_eviction_threshold;          ///< TLB miss count threshold for promotion
    int pte_eviction_threshold;          ///< PTE access count threshold for promotion
    
    // ============ THP Tracking (borrowed from ReserveTHP) ============
    // Maps 2MB region index (VA >> 21) to:
    //   - Base PPN of the 2MB region
    //   - Bitset<512> of which 4KB pages are allocated
    //   - Bool indicating if region has been promoted to THP
    std::map<UInt64, std::tuple<UInt64, std::bitset<512>, bool>> thp_regions;
    
    // ============ Migration Policy ============
    MigrationPolicyType migration_policy_type = MigrationPolicyType::None;  ///< Active migration policy
    
    // ============ Last Allocation State ============
    // These track the result of the most recent allocation for
    // the caller to query (useful for page table updates)
    bool last_in_restseg = false;        ///< Was last alloc in RestSeg?
    bool last_caused_eviction = false;   ///< Did last alloc evict a page?
    IntPtr last_evicted_address = 0;     ///< VA of evicted page (if any)
    int last_evicted_page_size_bits = 12; ///< Page size of evicted page (bits)
    
    // ============ Cost-based Top-K Migration State ============
    CostTopKConfig cost_topk_config;     ///< Configuration for cost-based migration
    CostTopKState cost_topk_state;       ///< Runtime state for cost-based migration

    // ============ Migration CSV Logging ============
#if ENABLE_UTOPIA_MIGRATION_CSV
    std::ofstream migration_csv_file;    ///< CSV file for migration logging
#endif

public:
    Stats& getStats() { return stats; }
    const Stats& getStats() const { return stats; }

    /**
     * @brief Construct a UtopiaAllocator
     * @param name Allocator name (for logging/stats)
     * @param memory_size Total physical memory in pages
     * @param max_order Maximum buddy allocator order
     * @param kernel_size Kernel reserved memory in pages
     * @param frag_type Fragmentation pattern for testing
     * 
     * Initialization sequence:
     *   1. Read RestSeg configuration from config file
     *   2. Create RestSeg instances (2MB and 4KB) with their physical memory regions
     *   3. Create single buddy allocator for all FlexSeg allocations
     *   4. Call Policy::on_init() for stats registration
     * 
     * Allocation hierarchy (priority order):
     *   1. RestSeg-2MB: Fast translation + large pages (best)
     *   2. FlexSeg-THP: Large pages with conventional translation (via buddy 2MB reservation)
     *   3. RestSeg-4KB: Fast translation, small pages
     *   4. FlexSeg-4KB: Standard fallback (via buddy 4KB allocation)
     */
    UtopiaAllocator(String name,
                    int memory_size,
                    int max_order,
                    int kernel_size,
                    String frag_type)
        : PhysicalMemoryAllocator(name, memory_size, kernel_size)
    {
        // Compile-time check for buddy policy compatibility
        static_assert(is_complete<BuddyPolicyFor<Policy>>::value,
                      "BuddyPolicyFor<Policy> is incomplete. Include buddy_policy.h first.");
        
        // Read Utopia configuration via policy interface
        num_restsegs = Policy::read_config_int("perf_model/utopia/RestSegs");
        tlb_eviction_threshold = Policy::read_config_int("perf_model/utopia/tlb_eviction_thr");
        pte_eviction_threshold = Policy::read_config_int("perf_model/utopia/pte_eviction_thr");
        thp_promotion_threshold = Policy::read_config_int("perf_model/utopia/thp_promotion_threshold") / 100.0;
        if (thp_promotion_threshold < 0.0 || thp_promotion_threshold > 1.0) {
            thp_promotion_threshold = 0.75;  // Default: promote when 75% of 2MB region is used
        }
        
        int num_cores = Policy::get_num_cores();
        
        std::cout << "[Utopia] Creating buddy allocator..." << std::endl;
        
        // Create single buddy allocator for ALL FlexSeg allocations (both 4KB and 2MB)
        // This is created FIRST so we can use it for RestSeg base allocation
        buddy_allocator = new BuddyType(memory_size, max_order, kernel_size, frag_type);
        
        std::cout << "[Utopia] Buddy allocator created. Creating " << num_restsegs << " RestSegs..." << std::endl;
        
        // Create RestSegs from per-segment configuration arrays
        // RestSeg physical memory comes from buddy allocator (reserved contiguous regions)
        
        // Read global fingerprint_bits config (default 4 bits)
        int fingerprint_bits = 4;
        try {
            fingerprint_bits = Policy::read_config_int("perf_model/utopia/fingerprint_bits");
            if (fingerprint_bits < 1 || fingerprint_bits > 8) {
                std::cout << "[Utopia] Warning: fingerprint_bits must be 1-8, using default 4" << std::endl;
                fingerprint_bits = 4;
            }
        } catch (...) {
            // Config not found, use default
        }
        std::cout << "[Utopia] Using fingerprint_bits=" << fingerprint_bits << std::endl;
        
        // Read fingerprint_enabled config (default true)
        // Use int read since bool might not be supported
        bool fingerprint_enabled = true;
        try {
            int fp_enabled_val = Policy::read_config_int("perf_model/utopia/fingerprint_enabled");
            fingerprint_enabled = (fp_enabled_val != 0);
        } catch (...) {
            // Config not found, use default (enabled)
        }
        std::cout << "[Utopia] Fingerprint filtering " << (fingerprint_enabled ? "ENABLED" : "DISABLED") << std::endl;
        
        for (int i = 0; i < num_restsegs; i++) {
            std::cout << "[Utopia] Reading config for RestSeg[" << i << "]..." << std::endl;
            
            int rs_size = Policy::read_config_int_array("perf_model/utopia/RestSeg/size", i);
            int rs_page_size = Policy::read_config_int_array("perf_model/utopia/RestSeg/page_size", i);
            int rs_assoc = Policy::read_config_int_array("perf_model/utopia/RestSeg/assoc", i);
            
            std::cout << "[Utopia] Creating RestSeg object: size=" << rs_size << " page_size=" << rs_page_size << " assoc=" << rs_assoc << std::endl;
            
            auto rs = std::make_unique<RestSeg<Policy>>(i + 1, rs_size, rs_page_size, rs_assoc, num_cores, fingerprint_bits, fingerprint_enabled);
            
            std::cout << "[Utopia] RestSeg object created, allocating memory..." << std::endl;
            
            // Allocate contiguous physical memory region for this RestSeg from kernel region
            // (not from buddy - kernel region is for OS/RestSeg metadata)
            UInt64 rs_bytes = rs->getSizeMB() * 1024ULL * 1024ULL;
            std::cout << "[Utopia] Requesting " << rs_bytes << " bytes from kernel region..." << std::endl;
            
            UInt64 base_ppn = this->handle_page_table_allocations(rs_bytes);
            
            // For large pages (2MB), align the base to page size boundary
            // This is crucial for correct physical address calculation
            UInt64 page_size_in_4kb_pages = 1ULL << (rs_page_size - 12);  // e.g., 512 for 2MB
            UInt64 aligned_base_ppn = ((base_ppn + page_size_in_4kb_pages - 1) / page_size_in_4kb_pages) * page_size_in_4kb_pages;
            
            std::cout << "[Utopia] Base PPN before alignment: " << base_ppn 
                      << ", after alignment: " << aligned_base_ppn << std::endl;
            
            rs->setBase(aligned_base_ppn);
            
            // Allocate metadata (tag array and fingerprint array) for each core
            // Permission filter is removed - fingerprint array replaces it
            // These need to be in simulated physical address space, not host memory
            UInt64 tag_size = rs->getTagArraySize();
            UInt64 fp_size = rs->getFingerprintArraySize();
            
            std::cout << "[Utopia] Allocating metadata for " << num_cores << " cores: "
                      << "tag_size=" << tag_size << " fp_size=" << fp_size << std::endl;
            
            for (int c = 0; c < num_cores; c++) {
                // Allocate tag array - returns PPN, convert to physical address
                UInt64 tag_ppn = this->handle_page_table_allocations(tag_size);
                IntPtr tag_addr = tag_ppn * 4096;  // Convert PPN to physical address
                
                // Allocate fingerprint array - returns PPN, convert to physical address
                UInt64 fp_ppn = this->handle_page_table_allocations(fp_size);
                IntPtr fp_addr = fp_ppn * 4096;
                
                rs->setMetadataBases(c, tag_addr, fp_addr);
            }
            
            // Set up radix allocator for VPN → {valid, way} tables
            // Capture 'this' to use handle_page_table_allocations for radix node allocation
            auto radix_alloc = [this](size_t size) -> IntPtr {
                UInt64 ppn = this->handle_page_table_allocations(size);
                return static_cast<IntPtr>(ppn * 4096);  // Convert PPN to physical address
            };
            rs->setRadixAllocator(radix_alloc);
            
            std::cout << "[Utopia] RestSeg[" << i << "] created: size=" << rs_size << "MB, page_size=" 
                      << rs_page_size << ", assoc=" << rs_assoc << ", base_ppn=" << aligned_base_ppn << std::endl;
            
            // Track which RestSeg is for which page size
            if (rs_page_size == 21) {  // 2MB = 2^21 bytes
                restseg_2mb_index = i;
            } else if (rs_page_size == 12) {  // 4KB = 2^12 bytes
                restseg_4kb_index = i;
            }
            
            restsegs.push_back(std::move(rs));
        }
        
        // ============ Initialize Cost-based Top-K Migration ============
        // Read config with defaults for backward compatibility
        try {
            int enabled = Policy::read_config_int("perf_model/utopia/migration_cost_topk/enabled");
            cost_topk_config.enabled = (enabled != 0);
            migration_policy_type = cost_topk_config.enabled ? MigrationPolicyType::CostTopK : MigrationPolicyType::None;
        } catch (...) {
            cost_topk_config.enabled = false;
            migration_policy_type = MigrationPolicyType::None;
        }
        
        if (cost_topk_config.enabled) {
            try { cost_topk_config.page_size_bits = Policy::read_config_int("perf_model/utopia/migration_cost_topk/page_size_bits"); }
            catch (...) { cost_topk_config.page_size_bits = 12; }
            
            try { cost_topk_config.score_bits = Policy::read_config_int("perf_model/utopia/migration_cost_topk/score_bits"); }
            catch (...) { cost_topk_config.score_bits = 10; }
            
            try { cost_topk_config.ptw_base_inc = Policy::read_config_int("perf_model/utopia/migration_cost_topk/ptw_base_increment"); }
            catch (...) { cost_topk_config.ptw_base_inc = 1; }
            
            try { cost_topk_config.dram_inc_cap = Policy::read_config_int("perf_model/utopia/migration_cost_topk/dram_weighted_increment_cap"); }
            catch (...) { cost_topk_config.dram_inc_cap = 3; }
            
            try { cost_topk_config.topk_entries = Policy::read_config_int("perf_model/utopia/migration_cost_topk/topk_entries"); }
            catch (...) { cost_topk_config.topk_entries = 16; }
            
            try { cost_topk_config.sticky_threshold = Policy::read_config_int("perf_model/utopia/migration_cost_topk/sticky_threshold"); }
            catch (...) { cost_topk_config.sticky_threshold = 2; }
            
            try { cost_topk_config.cooldown_epochs = Policy::read_config_int("perf_model/utopia/migration_cost_topk/cooldown_epochs"); }
            catch (...) { cost_topk_config.cooldown_epochs = 2; }
            
            try { cost_topk_config.rank_epoch_translations = Policy::read_config_int("perf_model/utopia/migration_cost_topk/ranking_epoch_translations"); }
            catch (...) { cost_topk_config.rank_epoch_translations = 1024; }
            
            try { cost_topk_config.credit_epoch_translations = Policy::read_config_int("perf_model/utopia/migration_cost_topk/credit_epoch_translations"); }
            catch (...) { cost_topk_config.credit_epoch_translations = 8192; }
            
            try { cost_topk_config.credits_max = Policy::read_config_int("perf_model/utopia/migration_cost_topk/credits_max"); }
            catch (...) { cost_topk_config.credits_max = 4; }
            
            try { cost_topk_config.credits_refill = Policy::read_config_int("perf_model/utopia/migration_cost_topk/credits_refill"); }
            catch (...) { cost_topk_config.credits_refill = 1; }
            
            try { cost_topk_config.migrate_threshold = Policy::read_config_int("perf_model/utopia/migration_cost_topk/migrate_threshold"); }
            catch (...) { cost_topk_config.migrate_threshold = 32; }
            
            try { cost_topk_config.post_migrate_score_shift = Policy::read_config_int("perf_model/utopia/migration_cost_topk/post_migrate_score_shift"); }
            catch (...) { cost_topk_config.post_migrate_score_shift = 2; }
            
            // Initialize state
            cost_topk_state.topk_table.resize(cost_topk_config.topk_entries);
            cost_topk_state.credits = cost_topk_config.credits_max;
            
            std::cout << "[Utopia] Cost-based Top-K migration ENABLED: K=" << cost_topk_config.topk_entries
                      << " threshold=" << cost_topk_config.migrate_threshold
                      << " credits_max=" << cost_topk_config.credits_max << std::endl;
        }
        
        // Initialize policy (logging, stats registration)
        Policy::on_init(name, memory_size, kernel_size, this);
        
        this->log("Utopia initialized: RestSeg-2MB idx=", restseg_2mb_index,
                 "RestSeg-4KB idx=", restseg_4kb_index,
                 "THP threshold=", thp_promotion_threshold);

#if ENABLE_UTOPIA_MIGRATION_CSV
        // Open migration CSV file in output directory
        std::string csv_path = Policy::get_output_directory() + "/utopia_migrations.csv";
        migration_csv_file.open(csv_path);
        if (migration_csv_file.is_open()) {
            migration_csv_file << "vpn,old_ppn,new_ppn,evicted_vpn,app_id,target_page_bits" << std::endl;
            std::cout << "[Utopia] Migration CSV logging enabled: " << csv_path << std::endl;
        } else {
            std::cerr << "[Utopia] WARNING: Failed to open " << csv_path << " for migration logging" << std::endl;
        }
#endif
    }

    ~UtopiaAllocator() {
#if ENABLE_UTOPIA_MIGRATION_CSV
        if (migration_csv_file.is_open()) {
            migration_csv_file.close();
        }
#endif
        delete buddy_allocator;
    }
    
    /**
     * @brief Dump all RestSeg RLE statistics to CSV files
     * 
     * Called explicitly before simulation ends to ensure stats are written
     * while the output directory is still valid.
     */
    void dumpAllRLEStats() {
        for (int i = 0; i < num_restsegs; i++) {
            if (restsegs[i]) {
                restsegs[i]->dumpRegionRLEStats();
            }
        }
    }
    
    /**
     * @brief Override base class method to dump final stats
     */
    void dumpFinalStats() override {
        dumpAllRLEStats();
    }

    // ============ THP Helper Functions (borrowed from ReserveTHP) ============
    
protected:
    /**
     * @brief Try to allocate within a 2MB THP region
     * @param address Virtual address to allocate
     * @param core_id Core/application ID
     * @return Pair of (PPN, promoted) where promoted=true if region became full 2MB THP
     * 
     * This implements THP tracking logic borrowed from ReserveTHPAllocator:
     *   - If 2MB region not yet reserved, try to reserve from buddy
     *   - If region exists, mark the 4KB sub-page as allocated
     *   - If utilization exceeds threshold, mark as "promoted" to full 2MB THP
     */
    std::pair<UInt64, bool> tryAllocateTHP(UInt64 address, UInt64 core_id) {
        UInt64 region_2mb = address >> 21;  // 2MB region index
        int offset_4kb = (address >> 12) & 0x1FF;  // 4KB page within 2MB (0-511)
        
        // If region not yet tracked, try to reserve 2MB from buddy
        if (thp_regions.find(region_2mb) == thp_regions.end()) {
            auto reserved = buddy_allocator->reserve_2mb_page(address, core_id);
            if (std::get<0>(reserved) == static_cast<UInt64>(-1)) {
                // Cannot reserve 2MB - caller should fall back to 4KB
                return std::make_pair(static_cast<UInt64>(-1), false);
            }
            
            // Create new THP tracking entry: (base_ppn, bitset<512>, not_promoted)
            thp_regions[region_2mb] = std::make_tuple(std::get<0>(reserved), std::bitset<512>(), false);
            stats.flexseg_thp_reserved++;
            this->log_trace("Reserved 2MB region: region_idx=", region_2mb, 
                           " base_ppn=", std::get<0>(reserved));
        }
        
        auto& region = thp_regions[region_2mb];
        auto& [base_ppn, bitset, promoted] = region;
        
        // If already promoted to full 2MB, shouldn't be hitting page fault
        if (promoted) {
            this->log_debug("THP region already promoted, returning base PPN");
            return std::make_pair(base_ppn, true);
        }
        
        // Check if this 4KB page is already allocated within the region
        // This makes allocation idempotent - same VA returns same PPN
        if (bitset.test(offset_4kb)) {
            this->log_trace("THP: 4KB page already allocated at offset ", offset_4kb,
                           " returning existing PPN=", base_ppn + offset_4kb);
            return std::make_pair(base_ppn + offset_4kb, false);
        }
        
        // Mark this 4KB page as allocated
        bitset.set(offset_4kb);
        
        // Check if we should promote to full 2MB THP
        double utilization = static_cast<double>(bitset.count()) / 512.0;
        if (utilization >= thp_promotion_threshold) {
            promoted = true;
            stats.flexseg_thp_promoted++;
            this->log_debug("THP region promoted: region_idx=", region_2mb,
                           " utilization=", utilization);
            return std::make_pair(base_ppn, true);
        }
        
        // Return specific 4KB page PPN within the 2MB region
        return std::make_pair(base_ppn + offset_4kb, false);
    }
    
    /**
     * @brief Demote the lowest-utilization THP region to free memory
     * @return true if a region was demoted, false if no regions to demote
     * 
     * Used when buddy allocator is out of memory - finds the least-utilized
     * THP region and frees its unused 4KB pages back to buddy.
     */
    bool demoteTHPRegion() {
        std::vector<std::pair<UInt64, double>> utilizations;
        
        for (auto& [region_idx, region] : thp_regions) {
            auto& [base_ppn, bitset, promoted] = region;
            if (promoted) continue;  // Skip fully promoted regions
            
            double util = static_cast<double>(bitset.count()) / 512.0;
            utilizations.push_back({region_idx, util});
        }
        
        if (utilizations.empty()) return false;
        
        // Sort by utilization (ascending) - demote least-used first
        std::sort(utilizations.begin(), utilizations.end(),
                  [](auto& a, auto& b) { return a.second < b.second; });
        
        UInt64 victim_region = utilizations[0].first;
        auto& [base_ppn, bitset, promoted] = thp_regions[victim_region];
        
        // Free unused pages back to buddy
        for (int i = 0; i < 512; i++) {
            if (!bitset[i]) {
                buddy_allocator->free(base_ppn + i, base_ppn + i + 1);
            }
        }
        
        stats.flexseg_thp_demoted++;
        thp_regions.erase(victim_region);
        
        this->log_debug("Demoted THP region: region_idx=", victim_region,
                       " utilization=", utilizations[0].second);
        return true;
    }
    
    /**
     * @brief Check if an address is in a reserved THP region
     * @param address Virtual address to check
     * @return Base PPN of the 2MB region, or -1 if not reserved
     */
    IntPtr isLargePageReserved(IntPtr address) {
        UInt64 region_2mb = address >> 21;
        auto it = thp_regions.find(region_2mb);
        if (it != thp_regions.end()) {
            return std::get<0>(it->second);
        }
        return static_cast<IntPtr>(-1);
    }
    
    /**
     * @brief Clear the THP bit for a specific 4KB page within a 2MB region
     * @param address Virtual address of the page
     * 
     * Called when a page is migrated from FlexSeg (THP) to RestSeg.
     * The 2MB region remains reserved but this specific 4KB page is no longer
     * tracked as being in use within that region.
     */
    void clearTHPBit(IntPtr address) {
        UInt64 region_2mb = address >> 21;
        auto it = thp_regions.find(region_2mb);
        if (it != thp_regions.end()) {
            UInt64 offset_4kb = (address >> 12) & 0x1FF;  // 9 bits for 512 4KB pages
            auto& [base_ppn, bitset, promoted] = it->second;
            bitset.reset(offset_4kb);
            this->log_trace("Cleared THP bit: VA=", SimLog::hex(address), 
                           " region=", region_2mb, " offset=", offset_4kb);
        }
    }
    
    /**
     * @brief Check if an address is currently allocated in any RestSeg
     * @param address Virtual address to check
     * @param app_id Application ID
     * @return true if the page is in a RestSeg, false otherwise
     * 
     * Used to prevent migration of pages that are already in RestSeg.
     */
    bool isInRestSeg(IntPtr address, int app_id) {
        for (int i = 0; i < num_restsegs; i++) {
            if (restsegs[i]->inRestSeg(address, app_id, false)) {
                return true;
            }
        }
        return false;
    }

public:
    // ============ Core Allocation Interface ============

    /**
     * @brief Allocate a physical page for a virtual address
     * @param size Requested size (unused, fixed page sizes)
     * @param address Virtual address being mapped
     * @param core_id Core/application ID
     * @param is_pagetable_allocation True if allocating for page table
     * @return Pair of (physical_page_number, page_size_bits)
     * 
     * NEW Allocation Hierarchy (priority order):
     *   1. RestSeg-2MB: Fast translation + large pages (best case)
     *   2. FlexSeg-THP: Large pages (2MB) with conventional translation
     *   3. RestSeg-4KB: Fast translation, small pages
     *   4. FlexSeg-4KB: Standard fallback (worst case)
     * 
     * Page table allocations always go to buddy allocator (4KB).
     */
    std::pair<UInt64, UInt64> allocate(UInt64 size, UInt64 address = 0, 
                                        UInt64 core_id = -1, 
                                        bool is_pagetable_allocation = false,
                                        bool is_instruction_allocation = false) override
    {
        stats.total_allocations++;
        
        this->log_debug("┌─ allocate(): VA=", SimLog::hex(address), " core=", core_id,
                       " is_pt=", is_pagetable_allocation, " alloc#", stats.total_allocations);
        
        // Page table allocations always go to buddy (need 4KB granularity)
        if (is_pagetable_allocation) {
            auto page = buddy_allocator->allocate(size, address, core_id);
            stats.flexseg_4kb_allocations++;
            stats.flexseg_allocations++;
            this->log_debug("└─ [PT] Page table -> Buddy 4KB, PPN=", page);
            return std::make_pair(page, 12ULL);  // 12 bits = 4KB
        }

        // Instruction allocations go to the reserved instruction area
        if (is_instruction_allocation) {
            return this->allocateInstruction(size);
        }

        // ============ LEVEL 1: Try RestSeg-2MB ============
        // Best case: Fast translation (no page table walk) + large page benefits
        // Translation: PA = RestSeg_base + (set * assoc + way) * 2MB
        if (restseg_2mb_index >= 0) {
            this->log_trace("├─ [L1] Trying RestSeg-2MB (fast translation + 2MB)...");
            // force_evict=false: only use free ways, no eviction on first-touch
            auto [success, evicted, evicted_addr] = restsegs[restseg_2mb_index]->allocate(address, core_id, false);
            (void)evicted; (void)evicted_addr;  // Unused with force_evict=false
            
            if (success) {
                stats.restseg_2mb_allocations++;
                stats.restseg_allocations++;
                last_in_restseg = true;
                last_caused_eviction = false;
                last_evicted_address = 0;
                
                // calculatePhysicalAddress returns PPN in 4KB units
                IntPtr ppn = restsegs[restseg_2mb_index]->calculatePhysicalAddress(address, core_id);
                this->log_debug("└─ [L1] ✓ RestSeg-2MB: VA=", SimLog::hex(address),
                               " -> PPN=", ppn, " (2MB, NO PTW)");
                return std::make_pair(static_cast<UInt64>(ppn), 21ULL);  // 21 bits = 2MB
            }
            this->log_trace("│  └─ [L1] ✗ RestSeg-2MB full (set conflict)");
        } else {
            this->log_trace("├─ [L1] RestSeg-2MB not configured, skipping");
        }
        
        // ============ LEVEL 2: Try FlexSeg-THP (2MB via buddy reservation) ============
        // Second best: Large page benefits, but requires conventional page table walk
        // Uses THP tracking borrowed from ReserveTHPAllocator
        // NOTE: If we reserve a 2MB region, we return immediately (even if not yet promoted)
        //       THP reservation takes precedence over RestSeg-4KB
        this->log_trace("├─ [L2] Trying FlexSeg-THP (2MB with PTW)...");
        {
            auto [phys, promoted] = tryAllocateTHP(address, core_id);
            
            if (phys != static_cast<UInt64>(-1)) {
                last_in_restseg = false;
                last_caused_eviction = false;
                last_evicted_address = 0;
                
                if (promoted) {
                    // Full 2MB page allocated/promoted
                    stats.flexseg_thp_allocations++;
                    stats.flexseg_allocations++;
                    this->log_debug("└─ [L2] ✓ FlexSeg-THP (promoted): VA=", SimLog::hex(address),
                                   " -> PPN=", phys, " (2MB, PTW required)");
                    return std::make_pair(phys, 21ULL);
                } else {
                    // 4KB within a 2MB reservation - return immediately
                    // THP reservation takes precedence over RestSeg-4KB
                    stats.flexseg_4kb_allocations++;
                    stats.flexseg_allocations++;
                    this->log_debug("└─ [L2] ✓ FlexSeg-THP (4KB in reserved 2MB): VA=", SimLog::hex(address),
                                   " -> PPN=", phys, " (4KB, PTW required)");
                    return std::make_pair(phys, 12ULL);
                }
            } else {
                this->log_trace("│  └─ [L2] ✗ FlexSeg-THP reservation failed");
            }
        }
        
        // ============ LEVEL 3: Try RestSeg-4KB ============
        // Third: Fast translation (no page table walk), but small pages
        // Translation: PA = RestSeg_base + (set * assoc + way) * 4KB
        // NOTE: Only reached if L2 (THP reservation) failed
        if (restseg_4kb_index >= 0) {
            this->log_trace("├─ [L3] Trying RestSeg-4KB (fast translation, 4KB)...");
            // force_evict=false: only use free ways, no eviction on first-touch
            auto [success, evicted, evicted_addr] = restsegs[restseg_4kb_index]->allocate(address, core_id, false);
            (void)evicted; (void)evicted_addr;  // Unused with force_evict=false
            
            if (success) {
                stats.restseg_4kb_allocations++;
                stats.restseg_allocations++;
                last_in_restseg = true;
                last_caused_eviction = false;
                last_evicted_address = 0;
                
                // calculatePhysicalAddress returns PPN in 4KB units
                IntPtr ppn = restsegs[restseg_4kb_index]->calculatePhysicalAddress(address, core_id);
                this->log_debug("└─ [L3] ✓ RestSeg-4KB: VA=", SimLog::hex(address),
                               " -> PPN=", ppn, " (4KB, NO PTW)");
                return std::make_pair(static_cast<UInt64>(ppn), 12ULL);  // 12 bits = 4KB
            }
            this->log_trace("│  └─ [L3] ✗ RestSeg-4KB full (set conflict)");
        } else {
            this->log_trace("├─ [L3] RestSeg-4KB not configured, skipping");
        }
        
        // ============ LEVEL 4: FlexSeg-4KB fallback ============
        // Worst case: Small pages with conventional page table walk
        // NOTE: Only reached if both L2 (THP) and L3 (RestSeg-4KB) failed
        //       L2 already tried THP reservation, so go directly to pure 4KB buddy
        this->log_trace("├─ [L4] Falling back to FlexSeg-4KB (pure buddy)...");
        {
            last_in_restseg = false;
            last_caused_eviction = false;
            last_evicted_address = 0;
            
            // Pure 4KB from buddy allocator
            auto page = buddy_allocator->allocate(size, address, core_id);
            if (page != static_cast<UInt64>(-1)) {
                stats.flexseg_4kb_allocations++;
                stats.flexseg_allocations++;
                this->log_debug("└─ [L4] ✓ FlexSeg-4KB: VA=", SimLog::hex(address),
                               " -> PPN=", page, " (4KB, PTW required)");
                return std::make_pair(page, 12ULL);  // 12 bits = 4KB
            }
        }
        
        // Out of memory - this shouldn't happen in normal operation
        this->log("└─ [ERROR] Out of memory! VA=", SimLog::hex(address),
                 " All 4 levels exhausted. Stats: RS-2MB=", stats.restseg_2mb_allocations,
                 " FS-THP=", stats.flexseg_thp_allocations,
                 " RS-4KB=", stats.restseg_4kb_allocations,
                 " FS-4KB=", stats.flexseg_4kb_allocations);
        return std::make_pair(static_cast<UInt64>(-1), 12ULL);
    }

    /**
     * @brief Allocate a range of virtual addresses (not implemented)
     */
    std::vector<Range> allocate_ranges(IntPtr start_va, IntPtr end_va, int app_id) override {
        return std::vector<Range>();  // Not implemented for Utopia
    }

    /**
     * @brief Fast allocation path (directly from buddy 4KB)
     * @param bytes Size in bytes
     * @param address Virtual address (for hash-based placement)
     * @param core_id Core ID
     * @return Physical page number
     * 
     * Bypasses the 4-level hierarchy, directly allocates from buddy.
     * Useful for kernel allocations that need guaranteed 4KB pages.
     */
    UInt64 givePageFast(UInt64 bytes, UInt64 address = 0, UInt64 core_id = -1) override {
        this->log_trace("givePageFast: bypassing hierarchy, direct buddy 4KB");
        return buddy_allocator->allocate(bytes, address, core_id);
    }

    /**
     * @brief Deallocate a page (TODO: implement)
     */
    void deallocate(UInt64 address, UInt64 core_id = -1) override {
        // TODO: Need to check if page is in RestSeg or FlexSeg
        // and handle accordingly
    }

    /**
     * @brief Fragment memory for testing purposes
     * @param target Target fragmentation level (0.0 to 1.0)
     */
    void fragment_memory(double target) override {
        buddy_allocator->fragmentMemory(target);
    }

    // ============ RestSeg Access ============
    
    /**
     * @brief Get the 2MB RestSeg
     * @return Pointer to 2MB RestSeg, or nullptr if not configured
     */
    RestSeg<Policy>* getRestSeg2MB() {
        return (restseg_2mb_index >= 0) ? restsegs[restseg_2mb_index].get() : nullptr;
    }
    
    /**
     * @brief Get the 4KB RestSeg
     * @return Pointer to 4KB RestSeg, or nullptr if not configured
     */
    RestSeg<Policy>* getRestSeg4KB() {
        return (restseg_4kb_index >= 0) ? restsegs[restseg_4kb_index].get() : nullptr;
    }
    
    /**
     * @brief Get a RestSeg by index
     * @param index RestSeg index (0-indexed)
     * @return Pointer to RestSeg, or nullptr if index out of bounds
     */
    RestSeg<Policy>* getRestSeg(int index) {
        return (index >= 0 && index < num_restsegs) ? restsegs[index].get() : nullptr;
    }
    
    /// Get number of configured RestSegs
    int getNumRestSegs() const { return num_restsegs; }
    
    /// Get index of 2MB RestSeg (-1 if not configured)
    int getRestSeg2MBIndex() const { return restseg_2mb_index; }
    
    /// Get index of 4KB RestSeg (-1 if not configured)
    int getRestSeg4KBIndex() const { return restseg_4kb_index; }
    
    /// Get THP promotion threshold
    double getTHPPromotionThreshold() const { return thp_promotion_threshold; }
    
    /// Get TLB miss threshold for promotion
    int getTLBThreshold() const { return tlb_eviction_threshold; }
    /// Get PTE access threshold for promotion
    int getPTEThreshold() const { return pte_eviction_threshold; }
    
    // ============ Last Allocation Info ============
    // These allow the caller to query details about the most recent allocation,
    // which is useful for updating page tables after evictions.
    
    /// Was the last allocation in a RestSeg (vs FlexSeg)?
    bool wasLastInRestSeg() const { return last_in_restseg; }
    /// Alias for compatibility with mmu_utopia.cc
    bool getLastAllocatedInRestSeg() const { return last_in_restseg; }
    /// Did the last allocation cause an eviction?
    bool didLastCauseEviction() const { return last_caused_eviction; }
    /// Get the virtual address of the page evicted by last allocation (if any)
    IntPtr getLastEvictedAddress() const { return last_evicted_address; }
    /// Get the page size (in bits) of the evicted page (from its RestSeg)
    int getLastEvictedPageSizeBits() const { return last_evicted_page_size_bits; }
    
    /// Check if an address is currently in any RestSeg (public wrapper)
    bool checkIsInRestSeg(IntPtr address, int app_id) {
        return isInRestSeg(address, app_id);
    }
    
    // ============ Migration ============
    
    /**
     * @brief Migrate a page from FlexSeg to RestSeg (promotion)
     * @param address Virtual address of the page
     * @param old_ppn Old physical page number (in FlexSeg)
     * @param target_page_size_bits Target page size for RestSeg
     * @param app_id Application/process ID
     * @return New physical address in RestSeg, or 0 if migration failed
     * 
     * This implements page promotion from FlexSeg to RestSeg (from paper
     * Section 4.5). When a page is identified as "hot" by the heuristics,
     * it can be migrated to a RestSeg for faster translation.
     * 
     * Migration steps:
     *   1. Find RestSeg with matching page size
     *   2. Force allocation (evict LRU if set is full)
     *   3. If eviction occurred, place evicted page in FlexSeg
     *   4. Free old physical page from buddy allocator
     *   5. Return new physical address in RestSeg
     * 
     * Note: The caller is responsible for updating page tables and
     * TLB shootdown after migration.
     */
    IntPtr migratePage(IntPtr address, IntPtr old_ppn, int target_page_size_bits, int app_id) {
        this->log_debug("migratePage: VA=", SimLog::hex(address), " old_ppn=", old_ppn,
                       " target_page_bits=", target_page_size_bits, " app=", app_id);
        
        // CRITICAL: Do not migrate pages that are already in RestSeg
        // Migration is only for FlexSeg → RestSeg promotion
        if (isInRestSeg(address, app_id)) {
            this->log_debug("migratePage: VA already in RestSeg, skipping migration");
            return 0;  // Already in RestSeg, no migration needed
        }
        
        // CRITICAL: Do not migrate pages that are in a THP reserved region
        // The 2MB region is already reserved, so migration provides no benefit
        // and would complicate tracking (the region must stay reserved for other pages)
        UInt64 region_2mb = address >> 21;
        if (thp_regions.find(region_2mb) != thp_regions.end()) {
            this->log_debug("migratePage: VA in THP reserved region, skipping migration");
            return 0;  // In THP region, no migration allowed
        }
        
        // Reset eviction tracking
        last_caused_eviction = false;
        last_evicted_address = 0;
        last_evicted_page_size_bits = 12;  // Default to 4KB
        
        // Find RestSeg with matching page size
        for (int i = 0; i < num_restsegs; i++) {
            if (restsegs[i]->getPageSizeBits() == target_page_size_bits) {
                // Force allocation (evict if needed)
                auto [success, evicted, evicted_addr] = restsegs[i]->allocate(address, app_id, true);
                
                if (success) {
                    stats.migrations++;
                    
                    if (evicted) {
                        stats.restseg_evictions++;
                        last_caused_eviction = true;
                        last_evicted_address = evicted_addr;
                        last_evicted_page_size_bits = restsegs[i]->getPageSizeBits();  // Track evicted page's RestSeg page size
                        this->log_debug("Migration caused eviction: evicted_addr=", SimLog::hex(evicted_addr),
                                       " evicted_page_size_bits=", last_evicted_page_size_bits);
                        // Don't allocate from buddy for evicted page - let it page fault naturally
                        // The next access to evicted_addr will trigger a page fault and allocate properly
                    }
                    
                    // Free old physical page back to buddy allocator
                    // Note: We already rejected THP pages at function entry, so this is a pure buddy 4KB page
                    buddy_allocator->free(old_ppn, old_ppn);
                    
                    // Delete page table entry for migrated page (now in RestSeg, no PT needed)
                    // This is Sniper-specific, delegated to policy
                    this->deletePageTableEntry(address, app_id);
                    
                    // calculatePhysicalAddress returns PPN in 4KB units (despite the name)
                    IntPtr new_ppn = restsegs[i]->calculatePhysicalAddress(address, app_id);
                    this->log("Migration success: VA=", SimLog::hex(address), "-> new PPN=", new_ppn,
                             " total_migrations=", stats.migrations);

#if ENABLE_UTOPIA_MIGRATION_CSV
                    // Log migration to CSV: vpn,old_ppn,new_ppn,evicted_vpn,app_id,target_page_bits
                    if (migration_csv_file.is_open()) {
                        UInt64 vpn = address >> 12;
                        UInt64 evicted_vpn = evicted ? (evicted_addr >> 12) : 0;
                        migration_csv_file << "0x" << std::hex << vpn
                                           << ",0x" << old_ppn
                                           << ",0x" << new_ppn
                                           << ",0x" << evicted_vpn
                                           << "," << std::dec << app_id
                                           << "," << target_page_size_bits
                                           << std::endl;
                    }
#endif

                    return new_ppn;  // Return PPN in 4KB units
                }
            }
        }
        this->log_debug("Migration failed: no matching RestSeg or allocation failed");
        return 0;  // Migration failed - no matching RestSeg or allocation failed
    }

    // ============ Policy Accessors ============
    
    /// Get migration policy type (None or CostTopK)
    MigrationPolicyType getMigrationPolicyType() const { return migration_policy_type; }

    // ============ Cost-based Top-K Migration API ============
    
    /**
     * @brief Get cost score for a VPN (from allocator's internal store)
     * @param vpn Virtual page number (address >> 12)
     * @return Cost score (0 if not tracked)
     */
    UInt16 getCostScore(UInt64 vpn) const {
        auto it = cost_topk_state.vpn_cost_score.find(vpn);
        return (it != cost_topk_state.vpn_cost_score.end()) ? it->second : 0;
    }
    
    /**
     * @brief Set cost score for a VPN
     * @param vpn Virtual page number
     * @param score New score value
     */
    void setCostScore(UInt64 vpn, UInt16 score) {
        UInt16 max_score = (1 << cost_topk_config.score_bits) - 1;
        cost_topk_state.vpn_cost_score[vpn] = std::min(score, max_score);
    }
    
    /**
     * @brief Bump cost score for a VPN (saturating add)
     * @param vpn Virtual page number
     * @param inc Increment amount (already capped by caller)
     * @return New score after increment
     */
    UInt16 bumpCostScore(UInt64 vpn, UInt16 inc) {
        UInt16 max_score = (1 << cost_topk_config.score_bits) - 1;
        UInt16 old_score = getCostScore(vpn);
        UInt16 new_score = std::min(static_cast<UInt16>(old_score + inc), max_score);
        cost_topk_state.vpn_cost_score[vpn] = new_score;
        cost_topk_state.score_updates++;
        return new_score;
    }
    
    /**
     * @brief Update the Top-K table with a new VPN and score
     * @param vpn Virtual page number
     * @param score Current cost score
     * @param count_stats Whether to count statistics
     * 
     * Algorithm:
     *   1. If VPN already in Top-K: update score_snapshot, bump sticky
     *   2. Else if score > min_score: replace min slot
     *   3. Recompute min slot by scanning K entries
     */
    void updateCostTopK(UInt64 vpn, UInt16 score, bool count_stats) {
        if (!cost_topk_config.enabled) return;
        
        auto& table = cost_topk_state.topk_table;
        int k = cost_topk_config.topk_entries;
        
        // Check if VPN already in Top-K
        for (int i = 0; i < k; i++) {
            if (table[i].valid && table[i].vpn == vpn) {
                // Update existing entry
                table[i].score_snapshot = score;
                if (table[i].sticky < 3) table[i].sticky++;
                recomputeTopKMin();
                return;
            }
        }
        
        // Not in Top-K - check if should insert
        // First, look for an invalid slot
        for (int i = 0; i < k; i++) {
            if (!table[i].valid) {
                table[i].valid = true;
                table[i].vpn = vpn;
                table[i].score_snapshot = score;
                table[i].sticky = 1;
                table[i].cooldown = 0;
                if (count_stats) cost_topk_state.topk_inserts++;
                recomputeTopKMin();
                return;
            }
        }
        
        // All slots valid - replace min if score is higher
        if (score > cost_topk_state.topk_min_score && cost_topk_state.topk_min_idx >= 0) {
            int idx = cost_topk_state.topk_min_idx;
            table[idx].valid = true;
            table[idx].vpn = vpn;
            table[idx].score_snapshot = score;
            table[idx].sticky = 1;
            table[idx].cooldown = 0;
            if (count_stats) cost_topk_state.topk_replacements++;
            recomputeTopKMin();
        }
    }
    
    /**
     * @brief Process epoch boundaries (ranking decay and credit refill)
     * @param count_stats Whether to count statistics
     * 
     * Called after each translation to check for epoch boundaries.
     */
    void processEpochs(bool count_stats) {
        if (!cost_topk_config.enabled) return;
        
        cost_topk_state.translation_counter++;
        cost_topk_state.rank_epoch_counter++;
        cost_topk_state.credit_epoch_counter++;
        
        // Ranking epoch: decay scores and stickiness
        if (cost_topk_state.rank_epoch_counter >= cost_topk_config.rank_epoch_translations) {
            cost_topk_state.rank_epoch_counter = 0;
            
            for (auto& entry : cost_topk_state.topk_table) {
                if (entry.valid) {
                    entry.score_snapshot >>= 1;  // Decay by half
                    if (entry.sticky > 0) entry.sticky--;
                    if (entry.cooldown > 0) entry.cooldown--;
                }
            }
            recomputeTopKMin();
        }
        
        // Credit epoch: refill credits
        if (cost_topk_state.credit_epoch_counter >= cost_topk_config.credit_epoch_translations) {
            cost_topk_state.credit_epoch_counter = 0;
            cost_topk_state.credits = std::min(
                cost_topk_state.credits + cost_topk_config.credits_refill,
                cost_topk_config.credits_max);
        }
    }
    
    /**
     * @brief Consider migrating a page based on cost-based policy
     * @param va Virtual address being translated (currently faulting)
     * @param current_ppn Current physical page number (in FlexSeg)
     * @param page_size_bits Current page size (must be 12 for 4KB)
     * @param app_id Application ID
     * @param vpn Virtual page number (va >> 12)
     * @param count_stats Whether to count statistics
     * @return Pair of (migrated, new_ppn) - new_ppn valid only if migrated==true
     * 
     * Migration is only attempted if:
     *   - CostTopK is enabled
     *   - Page is 4KB (FlexSeg page)
     *   - VPN is in Top-K table
     *   - Score >= migrate_threshold
     *   - Sticky >= sticky_threshold
     *   - Cooldown == 0
     *   - Credits > 0
     *   
     * On success, updates cooldown, credits, and decays score.
     */
    std::pair<bool, IntPtr> considerCostBasedMigration(IntPtr va, IntPtr current_ppn, int page_size_bits,
                                     int app_id, UInt64 vpn, bool count_stats) {
        if (!cost_topk_config.enabled) return {false, 0};
        if (page_size_bits != cost_topk_config.page_size_bits) return {false, 0};
        
        if (count_stats) cost_topk_state.migration_attempts++;
        
        // Find this VPN in Top-K
        int found_idx = -1;
        for (int i = 0; i < cost_topk_config.topk_entries; i++) {
            if (cost_topk_state.topk_table[i].valid && 
                cost_topk_state.topk_table[i].vpn == vpn) {
                found_idx = i;
                break;
            }
        }
        
        if (found_idx < 0) {
            // VPN not in Top-K - no migration
            return {false, 0};
        }
        
        auto& entry = cost_topk_state.topk_table[found_idx];
        
        // Check gating conditions
        if (entry.score_snapshot < static_cast<UInt16>(cost_topk_config.migrate_threshold)) {
            if (count_stats) cost_topk_state.gated_threshold++;
            return {false, 0};
        }
        
        if (entry.sticky < static_cast<UInt8>(cost_topk_config.sticky_threshold)) {
            if (count_stats) cost_topk_state.gated_sticky++;
            return {false, 0};
        }
        
        if (entry.cooldown > 0) {
            if (count_stats) cost_topk_state.gated_cooldown++;
            return {false, 0};
        }
        
        if (cost_topk_state.credits <= 0) {
            if (count_stats) cost_topk_state.gated_credits++;
            return {false, 0};
        }
        
        // All conditions met - attempt migration to RestSeg
        // Target is typically RestSeg with 4KB pages, but we allow any
        int target_page_size_bits = 12;  // Target 4KB RestSeg
        if (restseg_4kb_index >= 0) {
            target_page_size_bits = restsegs[restseg_4kb_index]->getPageSizeBits();
        } else if (restseg_2mb_index >= 0) {
            // Fall back to 2MB if no 4KB RestSeg
            target_page_size_bits = 21;
        } else {
            // No RestSeg available
            return {false, 0};
        }
        
        IntPtr new_ppn = migratePage(va, current_ppn, target_page_size_bits, app_id);
        
        if (new_ppn != 0) {
            // Migration successful
            cost_topk_state.credits--;
            entry.cooldown = cost_topk_config.cooldown_epochs;
            
            // Decay score after migration
            UInt16 old_score = getCostScore(vpn);
            UInt16 new_score = old_score >> cost_topk_config.post_migrate_score_shift;
            setCostScore(vpn, new_score);
            entry.score_snapshot = new_score;
            
            cost_topk_state.last_migration_ppn = new_ppn;
            if (count_stats) cost_topk_state.migrations_issued++;
            
            this->log_debug("[COST-MIG] migrate vpn=0x", SimLog::hex(vpn),
                           " score=", old_score, "->", new_score,
                           " sticky=", static_cast<int>(entry.sticky),
                           " credits=", cost_topk_state.credits);
            
            return {true, new_ppn};
        }
        
        return {false, 0};
    }
    
    /**
     * @brief Get PPN from last successful cost-based migration
     * @return Physical page number from last migration
     */
    IntPtr getLastMigrationPPN() const {
        return cost_topk_state.last_migration_ppn;
    }
    
    /**
     * @brief Check if Cost-based Top-K migration is enabled
     */
    bool isCostTopKEnabled() const {
        return cost_topk_config.enabled;
    }
    
    /**
     * @brief Get Cost-Top-K configuration (for MMU stats registration)
     */
    const CostTopKConfig& getCostTopKConfig() const { return cost_topk_config; }
    
    /**
     * @brief Get Cost-Top-K state (for MMU stats registration)
     */
    CostTopKState& getCostTopKState() { return cost_topk_state; }
    const CostTopKState& getCostTopKState() const { return cost_topk_state; }

private:
    /**
     * @brief Recompute the minimum score entry in Top-K table
     */
    void recomputeTopKMin() {
        cost_topk_state.topk_min_score = UINT16_MAX;
        cost_topk_state.topk_min_idx = -1;
        
        for (int i = 0; i < cost_topk_config.topk_entries; i++) {
            if (cost_topk_state.topk_table[i].valid) {
                if (cost_topk_state.topk_table[i].score_snapshot < cost_topk_state.topk_min_score) {
                    cost_topk_state.topk_min_score = cost_topk_state.topk_table[i].score_snapshot;
                    cost_topk_state.topk_min_idx = i;
                }
            }
        }
        
        // If no valid entries, set min_score to 0 so any score can enter
        if (cost_topk_state.topk_min_idx < 0) {
            cost_topk_state.topk_min_score = 0;
        }
    }

public:
    // ============ Utility ============
    
    /// Get the underlying buddy allocator (FlexSeg)
    BuddyType* getBuddyAllocator() { return buddy_allocator; }
    
    /// Hash prime used for address hashing (unused in current implementation)
    static const UInt64 HASH_PRIME = 124183;
};

// ============ Default Utopia Type for Sniper ============
// This provides a convenient non-template alias for use in mmu_utopia.cc and other files
// that expect a simple "Utopia" class name.

#include "memory_management/policies/utopia_policy.h"
#include "memory_management/policies/buddy_policy.h"  // For BuddyPolicyFor specialization

/// Default Utopia allocator using Sniper integration policy
using Utopia = UtopiaAllocator<Sniper::Utopia::MetricsPolicy>;
