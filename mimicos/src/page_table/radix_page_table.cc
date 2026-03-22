

#include <iostream>
#include <vector>
#include <memory>
#include <cstdint>
#include "radix_page_table.h"


/*A simple Radix Page Table implementation for MimicOS. 
This page table serves only a single purpose: emulate the memory accesses and the complexity of performing insert/update and lookup operations.
Its contents are propagated to the SIFT-based application via the MimicOS physical memory allocator, which is invoked upon page faults but 
its physical location is not shared with the SIFT-based application since we anyways emulate the physical memory allocation of the page table frames. 

 We use a 4-level Radix tree, similar to x86-64 page tables.
 Each level has 512 entries (9 bits), and the page size is 4KB (12 bits).
 The virtual address is split as follows:
 - Bits 39-47: Level 4 index (PML4)
 - Bits 30-38: Level 3 index (PDPT)
 - Bits 21-29: Level 2 index (PD)
 - Bits 12-20: Level 1 index (PT)
 - Bits 0-11: Offset within the page

 Each entry in the page table contains the physical address of the next level table or the physical frame.
 For simplicity, we only store the physical address and a present bit (bit 0).
*/

RadixPageTable::RadixPageTable() {
    // Initially allocate one page table for the PML4
    pageTables.push_back(std::make_unique<RadixPageTableFrame>());
}

uint64_t* RadixPageTable::lookup(uint64_t virtualAddress) {
    uint64_t* entry = nullptr;
    for (int level = 0; level < 4; ++level) {
        int index = getIndex(virtualAddress, level);
        if (level >= pageTables.size() || pageTables[level]->entries[index] == 0) {
            return nullptr; // No entry
        }
        entry = &pageTables[level]->entries[index];
    }
    return entry;
}

void RadixPageTable::insert(uint64_t virtualAddress, uint64_t physicalAddress) {
    for (int level = 0; level < 4; ++level) {
        int index = getIndex(virtualAddress, level);
        if (level >= pageTables.size()) {
            pageTables.push_back(std::make_unique<RadixPageTableFrame>()); // Dynamically allocate a new page table
        }
        if (pageTables[level]->entries[index] == 0 || level == 3) {
            pageTables[level]->entries[index] = physicalAddress | 0x1; // Set present bit
            return;
        }
    }
}
