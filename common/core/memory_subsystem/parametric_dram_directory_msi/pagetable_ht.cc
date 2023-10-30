
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
#include "virtuos.h"
#include "physical_memory_allocator.h"

namespace ParametricDramDirectoryMSI
{

	int PageTableHT::hashFunction(IntPtr address, int table_size)
	{
		uint64 result = CityHash64((const char *)&address, 8) % table_size;
		return result;
	}

	PageTableHT::PageTableHT(int core_id, String name, int page_sizes, int *page_size_list, int *page_table_sizes) : PageTable(core_id, name, page_sizes, page_size_list),
																													 m_page_table_sizes(page_table_sizes)
	{
		// Implement the constructor logic here
		for (int i = 0; i < m_page_sizes; i++)
		{

			Entry *page_table = (Entry *)malloc(sizeof(Entry) * m_page_table_sizes[i]);
			for (int j = 0; j < m_page_table_sizes[i]; j++)
			{
				page_table[j].empty = true;
				page_table[j].vpn = 0;
				page_table[j].ppn = 0;
				page_table[j].next_entry = NULL;
			}
			page_tables.push_back(page_table);

			stats.page_table_walks = new uint64[m_page_sizes];
			stats.chain_per_entry = new uint64[m_page_sizes];
			stats.num_accesses = new uint64[m_page_sizes];
			stats.latency = new SubsecondTime[m_page_sizes];
			stats.total_latency = new SubsecondTime[m_page_sizes];

			stats.page_faults = 0;
		}

		for (int i = 0; i < m_page_sizes; i++)
		{
			registerStatsMetric(name, core_id, ("page_table_walks_" + std::to_string(m_page_size_list[i])).c_str(), &stats.page_table_walks[i]);
			registerStatsMetric(name, core_id, ("conflicts_" + std::to_string(m_page_size_list[i])).c_str(), &stats.chain_per_entry[i]);
			registerStatsMetric(name, core_id, ("accesses_" + std::to_string(m_page_size_list[i])).c_str(), &stats.num_accesses[i]);
			registerStatsMetric(name, core_id, ("latency_" + std::to_string(m_page_size_list[i])).c_str(), &stats.latency[i]);
			registerStatsMetric(name, core_id, ("total_latency_" + std::to_string(m_page_size_list[i])).c_str(), &stats.total_latency[i]);
		}

		registerStatsMetric(name, core_id, "page_fault", &stats.page_faults);
	}

	PTWResult PageTableHT::initializeWalk(IntPtr address, bool count)
	{

		std::vector<tuple<int, int, IntPtr, bool>> visited_addresses;
		bool page_fault = true;
		int page_size_result = 0;
		IntPtr ppn_result = 0;

		for (int i = 0; i < m_page_sizes; i++)
		{

			if (count)
				stats.page_table_walks[i]++;

			int hash_function_result = hashFunction(address >> m_page_size_list[i], m_page_table_sizes[i]);
			Entry *current_entry = &page_tables[i][hash_function_result];

			if (count)
				stats.num_accesses[i]++;
			int counter = 0;

			while (true)
			{

				if (!(current_entry->empty))
				{
					if (current_entry->vpn == (address >> m_page_size_list[i]))
					{
						// std::cout << current_entry->vpn << " " << (address >> m_page_size_list[i]) << std::endl;

						// the tuple contains the page size, the counter of the entry, the address of the entry and a boolean that indicates if the entry delivers the translation
						visited_addresses.push_back(make_tuple(i, counter, (IntPtr)(current_entry), true));
						page_size_result = m_page_size_list[i];
						ppn_result = current_entry->ppn;
						page_fault = false;
						goto PTW_HIT;
					}
					else
					{

						if (current_entry->next_entry == NULL)
							break;

						else if (current_entry->next_entry != NULL)
						{

							if (count)
							{
								stats.num_accesses[i]++;
								stats.chain_per_entry[i]++;
								// std::cout << hash_function_result << "\n";
							}

							visited_addresses.push_back(make_tuple(i, counter, (IntPtr)(current_entry), false));
							current_entry = current_entry->next_entry;
							counter++;
						}
					}
				}
				else
				{

					break;
				}
			}
		}
	PTW_HIT:
		if (page_fault)
		{
			AllocatedPage page = handlePageFault(address, count);
			visited_addresses.insert(visited_addresses.end(), get<1>(page).begin(), get<1>(page).end());
			// std::cout << "Page fault at address: " << address << std::endl;
			return PTWResult(get<0>(page), visited_addresses, get<2>(page));
		}
		else
		{

			return PTWResult(page_size_result, visited_addresses, ppn_result);
		}
	}

	AllocatedPage PageTableHT::handlePageFault(IntPtr address, bool count, IntPtr override_ppn)
	{
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
		// Allocator will take a reference to the page table so that it calls the promotion function of the page table

		// printPageTable();

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

		int hash_function_result = hashFunction(address >> m_page_size_list[page_size_index], m_page_table_sizes[page_size_index]);

		Entry *current_entry = &page_tables[page_size_index][hash_function_result];
		accessedAddresses pagefault_addresses;

		int counter = 0;

		while (true)
		{
			if (current_entry->empty)
			{
				pagefault_addresses.push_back(make_tuple(0, counter, (IntPtr)(current_entry), false));
				current_entry->empty = false;
				current_entry->vpn = address >> page_size;
				current_entry->ppn = ppn;
				current_entry->next_entry = NULL;
				break;
			}
			else if (current_entry->next_entry == NULL)
			{
				Entry *new_entry = (Entry *)malloc(sizeof(Entry));
				new_entry->empty = false;
				new_entry->vpn = address >> page_size;
				new_entry->ppn = ppn;
				new_entry->next_entry = NULL;
				current_entry->next_entry = new_entry;
				pagefault_addresses.push_back(make_tuple(0, counter, (IntPtr)(new_entry), false));
				break;
			}
			else
			{
				pagefault_addresses.push_back(make_tuple(0, counter, (IntPtr)(current_entry), false));
				current_entry = current_entry->next_entry;
				counter++;
			}
		}
		return AllocatedPage(page_size, pagefault_addresses, ppn);
	}

	void PageTableHT::printPageTable()
	{

		for (int i = 0; i < m_page_sizes; i++)
		{

			std::cout << "[HashPageTable] Page Table for page size: " << m_page_size_list[i] << std::endl;

			for (int j = 0; j < m_page_table_sizes[i]; j++)
			{

				if ((page_tables[i][j].empty))
				{
					// /std::cout << "Empty" << std::endl;
				}
				else
				{
					std::cout << "Size: " << m_page_size_list[i] << " ID: " << j << " VPN: " << (page_tables[i][j].vpn >> m_page_size_list[i]) << " PPN: " << page_tables[i][j].ppn;

					Entry *pointers = page_tables[i][j].next_entry;
					while (pointers != NULL)
					{
						std::cout << " --> Size: " << m_page_size_list[i] << " ID: " << j << " VPN: " << (pointers->vpn >> m_page_size_list[i]) << " PPN: " << pointers->ppn;
						pointers = pointers->next_entry;
					}
					std::cout << std::endl;
				}
			}
		}
	}

	void PageTableHT::insertLargePage(IntPtr address, IntPtr ppn)
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

		int hash_function_result = hashFunction(address >> m_page_size_list[page_size_index], m_page_table_sizes[page_size_index]);

		Entry *current_entry = &page_tables[page_size_index][hash_function_result];
		accessedAddresses pagefault_addresses;

		int counter = 0;

		while (true)
		{
			if (current_entry->empty)
			{
				pagefault_addresses.push_back(make_tuple(0, counter, (IntPtr)(current_entry), false));
				current_entry->empty = false;
				current_entry->vpn = address >> page_size;
				current_entry->ppn = ppn;
				current_entry->next_entry = NULL;
				break;
			}
			else if (current_entry->next_entry == NULL)
			{
				Entry *new_entry = (Entry *)malloc(sizeof(Entry));
				new_entry->empty = false;
				new_entry->vpn = address >> page_size;
				new_entry->ppn = ppn;
				new_entry->next_entry = NULL;
				current_entry->next_entry = new_entry;
				pagefault_addresses.push_back(make_tuple(0, counter, (IntPtr)(new_entry), false));
				break;
			}
			else
			{
				pagefault_addresses.push_back(make_tuple(0, counter, (IntPtr)(current_entry), false));
				current_entry = current_entry->next_entry;
				counter++;
			}
		}
	}
	void PageTableHT::deletePage(IntPtr address)
	{

		int hash_function_result = hashFunction(address >> m_page_size_list[0], m_page_table_sizes[0]);
		Entry *current_entry = &page_tables[0][hash_function_result];
		while (true)
		{

			if (!(current_entry->empty))
			{
				if (current_entry->vpn == (address >> m_page_size_list[0]))
				{

					// the tuple contains the page size, the counter of the entry, the address of the entry and a boolean that indicates if the entry delivers the translation
					current_entry->empty = true;
					break;
				}
				else
				{

					if (current_entry->next_entry == NULL)
						break;

					else if (current_entry->next_entry != NULL)
					{
						current_entry = current_entry->next_entry;
					}
				}
			}
			else
				break;
		}
	}

} // namespace ParametricDramDirectoryMSI