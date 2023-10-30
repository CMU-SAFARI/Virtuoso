#pragma once

#include "fixed_types.h"
#include <vector>
#include "rangelb.h"

class PhysicalMemoryAllocator
{
public:
    PhysicalMemoryAllocator();

    virtual void init();
    virtual UInt64 allocate(UInt64 size, UInt64 address = 0, UInt64 core_id = -1) = 0;
    virtual void deallocate(UInt64) = 0;
    virtual std::vector<Range> allocate_eager_paging(UInt64) = 0;
    virtual void perform_init_random(double target_fragmentation, double target_memory_percent, bool store_in_file = false) = 0;
    virtual void print_allocator() = 0;

protected:
    UInt64 m_memory_size; // in mbytes
};
