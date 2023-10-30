#pragma once
#include "subsecond_time.h"
#include "fixed_types.h"
#include "city.h"
#include <stdint.h>
#include <vector>

namespace ParametricDramDirectoryMSI
{

	class PageTableHT : public PageTable
	{

	private:
		struct Entry
		{
			bool empty;
			IntPtr vpn;
			IntPtr ppn;
			Entry *next_entry;
		};

		std::vector<Entry *> page_tables;

		int *m_page_table_sizes;

		struct Stats
		{
			UInt64 *page_table_walks;
			UInt64 *chain_per_entry;
			UInt64 *num_accesses;
			UInt64 *table_matches;
			SubsecondTime *latency;
			SubsecondTime *total_latency;
			UInt64 page_faults;
		} stats;

	public:
		int hashFunction(IntPtr address, int table_size);
		PageTableHT(int core_id, String name, int page_sizes, int *page_size_list, int *page_table_sizes);
		PTWResult initializeWalk(IntPtr address, bool count);
		AllocatedPage handlePageFault(IntPtr address, bool count, IntPtr ppn = -1);
		void printPageTable();
		void deletePage(IntPtr address);
		void insertLargePage(IntPtr address, IntPtr ppn);
	};
}