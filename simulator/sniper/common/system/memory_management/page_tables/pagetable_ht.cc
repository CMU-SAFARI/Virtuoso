#include "cache_cntlr.h"
#include "pwc.h"
#include "subsecond_time.h"
#include "memory_manager.h"
#include "pagetable.h"
#include <iostream>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <math.h>
#include <fstream>
#include "pagetable_ht.h"
#include "simulator.h"
#include "physical_memory_allocator.h"
#include "mimicos.h"

// #define DEBUG

namespace ParametricDramDirectoryMSI
{

    /*
     * The PageTableHT class implements a hash table–based page table.
     *
     * Each level of the page table (for different page sizes) is implemented as
     * an array of `Entry` objects. A hash function is used to map (tag = VPN >> 3)
     * into this array. Because of collisions, each Entry supports chaining.
     */

    /*
     * hashFunction(...)
     *   - Hashes an address using CityHash64, then mod with table_size.
     *   - Used to index into the page table array for a specific page size.
     */
    UInt64 PageTableHT::hashFunction(IntPtr address, int table_size)
    {
        uint64 result = CityHash64((const char *)&address, 8) % table_size;
        return result;
    }

    /*
     * PageTableHT constructor:
     *   - Takes core_id, name, type, page_sizes, etc.
     *   - m_page_table_sizes is an array specifying the hash-table size for each page size.
     *   - Allocates a separate hash-table array (page_tables) for each page size.
     *   - Also allocates metrics to track usage (page_table_walks, chained, and num_accesses).
     *   - The table_pa array is used to store the "physical address" of each page table in memory.
     */
    PageTableHT::PageTableHT(int core_id,
                             String name,
                             String type,
                             int page_sizes,
                             int *page_size_list,
                             int *page_table_sizes,
                             bool is_guest)
        : PageTable(core_id, name, type, page_sizes, page_size_list, is_guest),
          m_page_table_sizes(page_table_sizes)
    {
        std::cout << std::endl;

        log_file_name = "pagetable_ht.log";
        log_file_name = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + log_file_name;
        log_file.open(log_file_name.c_str());

        // table_pa holds the physical addresses of the page-table base for each page size.
        table_pa = (IntPtr *)malloc(sizeof(IntPtr) * m_page_sizes);

        // For each page size index, create an array of Entry structures.
        // Each Entry can store data for up to 8 contiguous VPN blocks,
        // but collisions are chained via next_entry.
        for (int i = 0; i < m_page_sizes; i++)
        {
            Entry *page_table = (Entry *)malloc(sizeof(Entry) * m_page_table_sizes[i]);

#ifdef DEBUG
            std::cout << "[Hash Table Chain] Page table size for page size "
                      << m_page_size_list[i]
                      << " is "
                      << (m_page_table_sizes[i]) * sizeof(Entry) / 1024 / 1024 / 1024
                      << "GB" << std::endl;
#endif

            // Use the Memory Allocator to carve out space for the page table
            // handle_page_table_allocations(...) returns the base "emulated" physical address
            table_pa[i] = Sim()->getMimicOS()->getMemoryAllocator()->handle_page_table_allocations((m_page_table_sizes[i]) * 64);

#ifdef DEBUG
            std::cout << "[Hash Table Chain] Page table address for page size "
                      << m_page_size_list[i]
                      << " is " << table_pa[i] << std::endl;
#endif

            // Initialize each entry in the array to default values
            for (int j = 0; j < m_page_table_sizes[i]; j++)
            {
                page_table[j].tag = -1;          // Indicates empty
                page_table[j].next_entry = NULL; // No chain initially

                for (int k = 0; k < 8; k++)
                {
                    page_table[j].valid[k] = false;
                    page_table[j].ppn[k] = 0; // Physical page number array is zeroed
                }

                // Each table entry also has an "emulated_physical_address",
                // which references the hardware location of this entry (64 bytes per entry).
                page_table[j].emulated_physical_address = (table_pa[i] + j * 64);
            }

            // Push this newly created page_table array into our page_tables vector
            page_tables.push_back(page_table);

            // Allocate memory for tracking stats for each page size
            stats.page_table_walks = new uint64[m_page_sizes];
            stats.chained = new uint64[m_page_sizes];
            stats.num_accesses = new uint64[m_page_sizes];
        }

        // Register stats metrics for each page size
        for (int i = 0; i < m_page_sizes; i++)
        {
            registerStatsMetric(name,
                                core_id,
                                ("page_table_walks_" + std::to_string(m_page_size_list[i])).c_str(),
                                &stats.page_table_walks[i]);

            registerStatsMetric(name,
                                core_id,
                                ("conflicts_" + std::to_string(m_page_size_list[i])).c_str(),
                                &stats.chained[i]);

            registerStatsMetric(name,
                                core_id,
                                ("accesses_" + std::to_string(m_page_size_list[i])).c_str(),
                                &stats.num_accesses[i]);

            // table_chained_ is probably a duplicate stat for the same data
            registerStatsMetric(name,
                                core_id,
                                ("table_chained_" + std::to_string(m_page_size_list[i])).c_str(),
                                &stats.chained[i]);
        }
    }

    /*
     * PageTableHT destructor:
     *   - Frees the allocated memory for each page_table array
     *   - Also frees the table_pa array
     */
    PageTableHT::~PageTableHT()
    {
        for (int i = 0; i < m_page_sizes; i++)
        {
            free(page_tables[i]);
        }

        free(table_pa);
    }

    /*
     * printVectorStatistics(...)
     *   - Currently unimplemented; can be used to output vector stats from each page table.
     */
    void PageTableHT::printVectorStatistics()
    {
        // Implement the function to print vector statistics
    }

    /*
     * initializeWalk(...)
     *   - This method implements the main page-table lookup procedure for an address.
     *   - We try each page_size in m_page_size_list, compute the VPN, then the "tag" and "block_offset."
     *   - Use hashFunction(...) to find the candidate slot in the page_tables array for this page size.
     *   - If the matching slot is empty or invalid, we move on. If it's a chain, we follow next_entry.
     *   - If none of the page sizes yields a valid translation, we have a page fault.
     *
     *   The function returns a PTWResult, indicating (page_size, visited_addresses, ppn, walk_latency,
     *   caused_page_fault).
     */
    PTWResult PageTableHT::initializeWalk(IntPtr address, bool count, bool is_prefetch, bool restart_walk_after_fault)
    {
#ifdef DEBUG
        log_file << std::endl;
        log_file << "[Hash Table Chain] Initializing page table walk for address " << address << std::endl;
#endif
        accessedAddresses visited_addresses;

        int page_size_result = -1;
        IntPtr ppn_result = 0;
        bool total_page_fault = false;

        // Increase page_table_walks for all page sizes (could be seen as a single "walk" event).
        for (int i = 0; i < m_page_sizes; i++)
        {
            stats.page_table_walks[i]++;
        }

    restart_walk:

        // For each page size, attempt to find a match in the hash table
        for (int i = 0; i < m_page_sizes; i++)
        {
            IntPtr VPN = address >> (m_page_size_list[i]);
            IntPtr tag = VPN >> 3;         // The upper bits after removing block_offset
            IntPtr block_offset = VPN % 8; // The lower 3 bits in the VPN
            uint64_t hash_function_result = hashFunction(tag, m_page_table_sizes[i]);

            // Start at the hashed entry
            Entry *current_entry = &page_tables[i][hash_function_result];

            int counter = 0; // Tracks how many chain steps we've taken

#ifdef DEBUG
            log_file << "[Hash Table Chain] Walking page table for page size "
                     << m_page_size_list[i]
                     << " with VPN " << VPN
                     << " and tag " << tag
                     << " and block offset " << block_offset
                     << " and hash function result " << hash_function_result << std::endl;
#endif

            // Continue following the chain until we find a match, find an empty entry, or run out of chain
            while (true)
            {
                if (current_entry == NULL)
                {
                    // This theoretically shouldn't occur unless the memory structure is malformed
                    assert(false);
                    break;
                }
                // If the entry matches the tag and the relevant offset is valid => we have a PPN
                else if (current_entry->tag == tag && current_entry->valid[block_offset])
                {
#ifdef DEBUG
                    log_file << "[Hash Table Chain] Hit with valid entry for page size "
                             << m_page_size_list[i]
                             << " with VPN " << VPN
                             << " and tag " << tag
                             << " and block offset " << block_offset
                             << " and hash function result " << hash_function_result << std::endl;
                    log_file << "[Hash Table Chain] PPN: " << current_entry->ppn[block_offset]
                             << " and emulated page table physical address: "
                             << current_entry->emulated_physical_address * 4096 + block_offset * 8 << std::endl;
#endif
                    visited_addresses.push_back(make_tuple(i,
                                                           counter,
                                                           current_entry->emulated_physical_address * 4096 + block_offset * 8,
                                                           true));
                    if (count)
                        stats.num_accesses[i]++;

                    page_size_result = m_page_size_list[i];
                    ppn_result = current_entry->ppn[block_offset];
                    break;
                }
                // If the entry’s tag matches but the offset is not valid => not mapped
                else if (current_entry->tag == tag && !current_entry->valid[block_offset])
                {
#ifdef DEBUG
                    log_file << "[Hash Table Chain] Hit with invalid entry for page size "
                             << m_page_size_list[i]
                             << " with VPN " << VPN
                             << " and tag " << tag
                             << " and block offset " << block_offset
                             << " and hash function result " << hash_function_result << std::endl;
                    log_file << "[Hash Table Chain] Validity vector: ";
                    for (int k = 0; k < 8; k++)
                    {
                        log_file << current_entry->valid[k] << " ";
                    }
                    log_file << std::endl;
#endif
                    visited_addresses.push_back(make_tuple(i,
                                                           counter,
                                                           current_entry->emulated_physical_address * 4096 + block_offset * 8,
                                                           false));
                    if (count)
                        stats.num_accesses[i]++;
                    break;
                }
                // If the entry is completely empty (tag == -1), we are done searching for this page size
                else if (current_entry->tag == static_cast<IntPtr>(-1))
                {
#ifdef DEBUG
                    log_file << "[Hash Table Chain] Empty entry for page size "
                             << m_page_size_list[i]
                             << " with VPN " << VPN
                             << " and tag " << tag
                             << " and block offset " << block_offset
                             << " and hash function result " << hash_function_result << std::endl;
#endif
                    visited_addresses.push_back(make_tuple(i,
                                                           counter,
                                                           current_entry->emulated_physical_address * 4096 + block_offset * 8,
                                                           false));
                    if (count)
                        stats.num_accesses[i]++;

                    // If this is not chained further, break out; otherwise, continue to the next entry
                    if (current_entry->next_entry == NULL)
                    {
                        break;
                    }
                    else
                    {
                        current_entry = current_entry->next_entry;
                        counter++;
                    }
                }
                // If next_entry is not null, then we move forward in the chain
                else if (current_entry->next_entry != NULL)
                {
#ifdef DEBUG
                    log_file << "[Hash Table Chain] Chained entry for page size "
                             << m_page_size_list[i]
                             << " with VPN " << VPN
                             << " and tag " << tag
                             << " and block offset " << block_offset
                             << " and hash function result " << hash_function_result << std::endl;
#endif
                    visited_addresses.push_back(make_tuple(i,
                                                           counter,
                                                           current_entry->emulated_physical_address * 4096 + block_offset * 8,
                                                           false));
                    if (count)
                        stats.num_accesses[i]++;
                    current_entry = current_entry->next_entry;
                    counter++;
                }
                // End of chain
                else if (current_entry->next_entry == NULL)
                {
#ifdef DEBUG
                    log_file << "[Hash Table Chain] End of chain for page size "
                             << m_page_size_list[i]
                             << " with VPN " << VPN
                             << " and tag " << tag
                             << " and block offset " << block_offset
                             << " and hash function result " << hash_function_result << std::endl;
#endif
                    break;
                }
                else
                {
                    // Fallback catch for unexpected conditions
                    assert(false);
                }
            }
        }

        // If no page_size_result was found => we have a page fault
        if (page_size_result == -1)
        {
#ifdef DEBUG
            log_file << "[Hash Table Chain] Page fault for address " << address << std::endl;
#endif
            total_page_fault = true;

            // If we want to restart walk after fault, we handle the fault in the OS,
            // then jump back to restart_walk.
            if (restart_walk_after_fault)
                Sim()->getMimicOS()->handle_page_fault(address, core_id, 0);

#ifdef DEBUG
            log_file << "[Hash Table Chain] Page fault handled for address " << address << std::endl;
            log_file << std::endl;
            log_file << "[Hash Table Chain] Restarting page table walk for address " << address << std::endl;
#endif
            if (restart_walk_after_fault)
                goto restart_walk;
            else
                return PTWResult(page_size_result, visited_addresses, ppn_result, SubsecondTime::Zero(), total_page_fault);
        }

#ifdef DEBUG
        log_file << "[Hash Table Chain] Page table walk completed for address " << address << std::endl;
        log_file << "[Hash Table Chain] Page size result: " << page_size_result << std::endl;
        log_file << "[Hash Table Chain] PPN result: " << ppn_result << std::endl;
#endif

        // Return results: (page_size_found, visited_entries, final PPN, latency=Zero, page_fault?)
        return PTWResult(page_size_result, visited_addresses, ppn_result, SubsecondTime::Zero(), total_page_fault);
    }

    /*
     * calculate_mean(), calculate_std()
     *   - Stubbed out for future use to analyze distribution of chain lengths or latencies.
     */
    void PageTableHT::calculate_mean()
    {
        // Implement the function to calculate mean
    }

    void PageTableHT::calculate_std()
    {
        // Implement the function to calculate standard deviation
    }

    /*
     * updatePageTableFrames(...)
     *   - Updates or inserts a new mapping for the given (address -> ppn) at the relevant page_size.
     *   - We identify the correct hash table with page_size_index, compute a hash on (VPN >> 3),
     *     and walk the chain. If we find a match with an unused offset, we mark it valid.
     *     Otherwise, we allocate a new chained Entry.
     *   - frames is not used in this function but might store intermediate steps in advanced usage.
     */
    int PageTableHT::updatePageTableFrames(IntPtr address,
                                           IntPtr core_id,
                                           IntPtr ppn,
                                           int page_size,
                                           std::vector<UInt64> frames)
    {
#ifdef DEBUG
        log_file << std::endl;
        log_file << "[Hash Table Chain] Updating page table for address " << address
                 << " with ppn " << ppn
                 << " and page size " << page_size << std::endl;
#endif

        // Find which index in m_page_size_list matches the requested page_size
        int page_size_index = 0;
        for (int i = 0; i < m_page_sizes; i++)
        {
            if (m_page_size_list[i] == page_size)
            {
                page_size_index = i;
                break;
            }
        }

        // Extract the VPN bits from the virtual address
        IntPtr VPN = address >> (m_page_size_list[page_size_index]);
        IntPtr tag = VPN >> 3;
        IntPtr block_offset = VPN % 8;

        // Use the hash function to find the correct slot in the page_tables array
        uint64_t hash_function_result = hashFunction(tag, m_page_table_sizes[page_size_index]);

        Entry *current_entry = &page_tables[page_size_index][hash_function_result];
        int counter = 0;

        while (true)
        {
            // If the slot is empty (tag == -1), fill it in
            if (current_entry->tag == static_cast<IntPtr>(-1))
            {
                current_entry->tag = tag;
                current_entry->ppn[block_offset] = ppn;

                // Mark all offsets invalid initially, then mark the relevant offset as valid
                for (int i = 0; i < 8; i++)
                    current_entry->valid[i] = false;

                current_entry->valid[block_offset] = true;
                current_entry->next_entry = NULL;

#ifdef DEBUG
                log_file << "[Hash Table Chain] Inserted entry at index: "
                         << hash_function_result
                         << " with tag: " << tag
                         << " at offset: " << block_offset
                         << "with page size" << page_size << std::endl;
#endif
                break;
            }
            // If the current entry has the same tag but the offset is not valid, we enable that offset
            else if (current_entry->tag == tag && !current_entry->valid[block_offset])
            {
                current_entry->valid[block_offset] = true;
                current_entry->ppn[block_offset] = ppn;
#ifdef DEBUG
                log_file << "[Hash Table Chain] Updated entry at index: "
                         << hash_function_result
                         << " with tag: " << tag
                         << " at offset: " << block_offset << std::endl;
#endif
                break;
            }
            // If the current entry is in use and next_entry == NULL,
            // we allocate a new entry and chain it.
            else if (current_entry->next_entry == NULL)
            {
                Entry *new_entry = (Entry *)malloc(sizeof(Entry));
                new_entry->tag = tag;
                new_entry->ppn[block_offset] = ppn;

                for (int i = 0; i < 8; i++)
                    new_entry->valid[i] = false;

                new_entry->valid[block_offset] = true;
                new_entry->next_entry = NULL;
                new_entry->emulated_physical_address = Sim()->getMimicOS()->getMemoryAllocator()->handle_fine_grained_page_table_allocations(64);

                current_entry->next_entry = new_entry;

                // Increment the "chain" statistic
                stats.chained[page_size_index]++;

#ifdef DEBUG
                log_file << "[Hash Table Chain] Inserted new entry at index: "
                         << hash_function_result
                         << " with tag: " << tag
                         << " at offset: " << block_offset << std::endl;
                log_file << "[Hash Table Chain] New entry emulated physical address: "
                         << new_entry->emulated_physical_address << std::endl;
#endif
                break;
            }
            else
            {
                // Move further down the chain to find a suitable slot
                current_entry = current_entry->next_entry;
                counter++;
            }
        }

        return 0;
    }

    /*
     * printPageTable()
     *   - Iterates over each page table array in page_tables and prints out
     *     the tag, valid bits, and PPNs for debugging or analysis.
     */
    void PageTableHT::printPageTable()
    {
        for (int i = 0; i < m_page_sizes; i++)
        {
            cout << "Page table for page size " << m_page_size_list[i] << endl;
            for (int j = 0; j < m_page_table_sizes[i]; j++)
            {
                cout << "Entry " << j << ": " << endl;
                cout << "Tag: " << page_tables[i][j].tag << endl;
                cout << "Empty: " << page_tables[i][j].valid[0]
                     << page_tables[i][j].valid[1]
                     << page_tables[i][j].valid[2]
                     << page_tables[i][j].valid[3] << endl;
                cout << "PPN: " << page_tables[i][j].ppn[0]
                     << page_tables[i][j].ppn[1]
                     << page_tables[i][j].ppn[2]
                     << page_tables[i][j].ppn[3] << endl;
            }
        }
    }

    /*
     * deletePage(...)
     *   - Removes or invalidates the page entry for the given address in the base page table
     *     (index 0 in m_page_size_list). If after clearing the block_offset,
     *     the entry has no valid offsets, we remove the entire entry from the chain.
     */
    void PageTableHT::deletePage(IntPtr address)
    {
        IntPtr VPN = address >> (m_page_size_list[0]);
        IntPtr tag = VPN >> 3;
        IntPtr block_offset = VPN % 8;

        uint64_t hash_function_result = hashFunction(tag, m_page_table_sizes[0]);
        Entry *current_entry = &page_tables[0][hash_function_result];

#ifdef DEBUG
        std::cout << "Deleting page at address: " << address
                  << " with VPN: " << VPN
                  << " and tag: " << tag
                  << " and block offset: " << block_offset
                  << " at index: " << hash_function_result << std::endl;
#endif

        while (true)
        {
#ifdef DEBUG
            std::cout << "Current entry tag: "
                      << current_entry->tag
                      << " and tag to delete: " << tag
                      << " at offset " << block_offset << std::endl;
#endif
            // If the current entry has the same tag, we invalidate the block_offset
            if (current_entry->tag == tag)
            {
                current_entry->valid[block_offset] = false;
                current_entry->ppn[block_offset] = 0;

                // If all offsets are invalid, remove or repurpose the entry
                for (int i = 0; i < 8; i++)
                {
                    if (current_entry->valid[i])
                    {
                        break; // Found a valid offset, so we keep this entry
                    }
                    else if (i == 7)
                    {
                        // If we got here, none of the offsets in this entry are valid
                        current_entry->tag = -1;

                        // If there's a chained entry behind this one, copy it forward
                        if (current_entry->next_entry != NULL)
                        {
                            Entry *temp = current_entry->next_entry;
                            current_entry->tag = temp->tag;
                            current_entry->next_entry = temp->next_entry;
                            current_entry->emulated_physical_address = temp->emulated_physical_address;
                            for (int j = 0; j < 8; j++)
                            {
                                current_entry->ppn[j] = temp->ppn[j];
                                current_entry->valid[j] = temp->valid[j];
                            }
                            free(temp);
                        }
                    }
                }
                break;
            }
            // If entry is empty, there is no further data to remove
            else if (current_entry->tag == static_cast<IntPtr>(-1))
            {
                if (current_entry->next_entry == NULL)
                    break;
                else
                    current_entry = current_entry->next_entry;
            }
            // If this entry doesn't match the tag, try the chain
            else if (current_entry->tag != tag)
            {
                if (current_entry->next_entry == NULL)
                    break;
                else
                    current_entry = current_entry->next_entry;
            }
        }
    }

} // namespace ParametricDramDirectoryMSI
