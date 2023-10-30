
#pragma once

#include "buddy_allocator.h"
#include "physical_memory_allocator.h"
#include "allocation_manager.h"
#include "utopia.h"

class VirtuOS
{

private:
    PhysicalMemoryAllocator *m_memory_allocator; // This is the physical memory allocator
    AllocationManager *m_allocation_manager;     // Keeps track of the virtual to physical mappings
    Utopia *m_utopia;                            // Utopia's logic including allocation during boot time, replacement policy and others

public:
    VirtuOS();
    ~VirtuOS();
    Utopia *getUtopia() { return m_utopia; }
    PhysicalMemoryAllocator *getMemoryAllocator() { return m_memory_allocator; }
    AllocationManager *getAllocationManager() { return m_allocation_manager; }
};