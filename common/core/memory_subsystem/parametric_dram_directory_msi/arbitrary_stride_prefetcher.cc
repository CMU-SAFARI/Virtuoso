
#pragma once
#include "tlb_prefetcher_base.h"
#include "arbitrary_stride_prefetcher.h"
#include "cache_cntlr.h"
namespace ParametricDramDirectoryMSI
{
	ArbitraryStridePrefetcher::ArbitraryStridePrefetcher(Core *_core, MemoryManager *_memory_manager, ShmemPerfModel *_shmem_perf_model, int _table_size, int _prefetch_threshold, bool _extra_prefetch) : TLBPrefetcherBase(_core, _memory_manager, _shmem_perf_model), core(_core),
																																																		   memory_manager(_memory_manager),
																																																		   shmem_perf_model(_shmem_perf_model), prefetch_threshold(_prefetch_threshold), extra_prefetch(_extra_prefetch)
	{
		table_size = std::pow(2, _table_size);
		table = (entry_prefetcher *)malloc(table_size * sizeof(entry_prefetcher));
		for (int i = 0; i < table_size; i++)
		{
			table[i].PC = 0;
			table[i].vaddr = 0;
			table[i].stride = 0;
			table[i].saturation_counter = 0;
		}
	}
	std::vector<query_entry> ArbitraryStridePrefetcher::performPrefetch(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count, PageTable *pt)
	{
		vector<query_entry> result;
		int index = eip % table_size;
		if (table[index].PC == eip)
		{
			IntPtr new_stride = address - table[index].vaddr;
			if (table[index].stride == -1)
			{
				table[index].stride = new_stride;
				table[index].saturation_counter++;
			}
			else if (table[index].stride == new_stride)
			{
				table[index].saturation_counter++;
			}
			else
			{
				table[index].saturation_counter = 0;
			}
			if (table[index].saturation_counter > prefetch_threshold)
			{
				result.push_back(PTWTransparent(address + table[index].stride, eip, lock, modeled, count, pt));
				if (extra_prefetch)
				{
					result.push_back(PTWTransparent(address - table[index].vaddr, eip, lock, modeled, count, pt));
				}
			}
			table[index].vaddr = address;
		}
		else
		{
			table[index].PC = eip;
			table[index].vaddr = address;
			table[index].stride = -1;
			table[index].saturation_counter = 0;
		}
		// if (result.size() != 0)
		// 	std::cout << "Result size: " << result.size() << std::endl;
		return result;
	}

}