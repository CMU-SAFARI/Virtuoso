#ifndef SWAP_SPACE_H
#define SWAP_SPACE_H

#include <string>
#include <unordered_map>
#include <list>
#include <fstream>
#include <cstddef> // Required for size_t
#include <vector>
#include "simulator.h"
#include "config.hpp"
#include "fixed_types.h"

// Represents the unique location of a page in the backing store (swap disk)

using namespace std;

/**
 * @brief A class that imitates the Linux swap cache.
 */
class SwapCache {
public:

    SwapCache();

    // Access a page, triggering a major page fault if not in RAM.
    bool lookup(IntPtr page_id, int app_id);
    bool swapOut(IntPtr virtual_page,int app_id, bool is_memory_full);
    void swapIn(IntPtr virtual_page, int app_id);
    IntPtr findFreePage();


    struct {
        UInt64 swap_ins;
        UInt64 swap_outs;
        UInt64 failed_swap_outs_space;
    } stats;

    std::ofstream log_file;
    std::string log_file_name;

private:


    int swap_size;
    
    // Custom hash function for std::pair<IntPtr, int>
    struct PairHash {
        std::size_t operator()(const std::pair<IntPtr, int>& p) const {
        return std::hash<IntPtr>()(p.first) ^ (std::hash<int>()(p.second) << 1);
     }
    };

    // Custom equality comparator (optional if std::pair's operator== suffices)
    struct PairEqual {
        bool operator()(const std::pair<IntPtr, int>& lhs, const std::pair<IntPtr, int>& rhs) const {
        return lhs.first == rhs.first && lhs.second == rhs.second;
     }
    };

    // Define the unordered_map
    std::unordered_map<std::pair<IntPtr, int>, IntPtr, PairHash, PairEqual> swap_cache_map;

    

    // Define a data structure that stores free pages in the swap cache
    std::vector<bool> free_pages;
};

#endif // SWAP_CACHE_H
