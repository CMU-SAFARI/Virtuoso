#include "physical_memory_allocator.h"
#include "simulator.h"
#include "config.hpp"

PhysicalMemoryAllocator::PhysicalMemoryAllocator()
{
    init();
}

void PhysicalMemoryAllocator::init()
{
    m_memory_size = (UInt64)Sim()->getCfg()->getInt("perf_model/pmem_alloc/memory_size");
}
