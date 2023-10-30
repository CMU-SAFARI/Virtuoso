#pragma once
#include "pagetable.h"
#include "pagetable_cuckoo.h"
#include "pagetable_radix.h"
#include "pagetable_hdc.h"
#include "pagetable_ht.h"
#include "config.hpp"

namespace ParametricDramDirectoryMSI
{
	class PageTableFactory
	{
	public:
		static PageTable *createCuckooPageTable(int core_id, String name, int page_sizes, int *page_size_list, int *page_table_sizes, double rehash_threshold, int scale, int ways)
		{
			return new PageTableCuckoo(core_id, name, page_sizes, page_size_list, page_table_sizes, rehash_threshold, scale, ways);
		}

		static PageTable *createRadixPageTable(int core_id, String name, int page_sizes, int *page_size_list, int levels, int frame_size, PWC *pwc)
		{
			std::cout << "Creating radix page table" << std::endl;
			return new PageTableRadix(core_id, name, page_sizes, page_size_list, levels, frame_size, pwc);
		}

		static PageTable *createHDCPageTable(int core_id, String name, int page_sizes, int *page_size_list, int *page_table_size_list)
		{
			return new PageTableHDC(core_id, name, page_sizes, page_size_list, page_table_size_list);
		}

		static PageTable *createHTPageTable(int core_id, String name, int page_sizes, int *page_size_list, int *page_table_size_list)
		{
			return new PageTableHT(core_id, name, page_sizes, page_size_list, page_table_size_list);
		}

		static PageTable *createPageTable(String type, String name, Core *core)
		{

			if (type == "elastic_cuckoo")
			{
				std::cout << "Creating elastic cuckoo page table" << std::endl;
				int page_sizes = Sim()->getCfg()->getInt("perf_model/" + name + "/page_sizes");
				double rehash_threshold = (double)Sim()->getCfg()->getFloat("perf_model/" + name + "/rehash_threshold");

				int ways = Sim()->getCfg()->getInt("perf_model/" + name + "/ways");

				int scale = Sim()->getCfg()->getInt("perf_model/" + name + "/scale");

				std::cout << "Page sizes: " << page_sizes << std::endl;
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
				return createCuckooPageTable(core->getId(), name, page_sizes, page_size_list, page_table_size_list, rehash_threshold, scale, ways);
			}

			else if (type == "radix")
			{

				std::cout << "Creating Radix table with name: " << name << std::endl;

				int page_sizes = Sim()->getCfg()->getInt("perf_model/" + name + "/page_sizes");
				std::cout << "Page sizes: " << page_sizes << std::endl;
				int *page_size_list = new int[page_sizes];

				for (int i = 0; i < page_sizes; i++)
				{
					page_size_list[i] = Sim()->getCfg()->getIntArray("perf_model/" + name + "/page_size_list", i);
					std::cout << "Page size: " << page_size_list[i] << std::endl;
				}

				int levels = Sim()->getCfg()->getInt("perf_model/" + name + "/levels");
				int frame_size = Sim()->getCfg()->getInt("perf_model/" + name + "/frame_size");

				std::cout << "Levels: " << levels << std::endl;
				std::cout << "Frame size: " << frame_size << std::endl;
				bool m_pwc_enabled = Sim()->getCfg()->getBool("perf_model/" + name + "/pwc/enabled");
				PWC *pwc = NULL;
				if (m_pwc_enabled)
				{
					UInt32 *entries = (UInt32 *)malloc(sizeof(UInt64) * (levels - 1));
					UInt32 *associativities = (UInt32 *)malloc(sizeof(UInt64) * (levels - 1));
					for (int i = 0; i < levels - 1; i++)
					{
						entries[i] = Sim()->getCfg()->getIntArray("perf_model/" + name + "/pwc/entries", i);
						associativities[i] = Sim()->getCfg()->getIntArray("perf_model/" + name + "/pwc/associativity", i);
					}
					ComponentLatency pwc_access_latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/" + name + "/pwc/access_penalty"));
					ComponentLatency pwc_miss_latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/" + name + "/pwc/miss_penalty"));

					pwc = new PWC("pwc", "perf_model/" + name + "/pwc", core->getId(), associativities, entries, levels - 1, pwc_access_latency, pwc_miss_latency, false);
				}
				return createRadixPageTable(core->getId(), name, page_sizes, page_size_list, levels, frame_size, pwc);
			}
			else if (type == "hash_dont_cache")
			{
				std::cout << "Creating Hash Dont Cache table  [SIGMETRICS 2016] with name" << name << std::endl;

				int page_sizes = Sim()->getCfg()->getInt("perf_model/" + name + "/page_sizes");
				std::cout << "Page sizes: " << page_sizes << std::endl;
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

				return createHDCPageTable(core->getId(), name, page_sizes, page_size_list, page_table_size_list);
			}
			else if (type == "hash_table")
			{
				std::cout << "Creating hash table with name" << name << std::endl;

				int page_sizes = Sim()->getCfg()->getInt("perf_model/" + name + "/page_sizes");
				std::cout << "Page sizes: " << page_sizes << std::endl;
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

				return createHTPageTable(core->getId(), name, page_sizes, page_size_list, page_table_size_list);
			}
			else
				assert(0 && "Invalid page table name");
		}
	};
}