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
			IntPtr tag;
			IntPtr ppn[8];
			bool valid[8];
			Entry *next_entry;
			IntPtr emulated_physical_address;
			int distance_from_root;
		};

		int extra_pages;

		std::vector<Entry *> page_tables;
		IntPtr *table_pa;
		int *m_page_table_sizes;

		struct Stats
		{
			UInt64 *page_table_walks;
			UInt64 *chained;
			UInt64 *num_accesses;
			UInt64 *table_matches;
			UInt64 first_hit;
		} stats;

		std::ofstream log_file;
		std::string log_file_name;

	public:
		PageTableHT(int core_id, String name, String type, int page_sizes, int *page_size_list, int *page_table_sizes, bool is_guest = false);
		~PageTableHT();

		UInt64 hashFunction(IntPtr address, int table_size);
		IntPtr getPhysicalSpace(int size);

		PTWResult initializeWalk(IntPtr address, bool count, bool is_prefetch = false, bool restart_walk = false);
		int updatePageTableFrames(IntPtr address, IntPtr core_id, IntPtr ppn, int page_size, std::vector<UInt64> frames);
		void deletePage(IntPtr address);

		void calculate_mean();
		void calculate_std();

		void printVectorStatistics();
		void printPageTable();
	};
}