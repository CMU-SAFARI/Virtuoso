
#pragma once
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "core.h"
#include "shmem_perf_model.h"
#include "pagetable.h"
#include "tlb_subsystem.h"
#include "cache_block_info.h"
#include "tlb_prefetcher_base.h"

namespace ParametricDramDirectoryMSI
{

	class StridePrefetcher : public TLBPrefetcherBase
	{

	protected:
		int length;

	public:
		Core *core;
		MemoryManager *memory_manager;

		struct
		{
			UInt64 prefetch_attempts;
			UInt64 successful_prefetches;
			UInt64 failed_prefetches;
		} stats;

		ShmemPerfModel *shmem_perf_model;
		StridePrefetcher(Core *_core, MemoryManager *_memory_manager, ShmemPerfModel *_shmem_perf_model, int length);
		std::vector<query_entry> performPrefetch(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count, PageTable *pt);
	};
}