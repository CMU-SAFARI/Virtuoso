#pragma once
#include "subsecond_time.h"
#include "fixed_types.h"
#include "city.h"
#include <stdint.h>
#include <vector>

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
		std::vector<std::vector<Element>> tables;
		std::vector<int> table_ppns;
		int numItems;
		double loadFactor;
		int *m_page_table_sizes;

		struct Stats
		{
			UInt64 page_walks_total;
			UInt64 cuckoo_faults;
			UInt64 cuckoo_hits;
			UInt64 *cuckoo_hits_per_level;
			SubsecondTime cuckoo_latency;
		} stats;
		void accessTable(IntPtr address);

	public:
		PageTableCuckoo(int core_id, String name, int page_sizes, int *page_size_list,
						int *page_table_sizes, double rehash_threshold, int scale, int ways);
		PTWResult initializeWalk(IntPtr address, bool count);
		AllocatedPage handlePageFault(IntPtr address, bool count, IntPtr ppn = -1);
		int hash(int key, int tableIndex) const;

		void rehash();
		double currentLoadFactor() const;
		void display() const;
		void deletePage(IntPtr address);
		void insertLargePage(IntPtr address, IntPtr ppn);
	};
}
