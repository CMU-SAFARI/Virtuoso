
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

	class ArbitraryStridePrefetcher : public TLBPrefetcherBase
	{

	protected:
		int table_size;
		int prefetch_threshold;
		bool extra_prefetch;
		int lookahead; 
		int degree; 

		typedef struct entry_prefetcher_t
		{
			IntPtr PC;
			IntPtr vaddr;
			IntPtr stride;
			uint saturation_counter;

		} entry_prefetcher;

		struct {
			UInt64 successful_prefetches;
			UInt64 prefetch_attempts;
			UInt64 failed_prefetches;
			UInt64 late_prefetches;
			UInt64 early_prefetches;
		} stats;

	public:
		Core *core;
		MemoryManager *memory_manager;
		entry_prefetcher *table;
		ShmemPerfModel *shmem_perf_model;

		ArbitraryStridePrefetcher(Core *_core, MemoryManager *_memory_manager, ShmemPerfModel *_shmem_perf_model, int table_size, int prefetch_threshold, bool extra_prefetch, int lookahead, int degree);
		std::vector<query_entry> performPrefetch(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count, PageTable *pt);
	};
}