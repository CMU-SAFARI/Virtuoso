
#pragma once
#include "subsecond_time.h"
#include "fixed_types.h"
#include "city.h"
#include <stdint.h>
#include <vector>

namespace ParametricDramDirectoryMSI
{

	class PageTableHDC : public PageTable
	{

	private:
		struct Entry
		{
			//@kanellok: Switch it to bitset<> to save memory
			bool valid[8];
			IntPtr tag;
			IntPtr ppn[8];
			int distance_from_root;
		};
		std::vector<Entry *> page_tables;

		int *m_page_table_sizes;
		IntPtr *emulated_table_address;

		struct Stats
		{
			UInt64 *page_table_walks;
			UInt64 *num_accesses;
			UInt64 *collisions;
			UInt64 page_faults;
			std::vector<double> *std;
			std::vector<double> *mean;
		} stats;

		std::vector<std::ofstream> std_file;
		std::vector<std::ofstream> mean_file;

		std::ofstream log_file; // Log file for the page table
		std::string log_file_name;

	public:
		UInt64 hashFunction(IntPtr address, int table_size);
		PageTableHDC(int core_id, String name, String type, int page_sizes, int *page_size_list, int *page_table_sizes, bool is_guest = false);
		~PageTableHDC();

		PTWResult initializeWalk(IntPtr address, bool count, bool is_prefetch = false, bool restart_walk = false);
		int updatePageTableFrames(IntPtr address, IntPtr core_id, IntPtr ppn, int page_size, std::vector<UInt64> frames);
		void deletePage(IntPtr address);

		void printPageTable();
		void calculate_mean();
		void calculate_std();
		void printVectorStatistics();
	};
}
