#pragma once
#include "subsecond_time.h"
#include "fixed_types.h"
#include "city.h"
#include <stdint.h>
#include <vector>
#include <fstream>
#include <iostream>

namespace ParametricDramDirectoryMSI
{
	class PageTableCuckoo : public PageTable
	{
	private:
		struct Element
		{
			IntPtr tag;
			bool validityBits[8];
			IntPtr frames[8];

			Element(int t = -1) : tag(t)
			{
				for (int i = 0; i < 8; ++i)
				{
					validityBits[i] = false;
					frames[i] = -1;
				}
			}
		};

		Element ***tables;
		UInt64 **table_ppns;

		int *numItems;
		double loadFactor;
		int *m_page_table_sizes;
		int m_ways;
		float m_scale;

		std::vector<Element> nonResidentEntries;

		struct
		{
			UInt64 page_walks_total;
			UInt64 cuckoo_hits;
			UInt64 cuckoo_evictions;
			UInt64 rehashes;
			UInt64 *cuckoo_accesses;
			UInt64 *cuckoo_hits_per_level;
		} cuckoo_stats;

		std::ofstream log_file;
		std::string log_file_name;

		void accessTable(IntPtr address);

	public:
		PageTableCuckoo(int core_id, String name, String type, int page_sizes, int *page_size_list,
						int *page_table_sizes, double rehash_threshold, float scale, int ways, bool is_guest = false);

		~PageTableCuckoo()
		{
			for (int i = 0; i < m_page_sizes; ++i)
			{
				for (int j = 0; j < m_ways; ++j)
				{
					delete[] tables[i][j];
				}
				delete[] tables[i];
				delete[] table_ppns[i];
			}
			delete[] tables;
			delete[] table_ppns;
			delete[] numItems;
			delete[] m_page_table_sizes;
		}

		PTWResult initializeWalk(IntPtr address, bool count, bool is_prefetch = false, bool restart_walk = false);
		uint64 hash(uint64 key, int tableIndex);
		std::tuple<bool, accessedAddresses> insertElement(int page_size_index, Element entry, IntPtr address);

		int updatePageTableFrames(IntPtr address, IntPtr core_id, IntPtr ppn, int page_size, std::vector<UInt64> frames);

		IntPtr getPhysicalSpace(int size);
		bool rehash(int page_size_index, int new_size);
		double currentLoadFactor(int page_size_index) const;

		void display() const;
		void deletePage(IntPtr address);
	};
}
