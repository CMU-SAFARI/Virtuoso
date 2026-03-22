#pragma once

/**
 * NUMA-aware Buddy Allocator
 * 
 * Wraps N separate Buddy<Policy> allocator instances, one per NUMA node.
 * Each node has its own physical address range, kernel reservation, 
 * and independent free lists.
 *
 * The NUMA buddy allocator does NOT make placement decisions itself — it 
 * receives a target node_id from the NUMA placement engine and delegates
 * to the appropriate per-node buddy.
 *
 * Key responsibilities:
 *   - One buddy object per NUMA node
 *   - Per-node kernel reservation (first `kernel_reserved_pages` of each node)
 *   - Forwarding allocate/free to the correct per-node buddy
 *   - Aggregating statistics across nodes
 */

#include "memory_management/physical_memory_allocators/buddy.h"
#include "memory_management/numa/numa_policy.h"
#include <vector>
#include <cassert>
#include <iostream>

template <typename BuddyPolicy>
class NumaBuddyAllocator
{
public:
    using BuddyType = Buddy<BuddyPolicy>;

    struct NodeBuddy
    {
        BuddyType* buddy;
        UInt64 base_pfn;           // First PFN of this node (after global kernel)
        UInt64 num_pages;          // Total pages in this node
        UInt64 kernel_pages;       // Pages reserved for kernel on this node
        UInt64 capacity_mb;        // Node capacity in MB
        UInt64 kernel_mb;          // Kernel reservation in MB
        UInt32 node_id;

        NodeBuddy() : buddy(nullptr), base_pfn(0), num_pages(0),
                       kernel_pages(0), capacity_mb(0), kernel_mb(0), node_id(0) {}
    };

    /**
     * Construct a NUMA buddy allocator.
     *
     * @param num_nodes          Number of NUMA nodes
     * @param per_node_mem_mb    Array of per-node memory sizes in MB
     * @param per_node_kernel_mb Array of per-node kernel reservations in MB
     * @param max_order          Maximum buddy order
     * @param frag_type          Fragmentation type string
     * @param global_kernel_mb   Global kernel size (for PFN base offset)
     */
    NumaBuddyAllocator(UInt32 num_nodes,
                        const std::vector<UInt64>& per_node_mem_mb,
                        const std::vector<UInt64>& per_node_kernel_mb,
                        int max_order,
                        const String& frag_type,
                        UInt64 global_kernel_mb = 0)
        : m_num_nodes(num_nodes)
        , m_max_order(max_order)
    {
        assert(num_nodes > 0);
        assert(per_node_mem_mb.size() >= num_nodes);
        assert(per_node_kernel_mb.size() >= num_nodes);

        m_nodes.resize(num_nodes);

        // Calculate per-node PFN bases
        // Layout: [global_kernel | node0 | node1 | ... | nodeN-1]
        UInt64 current_pfn = global_kernel_mb * 1024 / 4; // Convert MB to 4KB pages

        for (UInt32 i = 0; i < num_nodes; ++i)
        {
            auto& node = m_nodes[i];
            node.node_id = i;
            node.capacity_mb = per_node_mem_mb[i];
            node.kernel_mb = per_node_kernel_mb[i];
            node.base_pfn = current_pfn;
            node.num_pages = per_node_mem_mb[i] * 1024 / 4;
            node.kernel_pages = per_node_kernel_mb[i] * 1024 / 4;

            // Create buddy allocator for this node.
            // The buddy sees (capacity_mb) as total memory and (kernel_mb) as kernel.
            node.buddy = new BuddyType(
                static_cast<int>(per_node_mem_mb[i]),
                max_order,
                static_cast<int>(per_node_kernel_mb[i]),
                frag_type
            );

            current_pfn += node.num_pages;

            std::cout << "[NumaBuddy] Node " << i 
                      << ": capacity=" << per_node_mem_mb[i] << "MB"
                      << " kernel=" << per_node_kernel_mb[i] << "MB"
                      << " base_pfn=" << node.base_pfn
                      << " pages=" << node.num_pages
                      << std::endl;
        }
    }

    ~NumaBuddyAllocator()
    {
        for (auto& node : m_nodes)
            delete node.buddy;
    }

    // =========================================================================
    // Core allocation / deallocation — node-directed
    // =========================================================================

    /**
     * Allocate from a specific NUMA node.
     * Returns the global PFN (node-local PFN + node base_pfn).
     * Returns (UInt64)-1 on failure.
     */
    UInt64 allocate(UInt32 node_id, UInt64 bytes, UInt64 address = 0, UInt64 core_id = (UInt64)-1)
    {
        if (node_id >= m_num_nodes) return static_cast<UInt64>(-1);

        auto& node = m_nodes[node_id];
        UInt64 local_pfn = node.buddy->allocate(bytes, address, core_id);

        if (local_pfn == static_cast<UInt64>(-1))
            return static_cast<UInt64>(-1);

        // Convert node-local PFN to global PFN
        return local_pfn + node.base_pfn;
    }

    /**
     * Allocate from any node that has space (used as fallback).
     * Tries nodes in order of least utilization first.
     */
    UInt64 allocateAny(UInt64 bytes, UInt64 address = 0, UInt64 core_id = (UInt64)-1,
                        UInt32* out_node_id = nullptr)
    {
        // Try all nodes, prefer least-utilized
        // Simple: try in order (could be optimized with utilization sorting)
        for (UInt32 i = 0; i < m_num_nodes; ++i)
        {
            UInt64 result = allocate(i, bytes, address, core_id);
            if (result != static_cast<UInt64>(-1))
            {
                if (out_node_id) *out_node_id = i;
                return result;
            }
        }
        return static_cast<UInt64>(-1);
    }

    /**
     * Reserve a 2MB page from a specific NUMA node.
     * Returns the buddy's tuple with global PFN.
     */
    std::tuple<UInt64, UInt64, bool, UInt64> reserve2MBPage(UInt32 node_id,
                                                             UInt64 address, UInt64 core_id)
    {
        if (node_id >= m_num_nodes)
            return std::make_tuple((UInt64)-1, 0, false, 0);

        auto& node = m_nodes[node_id];
        auto result = node.buddy->reserve_2mb_page(address, core_id);

        if (std::get<0>(result) != static_cast<UInt64>(-1))
        {
            // Offset the PFNs by node base
            std::get<0>(result) += node.base_pfn;
            std::get<1>(result) += node.base_pfn;
        }

        return result;
    }

    /**
     * Free pages back to the appropriate node's buddy.
     * Determines the node from the global PFN.
     */
    void free(UInt64 global_start_pfn, UInt64 global_end_pfn)
    {
        UInt32 node_id = getNodeForPFN(global_start_pfn);
        if (node_id >= m_num_nodes) return;

        auto& node = m_nodes[node_id];
        node.buddy->free(global_start_pfn - node.base_pfn,
                          global_end_pfn - node.base_pfn);
    }

    // =========================================================================
    // Fragmentation
    // =========================================================================

    void fragmentMemory(UInt32 node_id, double target_fragmentation)
    {
        if (node_id < m_num_nodes)
            m_nodes[node_id].buddy->fragmentMemory(target_fragmentation);
    }

    void fragmentAllNodes(double target_fragmentation)
    {
        for (UInt32 i = 0; i < m_num_nodes; ++i)
            m_nodes[i].buddy->fragmentMemory(target_fragmentation);
    }

    void fragmentMemoryToTargetCount(UInt32 node_id, UInt64 target_free_2mb_pages)
    {
        if (node_id < m_num_nodes)
            m_nodes[node_id].buddy->fragmentMemoryToTargetCount(target_free_2mb_pages);
    }

    // =========================================================================
    // Queries
    // =========================================================================

    UInt32 getNodeForPFN(UInt64 global_pfn) const
    {
        for (UInt32 i = 0; i < m_num_nodes; ++i)
        {
            if (global_pfn >= m_nodes[i].base_pfn &&
                global_pfn < m_nodes[i].base_pfn + m_nodes[i].num_pages)
                return i;
        }
        return 0; // fallback
    }

    UInt64 getNodeBasePFN(UInt32 node_id) const
    {
        if (node_id >= m_num_nodes) return 0;
        return m_nodes[node_id].base_pfn;
    }

    UInt64 getNodeNumPages(UInt32 node_id) const
    {
        if (node_id >= m_num_nodes) return 0;
        return m_nodes[node_id].num_pages;
    }

    UInt64 getNodeKernelPages(UInt32 node_id) const
    {
        if (node_id >= m_num_nodes) return 0;
        return m_nodes[node_id].kernel_pages;
    }

    /// Free pages usable for data allocation on this node
    UInt64 getNodeUsablePages(UInt32 node_id) const
    {
        if (node_id >= m_num_nodes) return 0;
        return m_nodes[node_id].num_pages - m_nodes[node_id].kernel_pages;
    }

    UInt64 getNodeFreePages(UInt32 node_id) const
    {
        if (node_id >= m_num_nodes) return 0;
        return m_nodes[node_id].buddy->getFreePages();
    }

    UInt64 getNodeFreeLargePages(UInt32 node_id) const
    {
        if (node_id >= m_num_nodes) return 0;
        return m_nodes[node_id].buddy->getFreeLargePageCount();
    }

    UInt64 getTotalFreePages() const
    {
        UInt64 total = 0;
        for (UInt32 i = 0; i < m_num_nodes; ++i)
            total += m_nodes[i].buddy->getFreePages();
        return total;
    }

    UInt32 getNumNodes() const { return m_num_nodes; }

    BuddyType* getNodeBuddy(UInt32 node_id)
    {
        if (node_id >= m_num_nodes) return nullptr;
        return m_nodes[node_id].buddy;
    }

    const NodeBuddy& getNode(UInt32 node_id) const { return m_nodes[node_id]; }

    /// Check if a global PFN is free in its node's buddy
    bool checkIfFree(UInt64 global_pfn, bool allocate_it = false)
    {
        UInt32 node_id = getNodeForPFN(global_pfn);
        if (node_id >= m_num_nodes) return false;
        return m_nodes[node_id].buddy->checkIfFree(
            global_pfn - m_nodes[node_id].base_pfn, allocate_it);
    }

private:
    UInt32 m_num_nodes;
    int m_max_order;
    std::vector<NodeBuddy> m_nodes;
};
