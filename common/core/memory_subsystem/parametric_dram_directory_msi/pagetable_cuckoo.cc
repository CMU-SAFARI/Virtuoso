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
#include "pagetable_cuckoo.h"
#include "simulator.h"
#include "physical_memory_allocator.h"
#include "virtuos.h"

namespace ParametricDramDirectoryMSI
{
	int PageTableCuckoo::hash(int key, int tableIndex) const
	{
		return key % tables[tableIndex].size() + tableIndex;
	}
	void PageTableCuckoo::rehash()
	{
		// std::cout << "[ECH] Rehashing! Scaling the tables \n";
		int newSize = 2 * tables[0].size();
		std::vector<std::vector<Element>> oldTables = tables;
		for (int i = 0; i < tables.size(); i++)
		{
			Sim()->getVirtuOS()->getMemoryAllocator()->deallocate(64 * oldTables[i].size());
			for (int a = 0; a < ceil((64 * newSize) / 4096); a++)
			{
				if (a == 0)
				{
					table_ppns[i] = Sim()->getVirtuOS()->getMemoryAllocator()->allocate(4096);
				}
			}

			tables[i].resize(newSize, Element());
		}
		numItems = 0;
		for (const auto &oldTable : oldTables)
		{
			for (const Element &e : oldTable)
			{
				if (e.tag != -1)
				{
					int indexInsideBlock = e.tag % 8;

					for (int i = 0; i < tables.size(); ++i)
					{
						int pos = hash(e.tag, i);
						if (tables[i][pos].tag == -1)
						{
							tables[i][pos] = e;
							numItems++;
							tables[i][pos].validityBits[indexInsideBlock] = true;

							break;
						}
						else if (tables[i][pos].validityBits[indexInsideBlock] == false)
						{
							tables[i][pos] = e;
							tables[i][pos].validityBits[indexInsideBlock] = true;
							break;
						}
					}
				}
			}
		}
	}
	double PageTableCuckoo::currentLoadFactor() const
	{
		int totalCapacity = 0;
		for (const auto &table : tables)
		{
			totalCapacity += table.size();
		}
		return (1.0 * numItems) / totalCapacity;
	}
	PageTableCuckoo::PageTableCuckoo(int core_id, String name, int page_sizes, int *page_size_list,
									 int *page_table_sizes, double rehash_threshold,
									 int scale, int ways) : PageTable(core_id, name, page_sizes, page_size_list),
															m_page_table_sizes(page_table_sizes)
	{
		loadFactor = rehash_threshold;
		for (int a = 0; a < m_page_sizes; a++)
		{
			tables.resize(ways);
			table_ppns.resize(ways);
			for (int i = 0; i < ways; i++)
			{
				for (int b = 0; b < ceil((64 * m_page_table_sizes[a]) / 4096); b++)
				{
					if (b == 0)
					{
						table_ppns[i] = Sim()->getVirtuOS()->getMemoryAllocator()->allocate(4096);
					}
				}
				tables[i].resize(page_table_sizes[a], Element());
			}
		}

		stats.cuckoo_faults = 0;
		stats.cuckoo_latency = SubsecondTime::Zero();
		stats.cuckoo_hits = 0;
		stats.page_walks_total = 0;
		stats.cuckoo_hits_per_level = (UInt64 *)malloc(sizeof(UInt64) * page_sizes);
		registerStatsMetric(name, core_id, "ptws", &stats.page_walks_total);
		registerStatsMetric(name, core_id, "hits", &stats.cuckoo_hits);
		for (int i = 0; i < page_sizes; i++)
		{
			stats.cuckoo_hits_per_level[i] = 0;
			registerStatsMetric(name, core_id, "hit_at_level" + itostr(i), &stats.cuckoo_hits_per_level[i]);
		}
		registerStatsMetric(name, core_id, "pagefaults", &stats.cuckoo_faults);
		registerStatsMetric(name, core_id, "latency", &stats.cuckoo_latency);
	}
	PTWResult PageTableCuckoo::initializeWalk(IntPtr address, bool count)
	{
		// std::cout << "Address : " << address << "\n";
		stats.page_walks_total++;
		accessedAddresses visitedAddresses;
		u_int page_size_result = 0;
		IntPtr ppn_result = 0;
		bool found = false;
		for (int i = 0; i < m_page_sizes; i++)
		{
			IntPtr VPN = address >> m_page_size_list[i];
			IntPtr offset = address & ((2 ^ m_page_size_list[i]) - 1);
			IntPtr tag = VPN >> 3;
			IntPtr indexInsideBlock = VPN % 8;
			// std::cout << VPN << " " << offset << " " << tag << " " << indexInsideBlock << "\n";
			//  std::cout << "Index inside block : " << indexInsideBlock << "\n";
			for (int a = 0; a < tables.size(); ++a)
			{

				int pos = hash(tag, a);
				if (tables[a][pos].tag == tag && tables[a][pos].validityBits[indexInsideBlock])
				{
					ppn_result = tables[a][pos].frames[indexInsideBlock];
					found = true;
					// std::cout << "Found in table " << a << "\n";
					visitedAddresses.push_back(make_tuple(i, 0, (IntPtr)(table_ppns[a] + offset), true));
					page_size_result = m_page_size_list[i];
					if (count)
					{
						stats.cuckoo_hits++;
						stats.cuckoo_hits_per_level[a]++;
					}
					break;
				}
				else
				{
					visitedAddresses.push_back(make_tuple(i, 0, (IntPtr)(table_ppns[a] + offset), false));
				}
			}
		}
		if (!found)
		{
			// std::cout << "Translation not found!\n";
			AllocatedPage page = handlePageFault(address, count);
			// std::cout << "Size of visited addresses : " << visitedAddresses.size() << "\n";
			// std::cout << "Size of pf_addresses : " << get<1>(page).size() << "\n";

			visitedAddresses.insert(visitedAddresses.end(), get<1>(page).begin(), get<1>(page).end());

			return PTWResult(get<0>(page), visitedAddresses, get<2>(page));
		}
		else
		{
			// std::cout << "Translation found!\n";
			// std::cout << page_size_result << " " << ppn_result << "\n";
			// std::cout << "Size of visited addresses : " << visitedAddresses.size() << "\n";
			return PTWResult(page_size_result, visitedAddresses, ppn_result);
		}
	}
	AllocatedPage PageTableCuckoo::handlePageFault(IntPtr address, bool count, IntPtr override_ppn)
	{
		if (count)
			stats.cuckoo_faults++;
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
		accessedAddresses visitedAddresses;
		// Reserve 6 slots in the visited addresses vector for the cuckoo search

		IntPtr VPN = address >> m_page_size_list[page_size_index];
		IntPtr tag = VPN >> 3;
		IntPtr indexInsideBlock = VPN % 8;
		IntPtr ppn;
		// std::cout << "Tag in PF : " << tag << "\n";
		// std::cout << "Index inside block in PF : " << indexInsideBlock << "\n";

		if (override_ppn != -1)
		{
			ppn = override_ppn;
		}
		else
		{
			ppn = Sim()->getVirtuOS()->getMemoryAllocator()->allocate(4096, address, core_id);
		}
		// std::cout << "[ECH] Current load factor : " << currentLoadFactor() << "\n";
		if (currentLoadFactor() > loadFactor)
		{
			rehash();
		}

		for (int i = 0; i < tables.size(); ++i)
		{

			int pos = hash(tag, i);
			visitedAddresses.push_back(make_tuple(page_size_index, 0, (IntPtr)(table_ppns[i] + pos * 64), false));
			// std::cout << "Position in table " << i << " : " << pos << "with tag " << tables[i][pos].tag << "and index inside block " << indexInsideBlock << "and validity bit " << tables[i][pos].validityBits[indexInsideBlock] << "\n";
			if (tables[i][pos].tag == -1)
			{
				tables[i][pos].tag = tag;
				tables[i][pos].frames[indexInsideBlock] = ppn;
				numItems++;
				tables[i][pos].validityBits[indexInsideBlock] = true;
				// std::cout << "Size of visited addresses PF: " << visitedAddresses.size() << "\n";

				return AllocatedPage(page_size, visitedAddresses, ppn);
			}
			else if (tables[i][pos].tag == tag && tables[i][pos].validityBits[indexInsideBlock] == false)
			{
				tables[i][pos].frames[indexInsideBlock] = ppn;
				tables[i][pos].validityBits[indexInsideBlock] = true;
				// std::cout << "Size of visited addresses PF: " << visitedAddresses.size() << "\n";
				return AllocatedPage(page_size, visitedAddresses, ppn);
			}
			else
			{
			}
		}
		rehash();
		return AllocatedPage(page_size, visitedAddresses, ppn);
	}
	void PageTableCuckoo::deletePage(IntPtr address)
	{
		// std::cout << "Address : " << address << "\n";
		bool found = false;
		for (int i = 0; i < m_page_sizes; i++)
		{
			IntPtr VPN = address >> m_page_size_list[i];
			IntPtr offset = address & ((2 ^ m_page_size_list[i]) - 1);
			IntPtr tag = VPN >> 3;
			IntPtr indexInsideBlock = VPN % 8;
			// std::cout << VPN << " " << offset << " " << tag << " " << indexInsideBlock << "\n";
			//  std::cout << "Index inside block : " << indexInsideBlock << "\n";
			for (int a = 0; a < tables.size(); ++a)
			{

				int pos = hash(tag, a);
				if (tables[a][pos].tag == tag && tables[a][pos].validityBits[indexInsideBlock])
				{

					tables[a][pos].validityBits[indexInsideBlock] = false;
					break;
				}
			}
		}
	}
	void PageTableCuckoo::insertLargePage(IntPtr address, IntPtr ppn)
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
		accessedAddresses visitedAddresses;
		// Reserve 6 slots in the visited addresses vector for the cuckoo search

		IntPtr VPN = address >> m_page_size_list[page_size_index];
		IntPtr tag = VPN >> 3;
		IntPtr indexInsideBlock = VPN % 8;
		// std::cout << "Tag in PF : " << tag << "\n";
		// std::cout << "Index inside block in PF : " << indexInsideBlock << "\n";
		//  std::cout << "[ECH] Current load factor : " << currentLoadFactor() << "\n";
		if (currentLoadFactor() > loadFactor)
		{
			rehash();
		}

		int pos = hash(tag, page_size_index);
		visitedAddresses.push_back(make_tuple(page_size_index, 0, (IntPtr)(table_ppns[page_size_index] + pos * 64), false));
		// std::cout << "Position in table " << i << " : " << pos << "with tag " << tables[i][pos].tag << "and index inside block " << indexInsideBlock << "and validity bit " << tables[i][pos].validityBits[indexInsideBlock] << "\n";
		if (tables[page_size_index][pos].tag == -1)
		{
			tables[page_size_index][pos].tag = tag;
			tables[page_size_index][pos].frames[indexInsideBlock] = ppn;
			numItems++;
			tables[page_size_index][pos].validityBits[indexInsideBlock] = true;
			return;
			// std::cout << "Size of visited addresses PF: " << visitedAddresses.size() << "\n";
		}
		else if (tables[page_size_index][pos].tag == tag && tables[page_size_index][pos].validityBits[indexInsideBlock] == false)
		{
			tables[page_size_index][pos].frames[indexInsideBlock] = ppn;
			tables[page_size_index][pos].validityBits[indexInsideBlock] = true;
			return;
			// std::cout << "Size of visited addresses PF: " << visitedAddresses.size() << "\n";
		}
		else
		{
		}

		rehash();
	}
}
