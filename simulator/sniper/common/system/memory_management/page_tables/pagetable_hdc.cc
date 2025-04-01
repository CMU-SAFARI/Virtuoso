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
#include "pagetable_hdc.h"
#include "simulator.h"
#include "physical_memory_allocator.h"
#include "mimicos.h"

// #define DEBUG
// #define LOG_MEAN_STD

namespace ParametricDramDirectoryMSI
{
	/*
	 * PageTableHDC implements a hash-based page table ("Hash-Distanced Chaining," perhaps).
	 * 
	 * - Each "page size" has a separate table (an array of Entry objects).
	 * - The key (tag) is computed from bits of the VPN.
	 * - Collisions are resolved by linear probing, incrementing the index until an empty slot is found.
	 * - Each Entry stores 8 possible offsets (block_offset), so that one Entry can represent multiple VPN slices.
	 * - Stats are tracked for collisions, page table walks, and overall usage.
	 */

	/*
	 * hashFunction(...)
	 *   - Uses CityHash64 on the address, then mods with table_size.
	 *   - This determines the initial index for linear probing in PageTableHDC.
	 */
	UInt64 PageTableHDC::hashFunction(IntPtr address, int table_size)
	{
		uint64 result = CityHash64((const char *)&address, 8) % table_size;
		return result;
	}

	/*
	 * Constructor for PageTableHDC:
	 *   - Creates one Entry array per page size (stored in page_tables).
	 *   - Allocates an "emulated_table_address" for each page size, signifying where 
	 *     these page-table entries live in physical memory.
	 *   - Registers stats counters for each page size (walks, accesses, collisions).
	 */
	PageTableHDC::PageTableHDC(int core_id,
							   String name,
							   String type,
							   int page_sizes,
							   int *page_size_list,
							   int *page_table_sizes,
							   bool is_guest)
		: PageTable(core_id, name, type, page_sizes, page_size_list, is_guest),
		  m_page_table_sizes(page_table_sizes)
	{
		// Create a log file for debugging/tracing
		log_file_name = "page_table_hdc.log";
		log_file_name = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + log_file_name;
		log_file.open(log_file_name.c_str());

		// emulated_table_address tracks the base address for each page-table array.
		emulated_table_address = (IntPtr *)malloc(sizeof(IntPtr) * m_page_sizes);

		std::cout << std::endl;

		// For each page size, allocate a page table of "m_page_table_sizes[i]" entries.
		for (int i = 0; i < m_page_sizes; i++)
		{
			std::cout << "[HDC] Page table size for page size " 
					  << m_page_size_list[i] << " is " 
					  << (m_page_table_sizes[i]) * 64 / 1024 
					  << " KB" << std::endl;

			// Use the OS's memory allocator to carve out space for these page-table entries
			emulated_table_address[i] = Sim()->getMimicOS()->getMemoryAllocator()->handle_page_table_allocations((m_page_table_sizes[i]) * 64);

			std::cout << "[HDC] Page table address for page size " 
					  << m_page_size_list[i] << " is " 
					  << emulated_table_address[i] * 4096 << std::endl;

			// Allocate actual memory for the array of Entry objects
			Entry *page_table = (Entry *)malloc(sizeof(Entry) * m_page_table_sizes[i]);

			// Initialize each entry in the array
			for (int j = 0; j < m_page_table_sizes[i]; j++)
			{
				for (int k = 0; k < 8; k++)
				{
					page_table[j].valid[k] = false;
					page_table[j].ppn[k] = 0;
				}
				page_table[j].tag = -1;              // Marks an empty or unused entry
				page_table[j].distance_from_root = -1;
			}

			// Store this new array into page_tables for future lookups
			page_tables.push_back(page_table);

			// Initialize page fault count for this page size
			stats.page_faults = 0;
		}

		// For each page size, also track page_table_walks, num_accesses, and collisions
		stats.page_table_walks = new uint64[m_page_sizes];
		stats.num_accesses = new uint64[m_page_sizes];
		stats.collisions = new uint64[m_page_sizes];

		// Register the metrics so they can be logged externally
		for (int i = 0; i < m_page_sizes; i++)
		{
			registerStatsMetric(name, core_id,
								("page_table_walks_" + std::to_string(m_page_size_list[i])).c_str(),
								&stats.page_table_walks[i]);

			registerStatsMetric(name, core_id,
								("accesses_" + std::to_string(m_page_size_list[i])).c_str(),
								&stats.num_accesses[i]);

			registerStatsMetric(name, core_id,
								("collisions_" + std::to_string(m_page_size_list[i])).c_str(),
								&stats.collisions[i]);
		}

#ifdef LOG_MEAN_STD
		// If enabled, also prepare vectors/files for logging mean and std dev. 
		stats.std = new std::vector<double>[m_page_sizes];
		stats.mean = new std::vector<double>[m_page_sizes];

		std::string std_filename;
		std::string mean_filename;

		for (int i = 0; i < m_page_sizes; i++)
		{
			std_filename = "std_" + std::to_string(m_page_size_list[i]);
			mean_filename = "mean_" + std::to_string(m_page_size_list[i]);

			String prefix = Sim()->getCfg()->getString("general/output_dir");

			String std_name = prefix + "/" + std_filename.c_str();
			String mean_name = prefix + "/" + mean_filename.c_str();

			std_file.push_back(std::ofstream(std_name.c_str()));
			mean_file.push_back(std::ofstream(mean_name.c_str()));

			std::cout << "Created file " << std_name << std::endl;
			std::cout << "Created file " << mean_name << std::endl;
		}
#endif
	}

	/*
	 * Destructor: Frees the arrays allocated for each page size,
	 * and frees the array emulated_table_address.
	 */
	PageTableHDC::~PageTableHDC()
	{
		for (int i = 0; i < m_page_sizes; i++)
		{
			free(page_tables[i]);
		}

		free(emulated_table_address);
	}

	/*
	 * printVectorStatistics():
	 *   - If using LOG_MEAN_STD, we write the latest standard deviation and mean to the 
	 *     respective files for each page size.
	 */
	void PageTableHDC::printVectorStatistics()
	{
		for (int i = 0; i < m_page_sizes; i++)
		{
			double mean = stats.mean[i].back();
			double std = stats.std[i].back();

			std_file[i] << std << std::endl;
			mean_file[i] << mean << std::endl;
		}

		return;
	}

	/*
	 * initializeWalk(...):
	 *   - Main page-table lookup method. For each page size, compute VPN/tag/block_offset, 
	 *     then probe (using linear probing) in the page_tables array to find a matching tag 
	 *     or empty slot. 
	 *   - If none of the page sizes yields a valid translation, a page fault is raised.
	 *   - Returns a PTWResult containing the final page size, visited addresses, PPN, 
	 *     translation latency, and whether a page fault was encountered.
	 */
	PTWResult PageTableHDC::initializeWalk(IntPtr address,
										   bool count,
										   bool is_prefetch,
										   bool restart_walk_after_fault)
	{
#ifdef DEBUG
		log_file << std::endl;
		log_file << "[HDC] Initializing page table walk for address " << address << std::endl;
#endif
		accessedAddresses visited_addresses;
		visited_addresses.reserve(50); // Reserving space for optimization, not mandatory

		bool is_pagefault_in_every_page_size = false;

		// Increment the page_table_walks stat for each page size if counting
		for (int i = 0; i < m_page_sizes; i++)
		{
			if (count)
				stats.page_table_walks[i]++;
		}

	restart_walk:;

		int page_size_result = -1;
		IntPtr ppn = 0;

		// Check each page size for a possible translation
		for (int i = 0; i < m_page_sizes; i++)
		{
			// Calculate VPN, then derive tag (upper bits) and block_offset (lower 3 bits)
			IntPtr VPN = address >> (m_page_size_list[i]);
			IntPtr tag = VPN >> 3;				 
			IntPtr block_offset = VPN % 8;		

			UInt64 hash_function_result = hashFunction(tag, m_page_table_sizes[i]);
			Entry *current_entry = &page_tables[i][hash_function_result];

#ifdef DEBUG
			log_file << "[HDC] Searching for VPN " << VPN 
					 << " in page size " << m_page_size_list[i] 
					 << " at index " << hash_function_result << std::endl;
#endif

			int depth = 0;

			// Linear probe until we find a matching or empty slot, or we exhaust the table
			while (depth < m_page_table_sizes[i])
			{
				// If tags match and offset is valid => success
				if ((current_entry->tag == tag) && (current_entry->valid[block_offset]))
				{
#ifdef DEBUG
					log_file << "[HDC] Found at depth " << depth 
							 << " in page size " << m_page_size_list[i] 
							 << " at index " << hash_function_result 
							 << " with ppn " << current_entry->ppn[block_offset] 
							 << " at emulated address " 
							 << (emulated_table_address[i] + 64 * hash_function_result + block_offset) << std::endl;
#endif

					if (count)
						stats.num_accesses[i]++;

					ppn = current_entry->ppn[block_offset];
					page_size_result = m_page_size_list[i];

					visited_addresses.emplace_back(make_tuple(
						i,
						depth,
						(IntPtr)(emulated_table_address[i] + 64 * hash_function_result + block_offset),
						true));

					break;
				}
				// If entry is empty or the tag matches but the offset is invalid => page not mapped
				else if ((current_entry->tag == static_cast<IntPtr>(-1)) ||
						 ((current_entry->tag == tag) && (!current_entry->valid[block_offset])))
				{
#ifdef DEBUG
					log_file << "[HDC] Page fault at depth " << depth 
							 << " in page size " << m_page_size_list[i] 
							 << " at index " << hash_function_result << std::endl;
#endif

					if (count)
						stats.num_accesses[i]++;

					visited_addresses.emplace_back(make_tuple(
						i,
						depth,
						(IntPtr)(emulated_table_address[i] + 64 * hash_function_result + block_offset),
						false));

					break;
				}
				// If the tag is different, we have a collision => increment index and keep going
				else if (current_entry->tag != tag)
				{
					if (count)
					{
						stats.num_accesses[i]++;
						stats.collisions[i]++;
					}

#ifdef DEBUG
					log_file << "[HDC] Collision at depth " << depth << std::endl;
					log_file << "[HDC] Switching to next entry: "
							 << (hash_function_result + 1) % m_page_table_sizes[i]
							 << std::endl;
#endif
					visited_addresses.emplace_back(make_tuple(
						i,
						depth,
						(IntPtr)(emulated_table_address[i] + 64 * hash_function_result + block_offset),
						false));

					hash_function_result = (hash_function_result + 1) % m_page_table_sizes[i];
					current_entry = &page_tables[i][hash_function_result];
				}

				depth++;
			}

			// If we have probed all entries, the table is effectively full
			if (depth >= m_page_table_sizes[i])
			{
				std::cerr << "Page table is full" << std::endl;
				exit(1);
			}
		}

		// If no translation was found among all page sizes => page fault
		if (page_size_result == -1)
		{
#ifdef DEBUG
			log_file << "[HDC] Page fault in every page size: going to handle page fault" << std::endl;
#endif
			is_pagefault_in_every_page_size = true;

			if (restart_walk_after_fault)
				Sim()->getMimicOS()->handle_page_fault(address, core_id, 0);

#ifdef DEBUG
			log_file << "[HDC] Page fault resolved" << std::endl;
#endif
			if (restart_walk_after_fault)
				goto restart_walk;
			else
				return PTWResult(page_size_result, visited_addresses, ppn, SubsecondTime::Zero(), is_pagefault_in_every_page_size);
		}

		if (page_size_result != -1)
		{
			return PTWResult(page_size_result, visited_addresses, ppn, SubsecondTime::Zero(), is_pagefault_in_every_page_size);
		}

		// If we somehow get here, there's a logical inconsistency
		assert(false);
	}

	/*
	 * calculate_mean() and calculate_std():
	 *   - Optionally compute statistics about how far from the "root" (original index) 
	 *     each entry is (distance_from_root). This helps measure hash distribution quality.
	 */
	void PageTableHDC::calculate_mean()
	{
		for (int i = 0; i < m_page_sizes; i++)
		{
			double sum = 0;
			for (int j = 0; j < m_page_table_sizes[i]; j++)
			{
				if (page_tables[i][j].tag != static_cast<IntPtr>(-1))
				{
					sum += page_tables[i][j].distance_from_root;
				}
			}
			stats.mean[i].push_back(sum / m_page_table_sizes[i]);
		}
	}

	void PageTableHDC::calculate_std()
	{
		for (int i = 0; i < m_page_sizes; i++)
		{
			double sum = 0;
			for (int j = 0; j < m_page_table_sizes[i]; j++)
			{
				if (page_tables[i][j].tag != static_cast<IntPtr>(-1))
				{
					sum += pow(page_tables[i][j].distance_from_root - stats.mean[i].back(), 2);
				}
			}
			stats.std[i].push_back(sqrt(sum / m_page_table_sizes[i]));
		}
	}

	/*
	 * updatePageTableFrames(...):
	 *   - Inserts or updates a new PPN entry for a given address in the relevant page_size table.
	 *   - We do linear probing to find an empty slot (tag == -1) or matching tag 
	 *     (in which case we set the valid[block_offset] and ppn).
	 *   - distance_from_root is recorded to measure how many probes we had to do.
	 */
	int PageTableHDC::updatePageTableFrames(IntPtr address,
											IntPtr core_id,
											IntPtr ppn,
											int page_size,
											std::vector<UInt64> frames)
	{
#ifdef LOG_MEAN_STD
		// Every 10K page faults, recalc mean/std and log them
		if (stats.page_faults % 10000 == 0)
		{
			calculate_mean();
			calculate_std();

			printVectorStatistics();

			std::cout << "Heartbeat";
			std::cout << " Page faults: " << stats.page_faults;
			for (int i = 0; i < m_page_sizes; i++)
			{
				std::cout << " Page size: " << m_page_size_list[i];
				std::cout << " Mean: " << stats.mean[i].back();
				std::cout << " Standard deviation: " << stats.std[i].back();
			}
			std::cout << std::endl;
		}
#endif

#ifdef DEBUG
		log_file << "[HDC] Updating page table frames for address " 
				 << address << " with ppn " << ppn 
				 << " and page size " << page_size << std::endl;
#endif

		// Identify which page_size_index corresponds to the given page_size
		int page_size_index = 0;
		for (int i = 0; i < m_page_sizes; i++)
		{
			if (m_page_size_list[i] == page_size)
			{
				page_size_index = i;
				break;
			}
		}

		IntPtr VPN = address >> (m_page_size_list[page_size_index]);
		IntPtr tag = VPN >> 3;
		IntPtr block_offset = VPN % 8;

		// Compute the index in the table via hash function
		UInt64 hash_function_result = hashFunction(tag, m_page_table_sizes[page_size_index]);
		Entry *current_entry = &page_tables[page_size_index][hash_function_result];

		int depth = 0;

#ifdef DEBUG
		log_file << "[HDC] Searching for Tag: " 
				 << tag << " in page size: " << m_page_size_list[page_size_index] 
				 << " at index: " << hash_function_result << std::endl;
#endif

		// Perform linear probing to find a free slot or a slot that matches the tag
		while (depth < m_page_table_sizes[page_size_index])
		{
			// If the current entry is empty, fill it
			if (current_entry->tag == static_cast<IntPtr>(-1))
			{
#ifdef DEBUG
				log_file << "[HDC] Found empty entry at depth " << depth 
						 << " in page size " << m_page_size_list[page_size_index] 
						 << " at index " << hash_function_result << std::endl;
#endif
				current_entry->tag = tag;
				current_entry->valid[block_offset] = true;
				current_entry->ppn[block_offset] = ppn;
				current_entry->distance_from_root = depth;
				break;
			}
			// If the tag matches but that particular offset is not valid, we can claim it
			else if (current_entry->tag == tag && !current_entry->valid[block_offset])
			{
#ifdef DEBUG
				log_file << "[HDC] Found entry with matching tag at depth " << depth 
						 << " in page size " << m_page_size_list[page_size_index] 
						 << " at index " << hash_function_result << std::endl;
#endif
				current_entry->valid[block_offset] = true;
				current_entry->ppn[block_offset] = ppn;
				break;
			}
			else
			{
#ifdef DEBUG
				log_file << "[HDC] Collision at depth " << depth 
						 << " in page size " << m_page_size_list[page_size_index] 
						 << " at index " << hash_function_result << std::endl;
#endif
				// Go to the next index, wrapping around as needed
				hash_function_result = (hash_function_result + 1) % m_page_table_sizes[page_size_index];
				current_entry = &page_tables[page_size_index][hash_function_result];
			}

			depth++;
		}

		return 0;
	}

	/*
	 * printPageTable():
	 *   - Displays the contents of each page table array for debugging. 
	 *     Shows the tag, valid bits, and first four PPNs.
	 */
	void PageTableHDC::printPageTable()
	{
		for (int i = 0; i < m_page_sizes; i++)
		{
			cout << "Page table for page size " << m_page_size_list[i] << endl;
			for (int j = 0; j < m_page_table_sizes[i]; j++)
			{
				cout << "Entry " << j << ": " << endl;
				cout << "Tag: " << page_tables[i][j].tag << endl;
				cout << "Empty: " 
					 << page_tables[i][j].valid[0] 
					 << page_tables[i][j].valid[1] 
					 << page_tables[i][j].valid[2] 
					 << page_tables[i][j].valid[3] << endl;
				cout << "PPN: " 
					 << page_tables[i][j].ppn[0] 
					 << page_tables[i][j].ppn[1] 
					 << page_tables[i][j].ppn[2] 
					 << page_tables[i][j].ppn[3] << endl;
			}
		}
	}

	/*
	 * deletePage(...):
	 *   - Removes the entry for the given 'address' from the base page table (index 0).
	 *   - If the correct tag/offset is found, it sets valid=false. If the tag is mismatched, 
	 *     linear probe continues. If an empty slot is reached, we stop (the page is not found).
	 */
	void PageTableHDC::deletePage(IntPtr address)
	{
		IntPtr VPN = address >> (m_page_size_list[0]);
		IntPtr tag = VPN >> 3;
		IntPtr block_offset = VPN % 8;

		UInt64 hash_function_result = hashFunction(tag, m_page_table_sizes[0]);

		Entry *current_entry = &page_tables[0][hash_function_result];

		int depth = 0;

		while (true)
		{
			if ((current_entry->tag == tag) && (current_entry->valid[block_offset]))
			{
				current_entry->valid[block_offset] = false;
				break;
			}
			else if (current_entry->tag != static_cast<IntPtr>(-1))
			{
				hash_function_result = (hash_function_result + 1) % m_page_table_sizes[0];
				current_entry = &page_tables[0][hash_function_result];
				depth++;
			}
			else
			{
				break;
			}
		}
	}

} // namespace ParametricDramDirectoryMSI
