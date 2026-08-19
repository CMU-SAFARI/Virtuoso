#pragma once


// mimicos/src/
#include "physical_allocator/policies/buddy_policy.h"
#include "physical_allocator/policies/reserve_thp_policy.h"
#include "physical_allocator/policies/baseline_allocator_policy.h"
#include "physical_allocator/policies/linux_buddy_anon_policy.h"

// We include the relevant headers from sniper/include/ - we added the core
// implementations of the allocators there to be accessible from both MimicOS and Sniper

#include "memory_management/physical_memory_allocators/reserve_thp.h"
#include "memory_management/physical_memory_allocators/baseline.h"
#include "memory_management/physical_memory_allocators/linux_buddy_anon.h"

// C++ libs
#include <string>
#include <iostream>

#include "globals.h"

using VirtuosoBaselineAllocator       = BaselineAllocator<Virtuoso::Baseline::NoMetricsPolicy>;
using VirtuosoTHPAllocator            = ReservationTHPAllocator<Virtuoso::ReserveTHP::NoMetricsPolicy>;
using VirtuosoLinuxBuddyAnonAllocator = LinuxBuddyAnonAllocator<Virtuoso::LinuxBuddyAnon::NoMetricsPolicy>;

class AllocatorFactory
{
public:
    /* threshold_for_promotion is a *fraction* of a 2 MiB region's 4 KiB pages
       (ReservationTHPAllocator compares it against a utilisation ratio), so it
       must be a double.  It was previously an int, which silently truncated
       every fractional threshold to the next lower integer — 0.5 became 0,
       i.e. "promote on first touch".  Widening is backward compatible: the
       standalone kernel passes an integer read via GetInteger. */
    static PhysicalMemoryAllocator *createAllocator(String allocator_name, int memory_size, int max_order, int kernel_size, String frag_type, double threshold_for_promotion = -1)
    {
        
        if (mimicos_log::enabled()) std::cout << "[MimicOS] [createAllocator] Creating allocator: " << allocator_name << std::endl;

        if (allocator_name == "baseline")
        {
            if (mimicos_log::enabled()) std::cout << "[MimicOS] [createAllocator] Created VirtuosoBaselineAllocator" << std::endl;
            return new VirtuosoBaselineAllocator(allocator_name, memory_size, max_order, kernel_size, frag_type);
        }
        else if (allocator_name == "reserve_thp")
        {
            if (mimicos_log::enabled()) std::cout << "[MimicOS] [createAllocator] Created VirtuosoTHPAllocator" << std::endl;
            return new VirtuosoTHPAllocator(allocator_name, memory_size, max_order, kernel_size, frag_type, threshold_for_promotion);
        }
        else if (allocator_name == "linux_buddy_anon")
        {
            if (mimicos_log::enabled()) std::cout << "[MimicOS] [createAllocator] Created VirtuosoLinuxBuddyAnonAllocator" << std::endl;
            return new VirtuosoLinuxBuddyAnonAllocator(allocator_name, memory_size, max_order, kernel_size, frag_type);
        }
        else
        {
            std::cout << "[MimicOS/AllocatorFactory] allocator_name = " << allocator_name <<
                         " was not migrated yet ..." << std::endl;
            std::cout << "[MimicOS/AllocatorFactory] allocator_name.size() = " << allocator_name.size() << std::endl;
            return nullptr;
        }
        
    }
};