#pragma once

/**
 * NUMA Policy Engine
 * 
 * Implements Linux-like NUMA memory placement policies:
 *   - LOCAL:       First-touch; bias allocation to CPU-local node
 *   - BIND:        Strict nodemask; allocate only from bound nodes
 *   - INTERLEAVE:  Deterministic round-robin striping across nodes
 *
 * Additionally provides:
 *   - Adaptive per-hash/per-node saturating counters for speculation
 *   - Compact per-page node hints (2-4 bits per page) for bounded fanout
 *   - Confidence tracking for automatic counter-only vs hint-guided mode
 *
 * NUMA-aware allocation policy for Virtuoso
 */

#include "fixed_types.h"
#include "subsecond_time.h"
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <algorithm>
#include <cassert>
#include <cstring>

// =============================================================================
// NUMA Policy Enum
// =============================================================================

enum class NumaPolicy : uint8_t
{
    LOCAL       = 0,   // MPOL_LOCAL: First-touch, bias to CPU-local node
    BIND        = 1,   // MPOL_BIND: Strict nodemask allocation
    INTERLEAVE  = 2,   // MPOL_INTERLEAVE: Deterministic round-robin across nodes
    PREFERRED   = 3,   // MPOL_PREFERRED: Prefer a node, spill to others
    NUM_POLICIES
};

static inline const char* numaPolicyToString(NumaPolicy p)
{
    switch (p) {
        case NumaPolicy::LOCAL:      return "LOCAL";
        case NumaPolicy::BIND:       return "BIND";
        case NumaPolicy::INTERLEAVE: return "INTERLEAVE";
        case NumaPolicy::PREFERRED:  return "PREFERRED";
        default:                     return "UNKNOWN";
    }
}

// =============================================================================
// NUMA Node Configuration
// =============================================================================

struct NumaNodeConfig
{
    UInt32 node_id;
    UInt64 capacity_bytes;        // Total capacity of this node
    UInt64 kernel_reserved_bytes; // Kernel-reserved portion for CPU-local apps
    UInt64 start_pfn;             // Start physical frame number for this node
    UInt64 end_pfn;               // End physical frame number
    UInt64 used_bytes;            // Currently used bytes
    String memory_type;           // "ddr", "cxl", etc.

    // Per-node statistics
    UInt64 alloc_count;
    UInt64 access_count;

    NumaNodeConfig()
        : node_id(0), capacity_bytes(0), kernel_reserved_bytes(0)
        , start_pfn(0), end_pfn(0), used_bytes(0), memory_type("ddr")
        , alloc_count(0), access_count(0) {}

    double utilization() const {
        if (capacity_bytes == 0) return 0.0;
        return static_cast<double>(used_bytes) / capacity_bytes;
    }

    UInt64 free_bytes() const {
        return (used_bytes < capacity_bytes) ? (capacity_bytes - used_bytes) : 0;
    }

    UInt64 usable_capacity() const {
        return capacity_bytes - kernel_reserved_bytes;
    }
};

// =============================================================================
// Adaptive Per-Node Saturating Counter (for speculation)
// =============================================================================

/**
 * Per-hash, per-node saturating counter for ranking candidate NUMA nodes.
 * 
 * Update rule (from paper):
 *   hit:  counter += delta_plus   (capped at max_value)
 *   miss: counter -= delta_minus  (floored at 0)
 * 
 * Stability condition: node n is stable if p_n > p* = delta_minus / (delta_plus + delta_minus)
 * With defaults (delta_plus=2, delta_minus=1): p* = 1/3
 */
class AdaptiveNodeCounter
{
public:
    AdaptiveNodeCounter()
        : m_num_nodes(0), m_delta_plus(2), m_delta_minus(1), m_max_value(255)
        , m_confidence_threshold(32) {}

    void init(UInt32 num_nodes, UInt32 num_hashes,
              int delta_plus = 2, int delta_minus = 1,
              int max_value = 255, int confidence_threshold = 32)
    {
        m_num_nodes = num_nodes;
        m_delta_plus = delta_plus;
        m_delta_minus = delta_minus;
        m_max_value = max_value;
        m_confidence_threshold = confidence_threshold;

        // counters[hash_idx][node_id]
        m_counters.assign(num_hashes, std::vector<int>(num_nodes, 0));
    }

    /// Record a speculation outcome: was the resolved PFN on node `actual_node`?
    void update(UInt32 hash_idx, UInt32 speculated_node, UInt32 actual_node)
    {
        if (hash_idx >= m_counters.size()) return;
        auto& row = m_counters[hash_idx];

        if (speculated_node < m_num_nodes)
        {
            if (speculated_node == actual_node)
                row[speculated_node] = std::min(row[speculated_node] + m_delta_plus, m_max_value);
            else
                row[speculated_node] = std::max(row[speculated_node] - m_delta_minus, 0);
        }

        // Shadow-update: boost actual node if different
        if (actual_node < m_num_nodes && actual_node != speculated_node)
        {
            row[actual_node] = std::min(row[actual_node] + m_delta_plus, m_max_value);
        }
    }

    /// Get top-k nodes by counter score for a given hash
    std::vector<UInt32> getTopKNodes(UInt32 hash_idx, UInt32 k) const
    {
        if (hash_idx >= m_counters.size())
            return {};

        const auto& row = m_counters[hash_idx];
        // Build (score, node_id) pairs, sort descending
        std::vector<std::pair<int, UInt32>> scored;
        scored.reserve(m_num_nodes);
        for (UInt32 n = 0; n < m_num_nodes; ++n)
            scored.emplace_back(row[n], n);

        std::sort(scored.begin(), scored.end(),
                  [](const auto& a, const auto& b){ return a.first > b.first; });

        std::vector<UInt32> result;
        for (UInt32 i = 0; i < std::min(k, (UInt32)scored.size()); ++i)
            result.push_back(scored[i].second);
        return result;
    }

    /// Is the counter confident? (gap between top-1 and top-2 exceeds threshold)
    bool isConfident(UInt32 hash_idx) const
    {
        if (hash_idx >= m_counters.size() || m_num_nodes < 2)
            return true; // trivially confident with 0-1 nodes

        const auto& row = m_counters[hash_idx];
        int top1 = 0, top2 = 0;
        for (UInt32 n = 0; n < m_num_nodes; ++n)
        {
            if (row[n] > top1) { top2 = top1; top1 = row[n]; }
            else if (row[n] > top2) { top2 = row[n]; }
        }
        return (top1 - top2) >= m_confidence_threshold;
    }

    int getScore(UInt32 hash_idx, UInt32 node_id) const
    {
        if (hash_idx >= m_counters.size() || node_id >= m_num_nodes) return 0;
        return m_counters[hash_idx][node_id];
    }

private:
    UInt32 m_num_nodes;
    int m_delta_plus;
    int m_delta_minus;
    int m_max_value;
    int m_confidence_threshold;
    std::vector<std::vector<int>> m_counters;  // [hash_idx][node_id]
};

// =============================================================================
// Compact Per-Page Node Hints
// =============================================================================

/**
 * Per-page node hints: 2-4 bits per 4KB page.
 * Stored in packed format: one 64B cache line encodes 256 pages (1MB coverage) at 2 bits/page.
 *
 * Hints are OS-maintained metadata, consulted only when counter confidence is low.
 * Stale/wrong hints reduce hit rate but never affect correctness.
 */
class PageNodeHints
{
public:
    PageNodeHints() : m_bits_per_hint(2), m_mask(0x3) {}

    void init(UInt32 bits_per_hint, UInt64 max_pages)
    {
        m_bits_per_hint = bits_per_hint;
        m_mask = (1u << bits_per_hint) - 1;

        // Pack hints into bytes: 8 / bits_per_hint hints per byte
        UInt64 num_bytes = (max_pages * bits_per_hint + 7) / 8;
        m_data.assign(num_bytes, 0);
    }

    /// Set the node hint for a page
    void setHint(UInt64 page_num, UInt32 node_id)
    {
        UInt64 bit_offset = page_num * m_bits_per_hint;
        UInt64 byte_idx = bit_offset / 8;
        UInt32 bit_idx = bit_offset % 8;

        if (byte_idx >= m_data.size()) return;

        // Clear old bits, set new
        m_data[byte_idx] &= ~(m_mask << bit_idx);
        m_data[byte_idx] |= ((node_id & m_mask) << bit_idx);
    }

    /// Get the node hint for a page
    UInt32 getHint(UInt64 page_num) const
    {
        UInt64 bit_offset = page_num * m_bits_per_hint;
        UInt64 byte_idx = bit_offset / 8;
        UInt32 bit_idx = bit_offset % 8;

        if (byte_idx >= m_data.size()) return 0;

        return (m_data[byte_idx] >> bit_idx) & m_mask;
    }

    /// Clear hint (on unmap/free)
    void clearHint(UInt64 page_num)
    {
        setHint(page_num, 0);
    }

    UInt32 getBitsPerHint() const { return m_bits_per_hint; }

private:
    UInt32 m_bits_per_hint;
    UInt32 m_mask;
    std::vector<uint8_t> m_data;
};

// =============================================================================
// NUMA Placement Engine
// =============================================================================

/**
 * Central NUMA placement engine that encapsulates all policy logic.
 * Used by NUMA-aware allocators and the tiered DRAM controller.
 *
 * Workflow (from paper):
 *   Stage 1 (eligibility): policy defines allowed node set N_policy(VPN)
 *   Stage 2 (selection for speculation): select candidate set S(VPN) ⊆ N_policy(VPN)
 *                                        using confidence-driven hybrid predictor
 */
class NumaPlacementEngine
{
public:
    NumaPlacementEngine()
        : m_num_nodes(0)
        , m_policy(NumaPolicy::LOCAL)
        , m_utilization_threshold(0.9)
        , m_interleave_counter(0)
        , m_max_speculation_nodes(3)
    {}

    /**
     * Initialize the NUMA placement engine.
     * @param num_nodes       Number of NUMA nodes
     * @param policy          Placement policy
     * @param num_hashes      Number of hash functions (for hash-based allocators)
     * @param util_threshold  Utilization threshold for spill decisions
     * @param max_spec_nodes  Maximum nodes to speculate on (k)
     */
    void init(UInt32 num_nodes, NumaPolicy policy,
              UInt32 num_hashes = 1,
              double util_threshold = 0.9,
              UInt32 max_spec_nodes = 3)
    {
        m_num_nodes = num_nodes;
        m_policy = policy;
        m_utilization_threshold = util_threshold;
        m_max_speculation_nodes = max_spec_nodes;

        m_nodes.resize(num_nodes);
        m_bind_mask.assign(num_nodes, true); // default: all nodes allowed
        m_preferred_node = 0;

        // Initialize adaptive counters
        m_counters.init(num_nodes, num_hashes, /*delta_plus=*/2, /*delta_minus=*/1);

        // Initialize node hints (2 bits per page, 64M pages = 256GB coverage)
        UInt64 max_pages = 64ULL * 1024 * 1024;
        UInt32 bits_per_hint = (num_nodes <= 4) ? 2 : 4;
        m_node_hints.init(bits_per_hint, max_pages);
    }

    // =========================================================================
    // Node configuration
    // =========================================================================

    void configureNode(UInt32 node_id, UInt64 capacity_bytes,
                       UInt64 kernel_reserved_bytes,
                       UInt64 start_pfn, UInt64 end_pfn,
                       const String& memory_type)
    {
        if (node_id >= m_num_nodes) return;
        auto& n = m_nodes[node_id];
        n.node_id = node_id;
        n.capacity_bytes = capacity_bytes;
        n.kernel_reserved_bytes = kernel_reserved_bytes;
        n.start_pfn = start_pfn;
        n.end_pfn = end_pfn;
        n.memory_type = memory_type;
    }

    void setPolicy(NumaPolicy policy) { m_policy = policy; }
    NumaPolicy getPolicy() const { return m_policy; }

    void setBindMask(const std::vector<bool>& mask) { m_bind_mask = mask; }
    void setPreferredNode(UInt32 node_id) { m_preferred_node = node_id; }
    void setUtilizationThreshold(double t) { m_utilization_threshold = t; }
    void setMaxSpeculationNodes(UInt32 k) { m_max_speculation_nodes = k; }

    UInt32 getNumNodes() const { return m_num_nodes; }
    const NumaNodeConfig& getNodeConfig(UInt32 node_id) const { return m_nodes[node_id]; }
    NumaNodeConfig& getNodeConfigMut(UInt32 node_id) { return m_nodes[node_id]; }

    // =========================================================================
    // Stage 1: Policy-based eligible node set
    // =========================================================================

    /**
     * Get the set of eligible NUMA nodes for a given VPN under the current policy.
     * @param vpn          Virtual page number
     * @param cpu_local_node  The CPU-local node for first-touch
     * @return  Vector of eligible node IDs
     */
    std::vector<UInt32> getEligibleNodes(UInt64 vpn, UInt32 cpu_local_node = 0) const
    {
        std::vector<UInt32> eligible;

        switch (m_policy)
        {
        case NumaPolicy::LOCAL:
        {
            // First-touch: prefer CPU-local node, spill to others if full
            if (cpu_local_node < m_num_nodes &&
                m_nodes[cpu_local_node].utilization() < m_utilization_threshold)
            {
                eligible.push_back(cpu_local_node);
            }
            else
            {
                // Spill: find least-utilized node
                for (UInt32 i = 0; i < m_num_nodes; ++i)
                    if (m_nodes[i].utilization() < m_utilization_threshold)
                        eligible.push_back(i);
                // If all above threshold, allow all
                if (eligible.empty())
                    for (UInt32 i = 0; i < m_num_nodes; ++i)
                        eligible.push_back(i);
            }
            break;
        }
        case NumaPolicy::BIND:
        {
            // Strict nodemask: only bound nodes
            for (UInt32 i = 0; i < m_num_nodes; ++i)
                if (i < m_bind_mask.size() && m_bind_mask[i])
                    eligible.push_back(i);
            if (eligible.empty())
                eligible.push_back(0); // fallback
            break;
        }
        case NumaPolicy::INTERLEAVE:
        {
            // Deterministic: compute target node from VPN
            UInt32 target = static_cast<UInt32>(vpn % m_num_nodes);
            eligible.push_back(target);
            break;
        }
        case NumaPolicy::PREFERRED:
        {
            // Prefer a specific node, fallback to others
            if (m_preferred_node < m_num_nodes &&
                m_nodes[m_preferred_node].utilization() < m_utilization_threshold)
            {
                eligible.push_back(m_preferred_node);
            }
            else
            {
                for (UInt32 i = 0; i < m_num_nodes; ++i)
                    if (m_nodes[i].utilization() < m_utilization_threshold)
                        eligible.push_back(i);
                if (eligible.empty())
                    eligible.push_back(m_preferred_node);
            }
            break;
        }
        default:
            eligible.push_back(0);
        }

        return eligible;
    }

    // =========================================================================
    // Stage 2: Speculation-aware node selection
    // =========================================================================

    /**
     * Select candidate nodes for speculation (S(VPN) ⊆ N_policy(VPN)).
     * Uses confidence-driven hybrid predictor:
     *   - When counters are confident: use counters alone
     *   - When confidence degrades: consult per-page node hints
     * 
     * @param vpn           Virtual page number
     * @param hash_idx      Hash function index (for multi-hash allocators)
     * @param cpu_local_node CPU-local node ID
     * @return Vector of candidate node IDs (bounded by max_speculation_nodes)
     */
    std::vector<UInt32> selectSpeculationNodes(UInt64 vpn, UInt32 hash_idx,
                                                UInt32 cpu_local_node = 0) const
    {
        // Get eligible set from policy
        auto eligible = getEligibleNodes(vpn, cpu_local_node);

        if (eligible.size() <= 1)
            return eligible;  // Trivial: only one candidate

        // Check counter confidence
        if (m_counters.isConfident(hash_idx))
        {
            // Counter-only selection: pick top-k from eligible set
            auto top_k = m_counters.getTopKNodes(hash_idx, m_max_speculation_nodes);
            std::vector<UInt32> result;
            for (UInt32 node : top_k)
            {
                // Only include if in eligible set
                for (UInt32 e : eligible)
                    if (e == node) { result.push_back(node); break; }
                if (result.size() >= m_max_speculation_nodes) break;
            }
            if (result.empty())
                result.push_back(eligible[0]);
            return result;
        }
        else
        {
            // Hint-guided: consult per-page hint
            UInt32 hinted_node = m_node_hints.getHint(vpn);
            std::vector<UInt32> result;

            // Add hinted node if in eligible set
            for (UInt32 e : eligible)
            {
                if (e == hinted_node) {
                    result.push_back(hinted_node);
                    break;
                }
            }

            // Fill remaining slots from counter ranking
            auto top_k = m_counters.getTopKNodes(hash_idx, m_max_speculation_nodes);
            for (UInt32 node : top_k)
            {
                if (result.size() >= m_max_speculation_nodes) break;
                bool already_in = false;
                for (UInt32 r : result) if (r == node) { already_in = true; break; }
                if (already_in) continue;
                for (UInt32 e : eligible)
                    if (e == node) { result.push_back(node); break; }
            }

            if (result.empty())
                result.push_back(eligible[0]);
            return result;
        }
    }

    // =========================================================================
    // Allocation: select a single node for placement
    // =========================================================================

    /**
     * Select the target NUMA node for a new allocation.
     * @param vpn            Virtual page number  
     * @param cpu_local_node CPU-local node
     * @return Target node ID
     */
    UInt32 selectAllocationNode(UInt64 vpn, UInt32 cpu_local_node = 0)
    {
        switch (m_policy)
        {
        case NumaPolicy::LOCAL:
        {
            if (cpu_local_node < m_num_nodes &&
                m_nodes[cpu_local_node].utilization() < m_utilization_threshold)
                return cpu_local_node;
            // Spill to least-utilized
            return getLeastUtilizedNode();
        }
        case NumaPolicy::BIND:
        {
            // Find least-utilized among bound nodes
            UInt32 best = 0;
            double best_util = 1.1;
            for (UInt32 i = 0; i < m_num_nodes; ++i)
            {
                if (i < m_bind_mask.size() && m_bind_mask[i] &&
                    m_nodes[i].utilization() < best_util)
                {
                    best = i;
                    best_util = m_nodes[i].utilization();
                }
            }
            return best;
        }
        case NumaPolicy::INTERLEAVE:
        {
            // Deterministic round-robin from VPN
            return static_cast<UInt32>(vpn % m_num_nodes);
        }
        case NumaPolicy::PREFERRED:
        {
            if (m_preferred_node < m_num_nodes &&
                m_nodes[m_preferred_node].utilization() < m_utilization_threshold)
                return m_preferred_node;
            return getLeastUtilizedNode();
        }
        default:
            return 0;
        }
    }

    // =========================================================================
    // Feedback / learning
    // =========================================================================

    /// Record speculation outcome for counter updates
    void recordSpeculationOutcome(UInt32 hash_idx, UInt32 speculated_node, UInt32 actual_node)
    {
        m_counters.update(hash_idx, speculated_node, actual_node);
    }

    /// Set page node hint (on allocation or migration)
    void setNodeHint(UInt64 vpn, UInt32 node_id)
    {
        m_node_hints.setHint(vpn, node_id);
    }

    /// Clear page node hint (on unmap/free)
    void clearNodeHint(UInt64 vpn)
    {
        m_node_hints.clearHint(vpn);
    }

    /// Get current node hint for a page
    UInt32 getNodeHint(UInt64 vpn) const
    {
        return m_node_hints.getHint(vpn);
    }

    /// Record allocation on a node (update used_bytes)
    void recordAllocation(UInt32 node_id, UInt64 bytes)
    {
        if (node_id < m_num_nodes)
        {
            m_nodes[node_id].used_bytes += bytes;
            m_nodes[node_id].alloc_count++;
        }
    }

    /// Record deallocation on a node
    void recordDeallocation(UInt32 node_id, UInt64 bytes)
    {
        if (node_id < m_num_nodes && m_nodes[node_id].used_bytes >= bytes)
            m_nodes[node_id].used_bytes -= bytes;
    }

    /// Record access on a node
    void recordAccess(UInt32 node_id)
    {
        if (node_id < m_num_nodes)
            m_nodes[node_id].access_count++;
    }

    // =========================================================================
    // Utilization-based helpers
    // =========================================================================

    double getNodeUtilization(UInt32 node_id) const
    {
        if (node_id >= m_num_nodes) return 0.0;
        return m_nodes[node_id].utilization();
    }

    UInt32 getLeastUtilizedNode() const
    {
        UInt32 best = 0;
        double best_util = 1.1;
        for (UInt32 i = 0; i < m_num_nodes; ++i)
        {
            double u = m_nodes[i].utilization();
            if (u < best_util)
            {
                best = i;
                best_util = u;
            }
        }
        return best;
    }

    /// Get node ID from a physical frame number
    UInt32 getNodeForPFN(UInt64 pfn) const
    {
        for (UInt32 i = 0; i < m_num_nodes; ++i)
        {
            if (pfn >= m_nodes[i].start_pfn && pfn < m_nodes[i].end_pfn)
                return i;
        }
        return 0; // fallback
    }

    // =========================================================================
    // INTERLEAVE-specific: per-region descriptor cache
    // =========================================================================

    struct InterleaveDescriptor
    {
        UInt64 base_vpn;      // Base VPN of the region
        UInt64 region_pages;  // Number of pages in this region
        std::vector<UInt32> nodemask;  // Participating nodes
        UInt32 offset;        // Interleave state/offset

        UInt32 getNodeForVPN(UInt64 vpn) const
        {
            if (nodemask.empty()) return 0;
            UInt64 page_idx = vpn - base_vpn;
            return nodemask[page_idx % nodemask.size()];
        }
    };

    void setInterleaveDescriptor(UInt64 base_vpn, UInt64 region_pages,
                                  const std::vector<UInt32>& nodemask)
    {
        InterleaveDescriptor desc;
        desc.base_vpn = base_vpn;
        desc.region_pages = region_pages;
        desc.nodemask = nodemask;
        desc.offset = 0;
        m_interleave_descriptors[base_vpn] = desc;
    }

private:
    UInt32 m_num_nodes;
    NumaPolicy m_policy;
    double m_utilization_threshold;
    UInt32 m_interleave_counter;
    UInt32 m_max_speculation_nodes;

    std::vector<NumaNodeConfig> m_nodes;
    std::vector<bool> m_bind_mask;
    UInt32 m_preferred_node;

    AdaptiveNodeCounter m_counters;
    PageNodeHints m_node_hints;

    // INTERLEAVE descriptor cache (indexed by region base VPN)
    std::unordered_map<UInt64, InterleaveDescriptor> m_interleave_descriptors;
};
