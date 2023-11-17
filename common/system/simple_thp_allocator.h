#pragma once
#include "physical_memory_allocator.h"
#include "rangelb.h"
#include <vector>
#include <map>
#include <bitset>

class SimpleTHPAllocator : public PhysicalMemoryAllocator
{
public:
    SimpleTHPAllocator(int max_order);
    void init();
    UInt64 allocate(UInt64 size, UInt64 address = 0, UInt64 core_id = -1);
    std::vector<Range> allocate_eager_paging(UInt64);
    UInt64 allocate_contiguous(UInt64 size);
    void deallocate(UInt64);
    void calculate_order(UInt64 size);
    void print_allocator();

    UInt64 getFreePages() const;
    UInt64 getTotalPages() const;
    double getAverageSizeRatio() const;
    double getLargePageRatio() const;
    double getFragmentationPercentage() const;

    void perform_init_file(String input_file_name);
    void perform_init_random(double target_fragmentation, double target_memory_percent, bool store_in_file = false);

    struct
    {

        std::vector<float> *fragmentation;
        std::vector<float> *two_mb_util_ratio;
        UInt64 four_kb_allocated;
        UInt64 two_mb_reserved;
        UInt64 two_mb_promoted;
        UInt64 two_mb_demoted;

    } stats;

protected:
    int m_max_order;
    UInt64 m_total_pages;
    UInt64 m_free_pages;
    std::vector<std::vector<std::tuple<UInt64, UInt64, bool, UInt64>>> free_list;
    std::map<UInt64, std::tuple<UInt64, std::bitset<512>, bool>> virtual_map;
    std::map<UInt64, UInt64> allocated_map;
};
