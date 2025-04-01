// #ifndef ALLOC_MANAGER_H
// #define ALLOC_MANAGER_H

// #include "fixed_types.h"
// #include "tls.h"
// #include "lock.h"
// #include "log.h"
// #include "core.h"
// #include "rangelb.h"

// #include <iostream>
// #include <fstream>
// #include <map>
// #include <vector>
// #include <unordered_map>


// typedef  std::vector<std::vector<Range>> rangeTable; // For each data structure store the ranges

// class AllocationManager
// {
//    private:

// 	   std::vector<std::unordered_map<uint64_t,uint64_t>> allocation_map_per_core; // Represents the mallocs
// 	   std::vector<std::unordered_map<IntPtr,uint64_t>> virtual_to_physical_map; // Normal full page table 
//       std::vector<std::unordered_map<IntPtr,uint64_t>> vpn_map_per_core; // Normal full page table 

//       rangeTable range_table;
//       bool eager;
//       int large_page_percentage;
      

//    public:
       
//       AllocationManager(bool eager, int large_page_percentage);
//       ~AllocationManager();
     
//       //handle page faults
//       int defaultPageFaultHandler();
//       void pageFaultHandler(IntPtr vpn, uint64_t core_id); 

//       //RMM ISCA 2015 functions
//       bool exists_in_range_table(IntPtr vpn, int core_id);
//       Range access_range_table(IntPtr vpn, int core_id);
//       int getNumberOfRanges(int cored_id);

//       //debug functions
//       void printPageMap(); 
//       void printVirtualMap();
//       void printRangeTable(std::vector<Range> range_table);

//       //handle malloc, calloc, realloc, free
//       void handleMalloc(uint64_t pointer,uint64_t size,int core_id);
//       void handleCalloc(uint64_t pointer,uint64_t size,int core_id);
//       void handleRealloc(uint64_t init_pointer, uint64_t pointer, uint64_t size,int core_id);
//       void handleFree(uint64_t pointer, int core_id);
// };

// #endif
