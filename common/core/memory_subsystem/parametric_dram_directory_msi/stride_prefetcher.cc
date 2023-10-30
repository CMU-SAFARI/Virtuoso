
#pragma once
#include "tlb_prefetcher_base.h"
#include "stride_prefetcher.h"
#include "cache_cntlr.h"
namespace ParametricDramDirectoryMSI
{
	StridePrefetcher::StridePrefetcher(Core *_core, MemoryManager *_memory_manager, ShmemPerfModel *_shmem_perf_model, int length) : TLBPrefetcherBase(_core, _memory_manager, _shmem_perf_model), core(_core),
																																	 memory_manager(_memory_manager),
																																	 shmem_perf_model(_shmem_perf_model), length(length)
	{
	}
	std::vector<query_entry> StridePrefetcher::performPrefetch(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count, PageTable *pt)
	{
		vector<query_entry> result;
		for (int i = -length; i <= length; i++)
		{
			if (i != 0)
			{
				result.push_back(PTWTransparent(address + i, eip, lock, modeled, count, pt));
			}
		}
		return result;
	}

}