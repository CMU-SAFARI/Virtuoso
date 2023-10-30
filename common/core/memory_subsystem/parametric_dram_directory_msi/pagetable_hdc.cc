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
// #define DEBUG

namespace ParametricDramDirectoryMSI
{
	int PageTableHDC::hashFunction(IntPtr address, int table_size)
	{
		// uint64 result = CityHash64((const char *)&address, 8) % table_size;
		uint64 result = address % table_size;
		return result;
	}
	PageTableHDC::PageTableHDC(int core_id, String name, int page_sizes, int *page_size_list, int *page_table_sizes) : PageTable(core_id, name, page_sizes, page_size_list),
																													   m_page_table_sizes(page_table_sizes)
	{
		emulated_table_address = (IntPtr *)malloc(sizeof(IntPtr) * m_page_sizes);

		// std::cout << "Supporting " << m_page_sizes << " page sizes" << std::endl;
		//  Implement the constructor logic here
		for (int i = 0; i < m_page_sizes; i++)
		{

			// std::cout << "Page table size for page size " << m_page_size_list[i] << " is " << m_page_table_sizes[i] << std::endl;
			emulated_table_address[i] = Sim()->getVirtuOS()->getMemoryAllocator()->allocate(32 * page_table_sizes[i]);
			Entry *page_table = (Entry *)malloc(sizeof(Entry) * m_page_table_sizes[i]);
			// std::cout << "Page table size in kilo bytes " << (sizeof(Entry) * m_page_table_sizes[i]) / 1024 / 1024 << std::endl;
			for (int j = 0; j < m_page_table_sizes[i]; j++)
			{
				page_table[j].empty[0] = true;
				page_table[j].empty[1] = true;
				page_table[j].empty[2] = true;
				page_table[j].empty[3] = true;
				page_table[j].ppn[0] = 0;
				page_table[j].ppn[1] = 0;
				page_table[j].ppn[2] = 0;
				page_table[j].ppn[3] = 0;
				page_table[j].tag = -1;
			}
			page_tables.push_back(page_table);

			stats.page_table_walks = new uint64[m_page_sizes];
			stats.num_accesses = new uint64[m_page_sizes];
			stats.collisions = new uint64[m_page_sizes];

			stats.page_faults = 0;
		}

		for (int i = 0; i < m_page_sizes; i++)
		{
			registerStatsMetric(name, core_id, ("page_table_walks_" + std::to_string(m_page_size_list[i])).c_str(), &stats.page_table_walks[i]);
			registerStatsMetric(name, core_id, ("accesses_" + std::to_string(m_page_size_list[i])).c_str(), &stats.num_accesses[i]);
			registerStatsMetric(name, core_id, ("collisions_" + std::to_string(m_page_size_list[i])).c_str(), &stats.collisions[i]);
		}

		registerStatsMetric(name, core_id, "page_fault", &stats.page_faults);
	}

	PTWResult PageTableHDC::initializeWalk(IntPtr address, bool count)
	{
		accessedAddresses visited_addresses;
		bool found_at_depth = false;
		int page_size_result = 0;
		IntPtr ppn = 0;
		bool is_pagefault = false;

		for (int i = 0; i < m_page_sizes; i++)
		{
			if (count)
				stats.page_table_walks[i]++;

			int hash_function_result = hashFunction(address >> (m_page_size_list[i] + 2), m_page_table_sizes[i]);
			Entry *current_entry = &page_tables[i][hash_function_result];
			IntPtr tag = (address >> (m_page_size_list[i] + 2));
			IntPtr block_offset = (address >> m_page_size_list[i]) & 0x3;
			int depth = 0;

			while (true)
			{
				if ((current_entry->tag == tag) && (!current_entry->empty[block_offset]))
				{
#ifdef DEBUG
					cout << "Found at depth " << depth << endl;
#endif

					if (count)
						stats.num_accesses[i]++;
					ppn = current_entry->ppn[block_offset];
					page_size_result = m_page_size_list[i];
					visited_addresses.push_back(make_tuple(i, depth, (IntPtr)(emulated_table_address[i] + 32 * hash_function_result), true));
					is_pagefault = false;
					goto PTW_HDC_HIT;
				}
				else if ((current_entry->tag == tag) && (current_entry->empty[block_offset]))
				{
#ifdef DEBUG
					cout << "Page fault at depth " << depth << endl;
#endif
					if (count)
						stats.num_accesses[i]++;
					visited_addresses.push_back(make_tuple(i, depth, (IntPtr)(emulated_table_address[i] + 32 * hash_function_result), false));
					is_pagefault = true;

					break;
				}
				else if (current_entry->tag != -1)
				{
#ifdef DEBUG
					cout << "Collision at depth " << depth << endl;
#endif

					if (count)
					{
						stats.num_accesses[i]++;
						stats.collisions[i]++;
					}

					visited_addresses.push_back(make_tuple(i, depth, (IntPtr)(emulated_table_address[i] + 32 * hash_function_result), false));
					hash_function_result = (hash_function_result + 1) % m_page_table_sizes[i];
					current_entry = &page_tables[i][hash_function_result];
					depth++;
				}
				else
				{
#ifdef DEBUG
					cout << "Page fault with empty tag at depth " << depth << endl;
#endif

					if (count)
						stats.num_accesses[i]++;
					visited_addresses.push_back(make_tuple(i, depth, (IntPtr)(emulated_table_address[i] + 32 * hash_function_result), false));
					is_pagefault = true;

					break;
				}
			}
			if (is_pagefault)
			{
				break;
			}
		}
	PTW_HDC_HIT:
		if (is_pagefault)
		{
			AllocatedPage page = handlePageFault(address, count);
			visited_addresses.insert(visited_addresses.end(), get<1>(page).begin(), get<1>(page).end());
			return PTWResult(get<0>(page), visited_addresses, get<2>(page));
		}
		else
		{
			return PTWResult(page_size_result, visited_addresses, ppn);
		}
	}

	AllocatedPage PageTableHDC::handlePageFault(IntPtr address, bool count, IntPtr override_ppn)
	{

#ifdef DEBUG
		std::cout << "Handling page fault at address " << (address >> 12) << std::endl;
#endif
		if (count)
			stats.page_faults++;

		IntPtr ppn;
		if (override_ppn != -1)
		{
			ppn = override_ppn;
		}
		else
		{
			ppn = Sim()->getVirtuOS()->getMemoryAllocator()->allocate(4096, address, core_id);
		}
		int page_size = 12;
		int page_size_index = 0;

		for (int i = 0; i < m_page_sizes; i++)
		{
			if (m_page_size_list[i] == page_size)
			{
				page_size_index = i;
				break;
			}
		}

		accessedAddresses pagefault_addresses;

		int hash_function_result = hashFunction(address >> (m_page_size_list[page_size_index] + 2), m_page_table_sizes[page_size_index]);
		Entry *current_entry = &page_tables[page_size_index][hash_function_result];
		IntPtr tag = (address >> (m_page_size_list[page_size_index] + 2));

		IntPtr block_offset = (address >> m_page_size_list[page_size_index]) & 0x3;
		int depth = 0;

		while (true)
		{
			if (current_entry->tag == -1)
			{
				pagefault_addresses.push_back(make_tuple(page_size_index, depth, (IntPtr)(emulated_table_address[page_size_index] + 32 * hash_function_result), false));
				current_entry->tag = tag;
				current_entry->empty[block_offset] = false;
				current_entry->ppn[block_offset] = ppn;
				// std::cout << "Allocating physical page at entry with hash " << hash_function_result << " tag " << tag << " block offset " << block_offset << std::endl;
				break;
			}
			else if (current_entry->tag == tag && current_entry->empty[block_offset])
			{
				pagefault_addresses.push_back(make_tuple(page_size_index, depth, (IntPtr)(emulated_table_address[page_size_index] + 32 * hash_function_result), false));
				current_entry->empty[block_offset] = false;
				current_entry->ppn[block_offset] = ppn;
				break;
			}
			else
			{
				pagefault_addresses.push_back(make_tuple(page_size_index, depth, (IntPtr)(emulated_table_address[page_size_index] + 32 * hash_function_result), false));
				hash_function_result = (hash_function_result + 1) % m_page_table_sizes[page_size_index];
				current_entry = &page_tables[page_size_index][hash_function_result];
				depth++;
			}
		}

		return AllocatedPage(page_size, pagefault_addresses, ppn);
	}

	void PageTableHDC::printPageTable()
	{
		for (int i = 0; i < m_page_sizes; i++)
		{
			cout << "Page table for page size " << m_page_size_list[i] << endl;
			for (int j = 0; j < m_page_table_sizes[i]; j++)
			{
				cout << "Entry " << j << ": " << endl;
				cout << "Tag: " << page_tables[i][j].tag << endl;
				cout << "Empty: " << page_tables[i][j].empty[0] << page_tables[i][j].empty[1] << page_tables[i][j].empty[2] << page_tables[i][j].empty[3] << endl;
				cout << "PPN: " << page_tables[i][j].ppn[0] << page_tables[i][j].ppn[1] << page_tables[i][j].ppn[2] << page_tables[i][j].ppn[3] << endl;
			}
		}
	}
	void PageTableHDC::deletePage(IntPtr address)
	{

		int hash_function_result = hashFunction(address >> (m_page_size_list[0] + 2), m_page_table_sizes[0]);

		Entry *current_entry = &page_tables[0][hash_function_result];
		IntPtr tag = (address >> ((int)log2(m_page_table_sizes[0]) + 2));
		IntPtr block_offset = (address >> (int)log2(m_page_table_sizes[0])) & 0x3;
		int depth = 0;

		while (true)
		{
			if ((current_entry->tag == tag) && (!current_entry->empty[block_offset]))
			{
				current_entry->empty[block_offset] = true;
				break;
			}
			else if ((current_entry->tag == tag) && (current_entry->empty[block_offset]))
			{

				break;
			}
			else if (current_entry->tag != -1)
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
	void PageTableHDC::insertLargePage(IntPtr address, IntPtr ppn)
	{
		int page_size = 21;
		int page_size_index = 0;

		for (int i = 0; i < m_page_sizes; i++)
		{
			if (m_page_size_list[i] == page_size)
			{
				page_size_index = i;
				break;
			}
		}

		accessedAddresses pagefault_addresses;

		int hash_function_result = hashFunction(address >> (m_page_size_list[page_size_index] + 2), m_page_table_sizes[page_size_index]);
		Entry *current_entry = &page_tables[page_size_index][hash_function_result];
		IntPtr tag = (address >> ((int)log2(m_page_table_sizes[page_size_index]) + 2));
		IntPtr block_offset = (address >> (int)log2(m_page_table_sizes[page_size_index])) & 0x3;
		int depth = 0;

		while (true)
		{
			if (current_entry->tag == -1)
			{
				pagefault_addresses.push_back(make_tuple(page_size_index, depth, (IntPtr)(emulated_table_address[page_size_index] + 32 * hash_function_result), false));
				current_entry->tag = tag;
				current_entry->empty[block_offset] = false;
				current_entry->ppn[block_offset] = ppn;
				break;
			}
			else if (current_entry->tag == tag && current_entry->empty[block_offset])
			{
				pagefault_addresses.push_back(make_tuple(page_size_index, depth, (IntPtr)(emulated_table_address[page_size_index] + 32 * hash_function_result), false));
				current_entry->empty[block_offset] = false;
				current_entry->ppn[block_offset] = ppn;
				break;
			}
			else
			{
				pagefault_addresses.push_back(make_tuple(page_size_index, depth, (IntPtr)(emulated_table_address[page_size_index] + 32 * hash_function_result), false));
				hash_function_result = (hash_function_result + 1) % m_page_table_sizes[page_size_index];
				current_entry = &page_tables[page_size_index][hash_function_result];
				depth++;
			}
		}
	}
}
