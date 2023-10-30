#pragma once

#include "physical_memory_allocator.h"
#include "buddy_allocator.h"
#include "simple_thp_allocator.h"

class AllocatorFactory
{
public:
    static PhysicalMemoryAllocator *createAllocator(String allocator_name, int max_order)
    {
        if (allocator_name == "buddy")
        {
            return new BuddyAllocator(max_order);
        }
        else if (allocator_name == "simple_thp")
        {
            return new SimpleTHPAllocator(max_order);
        }
        else
        {
            return nullptr;
        }
    }
};