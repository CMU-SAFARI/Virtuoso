#include "swap_space.h"
#include "simulator.h"
#include "config.hpp"
#include <cstring>
#include <unordered_map>
#include <list>
#include "stats.h"

//#define DEBUG 

SwapCache::SwapCache() {

    swap_size = Sim()->getCfg()->getInt("perf_model/swap_space/swap_size"); // In MB

    swap_size = swap_size * 1024/4;

    log_file = std::ofstream();
    log_file_name = "swap.log";
    log_file_name = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + log_file_name;
    log_file.open(log_file_name.c_str());

    // Constructor implementation can be added here if needed
    bzero(&stats, sizeof(stats));
    registerStatsMetric("swap_space", 0, "swap_ins", &stats.swap_ins);
    registerStatsMetric("swap_space", 0, "swap_outs", &stats.swap_outs);
    registerStatsMetric("swap_space", 0, "failed_swap_outs", &stats.failed_swap_outs_space);
    // initalize free pages
    free_pages.resize(swap_size, false); // Initialize all pages as free (false)

    std::cout << "[SWAP_CACHE] Swap cache initialized with size: " << swap_size << std::endl;
}


bool SwapCache::lookup(IntPtr page_id, int app_id) {

#ifdef DEBUG
    log_file<< "[SWAP_CACHE] Looking up page: " << page_id << " for app: " << app_id << std::endl;
#endif
    // Check if the page is in the swap cache
    auto it = swap_cache_map.find(make_pair(page_id, app_id));
#ifdef DEBUG
    log_file<< "[SWAP_CACHE] Swap cache size: " << swap_cache_map.size() << std::endl;
#endif
    if (it != swap_cache_map.end()) {
        // Page found in swap cache, update LRU list
        #ifdef DEBUG
            log_file<< "[SWAP_CACHE] Page " << page_id << " found in swap cache." << std::endl;
        #endif
        return true; // Page is in swap
    } else {
        // Here you would typically handle the page fault, e.g., loading the page from disk
        #ifdef DEBUG
            log_file<< "[SWAP_CACHE] Page " << page_id << " not found in swap cache." << std::endl;
        #endif
        return false; // Page is not in RAM
    }
}


void SwapCache::swapIn(IntPtr virtual_page, int app_id) {

    // This function would handle swapping a page into RAM from the swap cache
    
    //Remove the page from the swap cache
    swap_cache_map.erase(make_pair(virtual_page, app_id));

    #ifdef DEBUG
        log_file<< "[SWAP_CACHE] Swapped in page: " << virtual_page << std::endl;
    #endif
    stats.swap_ins++;

    return;

}

IntPtr SwapCache::findFreePage() {

    for (size_t i = 0; i < free_pages.size(); ++i) {
        if (!free_pages[i]) {
            free_pages[i] = true; // Mark this page as used
            #ifdef DEBUG
                log_file<< "[SWAP_CACHE] Found free page at index: " << i << std::endl;
            #endif
            return i; // Return the index as the page ID
        }
    }
    #ifdef DEBUG
        log_file<< "[SWAP_CACHE] No free page found." << std::endl;
    #endif
    return  static_cast<IntPtr>(-1); // No free page found
}

bool SwapCache::swapOut(IntPtr virtual_page, int app_id, bool is_memory_full) {

#ifdef DEBUG
    log_file<< "[SWAP_CACHE] Attempting to swap out page: " << virtual_page << " for app: " << app_id << std::endl;
#endif


    // Add the page to the swap cache
    IntPtr swap_location = findFreePage();

#ifdef DEBUG
    log_file<< "[SWAP_CACHE] Swap location found: " << swap_location << std::endl;
#endif

    if (swap_location == static_cast<IntPtr>(-1)) {
        
        // No free page available in the swap cache
        #ifdef DEBUG
            log_file<< "[SWAP_CACHE] No free page available, cannot swap out page: " << virtual_page << std::endl;
        #endif
        return false; // Cannot swap out
    }

#ifdef DEBUG
    log_file<< "[SWAP_CACHE] Swapping out page: " << virtual_page << " to swap location: " << swap_location << std::endl;
#endif
    swap_cache_map[std::make_pair(virtual_page, app_id)] = swap_location;

    //get the size of the swap cache
    int current_size = swap_cache_map.size();
    // If the cache exceeds a certain size, remove the least recently used page


    //We attempted to swap out a page and the memory is not full but the swap is full
#ifdef DEBUG
    log_file<< "[SWAP_CACHE] Current swap cache size: " << current_size << std::endl;
    log_file<< "[SWAP_CACHE] Swap size limit: " << swap_size << std::endl;
#endif

    if ( current_size > swap_size) {
        stats.failed_swap_outs_space++;
        #ifdef DEBUG
            log_file<< "[SWAP_CACHE] Swap cache is full, cannot swap out page: " << virtual_page << std::endl;
        #endif
        return false;
    }

    // Successfully swapped out the page
#ifdef DEBUG
    log_file<< "[SWAP_CACHE] Successfully swapped out page: " << virtual_page << " to swap location: " << swap_location << std::endl;
#endif
    stats.swap_outs++;
    return true; // Successfully swapped out the page
    
    #ifdef DEBUG
        log_file<< "[SWAP_CACHE] Swapped out page: " << virtual_page << " to location: " << swap_location << std::endl;
    #endif
}
