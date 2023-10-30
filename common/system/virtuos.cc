#include "virtuos.h"
#include "config.hpp"
#include "allocator_factory.h"

VirtuOS::VirtuOS()
{

    String allocator_name = Sim()->getCfg()->getString("perf_model/virtuos/memory_allocator");
    int max_order = Sim()->getCfg()->getInt("perf_model/pmem_alloc/max_order"); // We set the maximum order of the buddy allocator to 21 (2^21 * 4KB = 8GB)
    m_memory_allocator = AllocatorFactory::createAllocator(allocator_name, max_order);
    m_allocation_manager = new AllocationManager(false, 50);

    bool utopia_enabled = Sim()->getCfg()->getBool("perf_model/utopia/enabled");
    double target_fragmentation = Sim()->getCfg()->getFloat("perf_model/pmem_alloc/target_fragmentation"); // How much fragmentation to incur at boot time to mimic the real system
    double target_memory = Sim()->getCfg()->getFloat("perf_model/pmem_alloc/target_memory");               // How much memory to allocate at boot time to mimic the real system

    if (utopia_enabled)
    {
        m_utopia = new Utopia();
        for (int i = 0; i < m_utopia->getRestSegVector().size(); i++)
        {
            RestSeg *restseg = m_utopia->getRestSegVector()[i];
            m_memory_allocator->allocate(restseg->getSize() * 1024 * 1024); // Allocate the RestSegs at boot time in the physical memory
        }
        m_memory_allocator->perform_init_random(target_fragmentation, target_memory); // Allocate the rest of the memory with a target fragmentation level
    }
    else
    {
        m_memory_allocator->perform_init_random(target_fragmentation, target_memory);
    }
}

VirtuOS::~VirtuOS()
{
    delete m_memory_allocator;
    delete m_allocation_manager;
    delete m_utopia;
}