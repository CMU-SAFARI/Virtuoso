#pragma once

#include "simulator.h"
#include "config.hpp"

// NOTE!! buddy_policy.h should be included before reserve_thp.h
// common/system/memory_management/policies
#include "memory_management/policies/buddy_policy.h"           
#include "memory_management/policies/reserve_thp_policy.h"           
#include "memory_management/policies/baseline_policy.h"
#include "memory_management/policies/spot_policy.h"
#include "memory_management/policies/asap_policy.h"
#include "memory_management/policies/utopia_policy.h"
#include "memory_management/policies/utopia_coalesce_policy.h"
#include "memory_management/policies/numa_reserve_thp_policy.h"

// include/memory_management/physical_memory_allocators/
#include "../../include/memory_management/physical_memory_allocators/reserve_thp.h"
#include "../../include/memory_management/physical_memory_allocators/baseline.h"
#include "../../include/memory_management/physical_memory_allocators/spot.h"
#include "../../include/memory_management/physical_memory_allocators/asap.h"
#include "../../include/memory_management/physical_memory_allocators/utopia.h"
#include "../../include/memory_management/physical_memory_allocators/utopia_hash_coalesce.h"
#include "../../include/memory_management/physical_memory_allocators/numa_reserve_thp.h"
#include "../../include/memory_management/physical_memory_allocators/eager_paging.h"
// PhysicalMemoryAllocator*
// TODO @vlnitu: discuss w/ @kanellok maybe we want to append "include/" to INCLUDE_DIRS in Makefile
#include "../../include/memory_management/physical_memory_allocators/physical_memory_allocator.h"

#include "memory_management/numa/numa_policy.h"
#include <sstream>


using SniperBaselineAllocator = BaselineAllocator<Sniper::Baseline::MetricsPolicy>;
using SniperReserveTHPAllocator      = ReservationTHPAllocator<Sniper::ReserveTHP::MetricsPolicy>;
using SniperSpotAllocator = SpotAllocator<Sniper::Spot::SpotSniperPolicy>;
using SniperASAPAllocator = ASAPAllocator<Sniper::ASAP::MetricsPolicy>; // ASAP uses same base class as ReserveTHP
using SniperUtopiaAllocator = UtopiaAllocator<Sniper::Utopia::MetricsPolicy>;
using SniperUtopiaCoalesceAllocator = UtopiaHashCoalesce<Sniper::UtopiaCoalesce::MetricsPolicy>;

// NUMA-aware allocator types
using SniperNumaReserveTHPAllocator = NumaReservationTHPAllocator<Sniper::NumaReserveTHP::NumaReserveTHPSniperPolicy>;
using SniperEagerPagingAllocator = EagerPagingAllocator<Sniper::EagerPaging::MetricsPolicy>;

// --------------------------------------------------------------------------
// Helper: parse a comma-separated integer list from a config string
// --------------------------------------------------------------------------
static inline std::vector<UInt64> parseCommaSeparatedUInt64(const String& raw)
{
    std::vector<UInt64> result;
    std::stringstream ss(raw.c_str());
    std::string token;
    while (std::getline(ss, token, ','))
    {
        // Trim whitespace
        size_t start = token.find_first_not_of(" \t");
        size_t end = token.find_last_not_of(" \t");
        if (start != std::string::npos)
            result.push_back(std::stoull(token.substr(start, end - start + 1)));
    }
    return result;
}

static inline NumaPolicy parseNumaPolicy(const String& raw)
{
    std::string s(raw.c_str());
    for (auto& c : s) c = std::tolower(c);
    if (s == "local")       return NumaPolicy::LOCAL;
    if (s == "bind")        return NumaPolicy::BIND;
    if (s == "interleave")  return NumaPolicy::INTERLEAVE;
    if (s == "preferred")   return NumaPolicy::PREFERRED;
    std::cout << "[MimicOS] Unknown NUMA policy: " << raw << ", defaulting to LOCAL" << std::endl;
    return NumaPolicy::LOCAL;
}

class AllocatorFactory
{
public:
    static PhysicalMemoryAllocator *createAllocator(String mimicos_name)
    {

        String allocator_name = Sim()->getCfg()->getString("perf_model/" + mimicos_name + "/memory_allocator_name");
        String allocator_type = Sim()->getCfg()->getString("perf_model/" + mimicos_name + "/memory_allocator_type");
        std::cout << "[MimicOS] [createAllocator] Creating physical memory allocator for " << mimicos_name <<
                     " - allocator_name = " << allocator_name <<
                    "  - allocator_type = " << allocator_type <<  std::endl;

        UInt64 memory_size = (UInt64)Sim()->getCfg()->getInt("perf_model/" + allocator_name + "/memory_size");
        UInt64 kernel_size = Sim()->getCfg()->getInt("perf_model/" + allocator_name + "/kernel_size");

        std::cout << "[MimicOS] Kernel size in MB: " << kernel_size << std::endl;
      
        if (allocator_type == "reserve_thp")
        { 
            // Based on FreeBSD's reservation-based THP allocator

            String frag_type = Sim()->getCfg()->getString("perf_model/" + allocator_name + "/frag_type");
            int max_order = Sim()->getCfg()->getInt("perf_model/" + allocator_name + "/max_order");
            float threshold_for_promotion = Sim()->getCfg()->getFloat("perf_model/" + allocator_name + "/threshold_for_promotion");
            return new SniperReserveTHPAllocator(allocator_type, memory_size, max_order, kernel_size, frag_type, threshold_for_promotion);
        }
        else if (allocator_type == "baseline")
        {
            String frag_type = Sim()->getCfg()->getString("perf_model/" + allocator_name + "/frag_type");
            int max_order = Sim()->getCfg()->getInt("perf_model/" + allocator_name + "/max_order");
            return new SniperBaselineAllocator(allocator_type, memory_size, max_order, kernel_size, frag_type);
        } 
        else if (allocator_type == "spot")
        {
            // Based on Spot [Alverti+  ISCA 2020]
            String frag_type = Sim()->getCfg()->getString("perf_model/" + allocator_name + "/frag_type");
            int max_order = Sim()->getCfg()->getInt("perf_model/" + allocator_name + "/max_order");
            return new SniperSpotAllocator(allocator_name, memory_size, max_order, kernel_size, frag_type);
        }
        else if (allocator_type == "asap")
        { // Based on ASAP [Margaritov+ MICRO 2019]
            String frag_type = Sim()->getCfg()->getString("perf_model/" + allocator_name + "/frag_type");
            int max_order = Sim()->getCfg()->getInt("perf_model/" + allocator_name + "/max_order");
            float threshold_for_promotion = Sim()->getCfg()->getFloat("perf_model/" + allocator_name + "/threshold_for_promotion");
            return new SniperASAPAllocator(allocator_name, memory_size, max_order, kernel_size, frag_type, threshold_for_promotion);
        }
        else if (allocator_type == "utopia")
        { // Based on [Kanellopoulos+  MICRO '23]
            String frag_type = Sim()->getCfg()->getString("perf_model/" + allocator_name + "/frag_type");
            int max_order = Sim()->getCfg()->getInt("perf_model/" + allocator_name + "/max_order");
            return new SniperUtopiaAllocator(allocator_name, memory_size, max_order, kernel_size, frag_type);
        }
        else if (allocator_type == "utopia_coalesce")
        { // Utopia with implicit 2MB coalescing via bitmap intersection
            String frag_type = Sim()->getCfg()->getString("perf_model/" + allocator_name + "/frag_type");
            int max_order = Sim()->getCfg()->getInt("perf_model/" + allocator_name + "/max_order");
            return new SniperUtopiaCoalesceAllocator(allocator_name, memory_size, max_order, kernel_size, frag_type);
        }
        else if (allocator_type == "numa_reserve_thp")
        { // NUMA-aware Reservation-based THP allocator
            String frag_type = Sim()->getCfg()->getString("perf_model/" + allocator_name + "/frag_type");
            int max_order = Sim()->getCfg()->getInt("perf_model/" + allocator_name + "/max_order");
            float threshold_for_promotion = Sim()->getCfg()->getFloat("perf_model/" + allocator_name + "/threshold_for_promotion");

            // NUMA-specific parameters
            UInt32 num_numa_nodes = (UInt32)Sim()->getCfg()->getInt("perf_model/" + allocator_name + "/num_numa_nodes");
            NumaPolicy numa_policy = parseNumaPolicy(Sim()->getCfg()->getString("perf_model/" + allocator_name + "/numa_policy"));
            double utilization_threshold = Sim()->getCfg()->getFloat("perf_model/" + allocator_name + "/utilization_threshold");
            std::vector<UInt64> per_node_cap = parseCommaSeparatedUInt64(Sim()->getCfg()->getString("perf_model/" + allocator_name + "/per_node_capacity_mb"));
            std::vector<UInt64> per_node_kern = parseCommaSeparatedUInt64(Sim()->getCfg()->getString("perf_model/" + allocator_name + "/per_node_kernel_mb"));

            std::cout << "[MimicOS] Creating NUMA ReserveTHP: " << num_numa_nodes << " nodes, policy=" 
                      << numaPolicyToString(numa_policy) << std::endl;

            UInt32 cores_total = Sim()->getConfig()->getApplicationCores();

            return new SniperNumaReserveTHPAllocator(
                allocator_name, memory_size, max_order, kernel_size, frag_type,
                threshold_for_promotion,
                num_numa_nodes, numa_policy, utilization_threshold,
                per_node_cap, per_node_kern,
                cores_total);
        }
        else if (allocator_type == "eager_paging")
        {
            String frag_type = Sim()->getCfg()->getString("perf_model/" + allocator_name + "/frag_type");
            int max_order = Sim()->getCfg()->getInt("perf_model/" + allocator_name + "/max_order");
            return new SniperEagerPagingAllocator(allocator_name, memory_size, max_order, kernel_size, frag_type);
        }
        else
        {
            std::cout << "[Sniper] Allocator not found" << std::endl;
            return nullptr;
        }
    }
};
