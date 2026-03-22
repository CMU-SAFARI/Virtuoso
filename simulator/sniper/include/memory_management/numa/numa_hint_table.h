#pragma once

/**
 * NumaHintTable — Radix tree mapping VPN → {valid, NUMA node ID}
 *
 * Analogous to RadixWayTable in Utopia, but the leaf payload is a 2-bit
 * NUMA node identifier (supporting up to 4 nodes) instead of a way index.
 *
 * DESIGN
 * ------
 *   - Internal nodes: RadixNode-style, 512-child (9-bit radix), 4KB each
 *   - Leaf nodes:     4KB, packed {valid:1, node_id:NUMA_BITS} entries
 *   - Default NUMA_BITS = 2  →  3 bits/entry  →  10922 entries/leaf (4KB)
 *   - At 4KB pages this covers ~42 MB of VA per leaf
 *   - Sparse allocation: nodes allocated on demand
 *   - Each node/leaf has a simulated physical address for cache-latency modeling
 *
 * LEAF SIZING
 * -----------
 *   bits_per_entry = 1 (valid) + NUMA_BITS (node_id)
 *   leaf_entries   = floor(32768 / bits_per_entry)
 *   Examples (4KB pages):
 *     NUMA_BITS=2: entry=3 bits, 10922 entries (42.7 MB per leaf)
 *     NUMA_BITS=3: entry=4 bits,  8192 entries (32 MB per leaf)
 *     NUMA_BITS=4: entry=5 bits,  6553 entries (25.6 MB per leaf)
 *
 * INDEXING
 * --------
 *   leaf_index      = vpn % leaf_entries
 *   vpn_above_leaf  = vpn / leaf_entries
 *   Upper levels: 9-bit chunks on vpn_above_leaf (same as RadixWayTable)
 *
 * Based on the RadixWayTable / RadixLeaf design from Utopia (Kanellopoulos+ MICRO'23)
 */

#include "fixed_types.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <vector>

// ============================================================================
// Aligned allocation helpers (identical to utopia.h)
// ============================================================================
namespace numa_hint_detail {

inline void* aligned_alloc_node(size_t size, size_t alignment) {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0)
        ptr = nullptr;
    return ptr;
}

inline void aligned_free_node(void* ptr) {
    free(ptr);
}

} // namespace numa_hint_detail

// ============================================================================
// NumaHintNode — 512-child internal radix node (identical structure to RadixNode)
// ============================================================================
struct alignas(64) NumaHintNode {
    static constexpr int RADIX_BITS = 9;
    static constexpr int FANOUT     = 1 << RADIX_BITS;  // 512

    std::array<NumaHintNode*, FANOUT> children;
    bool   is_leaf       = false;
    IntPtr sim_phys_addr = 0;   ///< Simulated PA for cache-latency modeling

    NumaHintNode() : is_leaf(false), sim_phys_addr(0) {
        children.fill(nullptr);
    }

    ~NumaHintNode() {
        if (!is_leaf) {
            for (auto* child : children)
                delete child;
        }
    }

    static void* operator new(size_t size) {
        void* p = numa_hint_detail::aligned_alloc_node(size, 64);
        if (!p) throw std::bad_alloc();
        return p;
    }
    static void operator delete(void* p) {
        numa_hint_detail::aligned_free_node(p);
    }
};

// ============================================================================
// NumaHintLeaf — 4KB leaf storing packed {valid:1, node_id:NUMA_BITS} entries
// ============================================================================
class alignas(4096) NumaHintLeaf : public NumaHintNode {
public:
    static constexpr int LEAF_BYTES = 4096;
    static constexpr int LEAF_BITS  = LEAF_BYTES * 8;   // 32768

private:
    int m_numa_bits;        ///< Bits for NUMA node ID (default 2)
    int m_bits_per_entry;   ///< 1 + m_numa_bits
    int m_leaf_entries;     ///< floor(LEAF_BITS / m_bits_per_entry)
    std::array<uint8_t, LEAF_BYTES> m_data;

public:
    explicit NumaHintLeaf(int numa_bits)
        : m_numa_bits(numa_bits)
        , m_bits_per_entry(1 + numa_bits)
    {
        is_leaf = true;
        m_leaf_entries = LEAF_BITS / m_bits_per_entry;
        m_data.fill(0);
    }

    int getLeafEntries() const { return m_leaf_entries; }
    int getNumaBits()    const { return m_numa_bits; }

    /**
     * Read entry at @p index.
     * @param[out] valid    Whether a mapping exists at this index
     * @param[out] node_id  NUMA node ID (meaningful only when valid)
     */
    void getEntry(int index, bool& valid, int& node_id) const {
        if (index < 0 || index >= m_leaf_entries) {
            valid = false; node_id = 0; return;
        }
        int bit_pos    = index * m_bits_per_entry;
        int byte_pos   = bit_pos / 8;
        int bit_offset = bit_pos % 8;

        uint32_t raw = m_data[byte_pos];
        if (byte_pos + 1 < LEAF_BYTES)
            raw |= static_cast<uint32_t>(m_data[byte_pos + 1]) << 8;

        raw >>= bit_offset;
        uint32_t mask  = (1u << m_bits_per_entry) - 1;
        uint32_t entry = raw & mask;

        // Format: [valid:1][node_id:m_numa_bits]  (valid is MSB)
        valid   = (entry >> m_numa_bits) & 1;
        node_id = entry & ((1 << m_numa_bits) - 1);
    }

    /**
     * Write entry at @p index.
     */
    void setEntry(int index, bool valid, int node_id) {
        if (index < 0 || index >= m_leaf_entries) return;

        uint32_t entry = (node_id & ((1 << m_numa_bits) - 1))
                       | (valid ? (1u << m_numa_bits) : 0);

        int bit_pos    = index * m_bits_per_entry;
        int byte_pos   = bit_pos / 8;
        int bit_offset = bit_pos % 8;

        uint32_t mask       = (1u << m_bits_per_entry) - 1;
        uint32_t clear_mask = ~(mask << bit_offset);
        uint32_t set_mask   = (entry & mask) << bit_offset;

        m_data[byte_pos] = (m_data[byte_pos] & (clear_mask & 0xFF))
                         | (set_mask & 0xFF);
        if (byte_pos + 1 < LEAF_BYTES)
            m_data[byte_pos + 1] = (m_data[byte_pos + 1] & ((clear_mask >> 8) & 0xFF))
                                 | ((set_mask >> 8) & 0xFF);
    }

    void clearEntry(int index) { setEntry(index, false, 0); }

    /// Cache-line offset for latency modeling
    int getCacheLineOffset(int index) const {
        int bit_pos  = index * m_bits_per_entry;
        int byte_pos = bit_pos / 8;
        return (byte_pos / 64) * 64;
    }

    // --- Self-test -------------------------------------------------------
    bool selfTest() const {
        NumaHintLeaf tl(m_numa_bits);
        int max_node = (1 << m_numa_bits) - 1;
        for (int idx : {0, 1, m_leaf_entries/2, m_leaf_entries-1}) {
            if (idx >= m_leaf_entries) continue;
            for (int n : {0, 1, max_node}) {
                tl.setEntry(idx, true, n);
                bool v; int nout;
                tl.getEntry(idx, v, nout);
                if (!v || nout != n) return false;
            }
            tl.clearEntry(idx);
            bool v; int nout;
            tl.getEntry(idx, v, nout);
            if (v) return false;
        }
        return true;
    }

    static void* operator new(size_t size) {
        void* p = numa_hint_detail::aligned_alloc_node(size, 4096);
        if (!p) throw std::bad_alloc();
        return p;
    }
    static void operator delete(void* p) {
        numa_hint_detail::aligned_free_node(p);
    }
};

// ============================================================================
// NumaHintTable — Sparse radix tree   VPN → {valid, node_id}
// ============================================================================
class NumaHintTable {
public:
    static constexpr int RADIX_BITS = 9;
    static constexpr int MAX_LEVELS = 4;

private:
    NumaHintNode* m_root = nullptr;
    int m_numa_bits;          ///< 2 for ≤4 nodes, 3 for ≤8, …
    int m_leaf_entries;       ///< entries per 4KB leaf
    int m_page_size_bits;     ///< 12 for 4KB pages
    int m_num_internal_levels;
    mutable std::mutex m_lock;

    // --- stats ---
    mutable UInt64 m_lookups      = 0;
    mutable UInt64 m_lookup_hits  = 0;
    mutable UInt64 m_lookup_misses = 0;
    UInt64 m_insertions  = 0;
    UInt64 m_deletions   = 0;
    UInt64 m_nodes_alloc = 0;
    UInt64 m_leaves_alloc = 0;

    // optional physical-page allocator for latency modeling
    std::function<IntPtr(size_t)> m_alloc_phys_page;

public:
    /**
     * @param numa_bits       Bits per NUMA node id (2 = up to 4 nodes)
     * @param page_size_bits  12 for 4KB, 21 for 2MB
     * @param alloc_fn        Optional PA allocator (for cache latency modeling of radix-node pages)
     */
    NumaHintTable(int numa_bits = 2,
                  int page_size_bits = 12,
                  std::function<IntPtr(size_t)> alloc_fn = nullptr)
        : m_numa_bits(numa_bits)
        , m_page_size_bits(page_size_bits)
        , m_alloc_phys_page(alloc_fn)
    {
        int bits_per_entry = 1 + m_numa_bits;
        m_leaf_entries = NumaHintLeaf::LEAF_BITS / bits_per_entry;

        // Compute internal-level depth
        UInt64 max_vpn         = 1ULL << (48 - m_page_size_bits);
        UInt64 max_above_leaf  = (max_vpn + m_leaf_entries - 1) / m_leaf_entries;
        int bits_needed = 0;
        for (UInt64 t = max_above_leaf; t > 0; t >>= 1) bits_needed++;
        m_num_internal_levels = (bits_needed + RADIX_BITS - 1) / RADIX_BITS;
        if (m_num_internal_levels < 1) m_num_internal_levels = 1;
        if (m_num_internal_levels > MAX_LEVELS) m_num_internal_levels = MAX_LEVELS;

        static bool printed = false;
        if (!printed) {
            printed = true;
            fprintf(stderr, "[NumaHintTable] numa_bits=%d entry_bits=%d leaf_entries=%d "
                            "leaf_va_span=%lluMB internal_levels=%d\n",
                    m_numa_bits, bits_per_entry, m_leaf_entries,
                    (unsigned long long)(m_leaf_entries * (1ULL << m_page_size_bits)) / (1024*1024),
                    m_num_internal_levels);
        }
    }

    ~NumaHintTable() { delete m_root; }

    NumaHintTable(const NumaHintTable&) = delete;
    NumaHintTable& operator=(const NumaHintTable&) = delete;

    int getLeafEntries()      const { return m_leaf_entries; }
    int getNumInternalLevels() const { return m_num_internal_levels; }
    int getNumLevels()        const { return m_num_internal_levels + 1; }

    // ------------ index helpers -------------------------------------------
    void computeIndices(UInt64 vpn, int& leaf_index, UInt64& vpn_above_leaf) const {
        leaf_index     = vpn % m_leaf_entries;
        vpn_above_leaf = vpn / m_leaf_entries;
    }

    int getLevelIndex(UInt64 vpn_above_leaf, int level) const {
        int shift = (m_num_internal_levels - 1 - level) * RADIX_BITS;
        return (vpn_above_leaf >> shift) & ((1 << RADIX_BITS) - 1);
    }

    // ------------ lookup --------------------------------------------------
    /**
     * Walk the radix and return the NUMA node hint for @p vpn.
     * @param[out] node_id_out  NUMA node ID when found
     * @return true if a valid hint exists
     */
    bool lookup(UInt64 vpn, int& node_id_out) const {
        std::lock_guard<std::mutex> guard(m_lock);
        m_lookups++;
        node_id_out = 0;

        if (!m_root) { m_lookup_misses++; return false; }

        int leaf_idx;
        UInt64 above;
        computeIndices(vpn, leaf_idx, above);

        NumaHintNode* node = m_root;
        for (int l = 0; l < m_num_internal_levels; ++l) {
            node = node->children[getLevelIndex(above, l)];
            if (!node) { m_lookup_misses++; return false; }
        }
        if (!node->is_leaf) { m_lookup_misses++; return false; }

        auto* leaf = static_cast<NumaHintLeaf*>(node);
        bool valid;
        leaf->getEntry(leaf_idx, valid, node_id_out);
        if (valid) { m_lookup_hits++; return true; }
        m_lookup_misses++;
        return false;
    }

    // ------------ insert / update ----------------------------------------
    /**
     * Set (or update) the NUMA node hint for @p vpn.
     */
    void set(UInt64 vpn, int node_id) {
        std::lock_guard<std::mutex> guard(m_lock);
        m_insertions++;

        int leaf_idx;
        UInt64 above;
        computeIndices(vpn, leaf_idx, above);

        if (!m_root) {
            m_root = new NumaHintNode();
            if (m_alloc_phys_page) m_root->sim_phys_addr = m_alloc_phys_page(4096);
            m_nodes_alloc++;
        }

        NumaHintNode* node = m_root;
        for (int l = 0; l < m_num_internal_levels; ++l) {
            int idx = getLevelIndex(above, l);
            if (!node->children[idx]) {
                if (l == m_num_internal_levels - 1) {
                    auto* leaf = new NumaHintLeaf(m_numa_bits);
                    if (m_alloc_phys_page) leaf->sim_phys_addr = m_alloc_phys_page(4096);
                    node->children[idx] = leaf;
                    m_leaves_alloc++;
                } else {
                    auto* n = new NumaHintNode();
                    if (m_alloc_phys_page) n->sim_phys_addr = m_alloc_phys_page(4096);
                    node->children[idx] = n;
                    m_nodes_alloc++;
                }
            }
            node = node->children[idx];
        }
        static_cast<NumaHintLeaf*>(node)->setEntry(leaf_idx, true, node_id);
    }

    // ------------ clear ---------------------------------------------------
    void clear(UInt64 vpn) {
        std::lock_guard<std::mutex> guard(m_lock);
        m_deletions++;
        if (!m_root) return;

        int leaf_idx;
        UInt64 above;
        computeIndices(vpn, leaf_idx, above);

        NumaHintNode* node = m_root;
        for (int l = 0; l < m_num_internal_levels; ++l) {
            node = node->children[getLevelIndex(above, l)];
            if (!node) return;
        }
        if (!node->is_leaf) return;
        static_cast<NumaHintLeaf*>(node)->clearEntry(leaf_idx);
    }

    // ------------ walk-latency helper ------------------------------------
    /**
     * Collect the simulated physical addresses along the radix-walk path
     * for a given VPN (for cache-latency modeling). Returns up to
     * num_internal_levels + 1 addresses.
     */
    std::vector<IntPtr> getWalkAddresses(UInt64 vpn) const {
        std::vector<IntPtr> addrs;
        if (!m_root) return addrs;

        int leaf_idx;
        UInt64 above;
        computeIndices(vpn, leaf_idx, above);

        NumaHintNode* node = m_root;
        addrs.push_back(node->sim_phys_addr);

        for (int l = 0; l < m_num_internal_levels; ++l) {
            node = node->children[getLevelIndex(above, l)];
            if (!node) break;
            addrs.push_back(node->sim_phys_addr);  // leaf addr included here
        }
        return addrs;
    }

    // ------------ stats ---------------------------------------------------
    UInt64 getLookups()      const { return m_lookups; }
    UInt64 getLookupHits()   const { return m_lookup_hits; }
    UInt64 getLookupMisses() const { return m_lookup_misses; }
    UInt64 getInsertions()   const { return m_insertions; }
    UInt64 getNodesAllocated()  const { return m_nodes_alloc; }
    UInt64 getLeavesAllocated() const { return m_leaves_alloc; }
};
