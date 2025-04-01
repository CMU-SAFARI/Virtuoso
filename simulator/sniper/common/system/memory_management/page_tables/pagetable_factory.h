#pragma once
#include "pagetable.h"
#include "pagetable_cuckoo.h"
#include "pagetable_radix.h"
#include "pagetable_hdc.h"
#include "pagetable_ht.h"
#include "config.hpp"
#include "mimicos.h"

namespace ParametricDramDirectoryMSI
{
	class PageTableFactory
	{
	public:
		static PageTable *createCuckooPageTable(int core_id, String name, String type, int page_sizes, int *page_size_list, int *page_table_sizes, double rehash_threshold, float scale, int ways, bool is_guest)
		{
			return new PageTableCuckoo(core_id, name, type, page_sizes, page_size_list, page_table_sizes, rehash_threshold, scale, ways, is_guest);
		}

		static PageTable *createRadixPageTable(int app_id, String name, String type, int page_sizes, int *page_size_list, int levels, int frame_size, bool is_guest)
		{
			std::cout << "[Radix Page Table] Creating 4-level Radix table with name: " << name << "for app id: " << app_id << std::endl;
			std::cout << "[Radix Page Table] Page sizes: " << page_sizes << std::endl;
			for (int i = 0; i < page_sizes; i++)
			{
				std::cout << "[Page Table] Page size: " << page_size_list[i] << std::endl;
			}
			std::cout << "[Radix Page Table] Levels: " << levels << std::endl;
			std::cout << "[Radix Page Table] Frame size: " << frame_size << " entries" << std::endl;

			return new PageTableRadix(app_id, name, type, page_sizes, page_size_list, levels, frame_size, is_guest);
		}

		static PageTable *createHDCPageTable(int core_id, String name, String type, int page_sizes, int *page_size_list, int *page_table_size_list, bool is_guest)
		{
			std::cout << "[HDC Page Table] Creating Hash Dont Cache table  [SIGMETRICS 2016] with name: " << name << std::endl;
			std::cout << "[HDC Page Table] Page sizes: " << page_sizes << std::endl;
			for (int i = 0; i < page_sizes; i++)
			{
				std::cout << "[HDC Page Table] Page size: " << page_size_list[i] << std::endl;
			}
			std::cout << "[HDC Page Table] Page table sizes: " << std::endl;
			for (int i = 0; i < page_sizes; i++)
			{
				std::cout << "[HDC Page Table] Page table size: " << page_table_size_list[i] << std::endl;
			}

			return new PageTableHDC(core_id, name, type, page_sizes, page_size_list, page_table_size_list, is_guest);
		}

		static PageTable *createHTPageTable(int core_id, String name, String type, int page_sizes, int *page_size_list, int *page_table_size_list, bool is_guest)
		{
			return new PageTableHT(core_id, name, type, page_sizes, page_size_list, page_table_size_list, is_guest);
		}

		static PageTable *createPageTable(String type, String name, UInt64 app_id, bool is_guest = false)
		{

			if (type == "elastic_cuckoo")
			{
				int page_sizes = Sim()->getCfg()->getInt("perf_model/" + name + "/page_sizes");

				double rehash_threshold = (double)Sim()->getCfg()->getFloat("perf_model/" + name + "/rehash_threshold");

				int ways = Sim()->getCfg()->getInt("perf_model/" + name + "/ways");

				float scale = Sim()->getCfg()->getFloat("perf_model/" + name + "/scale");

				int *page_size_list = new int[page_sizes];

				for (int i = 0; i < page_sizes; i++)
				{
					page_size_list[i] = Sim()->getCfg()->getIntArray("perf_model/" + name + "/page_size_list", i);
				}

				int *page_table_size_list = new int[page_sizes];

				for (int i = 0; i < page_sizes; i++)
				{
					page_table_size_list[i] = Sim()->getCfg()->getIntArray("perf_model/" + name + "/page_table_size_list", i);
				}
				return createCuckooPageTable(app_id, name, type, page_sizes, page_size_list, page_table_size_list, rehash_threshold, scale, ways, is_guest);
			}

			if (type == "radix")
			{
				String mimicos_name;
				if (is_guest == true)
					mimicos_name = Sim()->getMimicOS_VM()->getName();
				else
					mimicos_name = Sim()->getMimicOS()->getName();

				Core* core = Sim()->getCoreManager()->getCoreFromID(app_id);

				int page_sizes = Sim()->getCfg()->getInt("perf_model/" + mimicos_name + "/" + name + "/page_sizes");
				int *page_size_list = new int[page_sizes];

				for (int i = 0; i < page_sizes; i++)
				{
					page_size_list[i] = Sim()->getCfg()->getIntArray("perf_model/" + mimicos_name + "/" + name + "/page_size_list", i);
				}

				int levels = Sim()->getCfg()->getInt("perf_model/" + mimicos_name + "/" + name + "/levels");
				int frame_size = Sim()->getCfg()->getInt("perf_model/" + mimicos_name + "/" + name + "/frame_size");

				return createRadixPageTable(core->getId(), name, type, page_sizes, page_size_list, levels, frame_size, is_guest);
			}
			else if (type == "hash_dont_cache")
			{

				int page_sizes = Sim()->getCfg()->getInt("perf_model/" + name + "/page_sizes");
				int *page_size_list = new int[page_sizes];

				for (int i = 0; i < page_sizes; i++)
				{
					page_size_list[i] = Sim()->getCfg()->getIntArray("perf_model/" + name + "/page_size_list", i);
				}

				int *page_table_size_list = new int[page_sizes];

				for (int i = 0; i < page_sizes; i++)
				{
					page_table_size_list[i] = Sim()->getCfg()->getIntArray("perf_model/" + name + "/page_table_size_list", i);
				}

				return createHDCPageTable(app_id, name, type, page_sizes, page_size_list, page_table_size_list, is_guest);
			}
			else if (type == "hash_table")
			{

				int page_sizes = Sim()->getCfg()->getInt("perf_model/" + name + "/page_sizes");
				int *page_size_list = new int[page_sizes];

				for (int i = 0; i < page_sizes; i++)
				{
					page_size_list[i] = Sim()->getCfg()->getIntArray("perf_model/" + name + "/page_size_list", i);
				}

				int *page_table_size_list = new int[page_sizes];

				for (int i = 0; i < page_sizes; i++)
				{
					page_table_size_list[i] = Sim()->getCfg()->getIntArray("perf_model/" + name + "/page_table_size_list", i);
				}

				return createHTPageTable(app_id, name, type, page_sizes, page_size_list, page_table_size_list, is_guest);
			}
			else
				assert(0 && "Invalid page table name");
		}
	};
}