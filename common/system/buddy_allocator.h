#pragma once
#include "physical_memory_allocator.h"
#include "rangelb.h"
#include <vector>
#include <map>

class BuddyAllocator : public PhysicalMemoryAllocator
{
public:
    BuddyAllocator(int max_order);
    void init();
    UInt64 allocate(UInt64 size, UInt64 address = 0, UInt64 core = 0);
    std::vector<Range> allocate_eager_paging(UInt64);
    UInt64 allocate_contiguous(UInt64 size);
    void deallocate(UInt64);
    bool compact_memory();
    bool compact_memory(UInt64 size);
    void calculate_order(UInt64 size);
    void print_allocator();

    UInt64 getFreePages() const;
    UInt64 getTotalPages() const;
    double getAverageSizeRatio() const;
    double getFragmentationPercentage() const;

    void perform_init_file(String input_file_name);
    void perform_init_random(double target_fragmentation, double target_memory_percent, bool store_in_file = false);

protected:
    int m_max_order;
    UInt64 m_total_pages;
    UInt64 m_free_pages;
    std::vector<std::vector<std::pair<UInt64, UInt64>>> free_list;
    std::map<UInt64, UInt64> allocated_map;
};
