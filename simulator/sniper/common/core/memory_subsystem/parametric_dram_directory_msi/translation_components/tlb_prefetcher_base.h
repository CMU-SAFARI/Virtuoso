
#pragma once
#include "subsecond_time.h"
#include "fixed_types.h"
#include "core.h"
#include "../memory_manager.h"
#include "shmem_perf_model.h"
#include "pagetable.h"
#include "cache_block_info.h"
#include "trans_defs.h"

namespace ParametricDramDirectoryMSI
{


	class TLBPrefetcherBase
	{

	protected:
	public:
		Core *core;
		ParametricDramDirectoryMSI::MemoryManager *memory_manager;

		ShmemPerfModel *shmem_perf_model;
		TLBPrefetcherBase(Core *_core, MemoryManager *_memory_manager, ShmemPerfModel *_shmem_perf_model) : core(_core),
																											memory_manager(_memory_manager),
																											shmem_perf_model(_shmem_perf_model)
		{
		}
		virtual query_entry PTWTransparent(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count, PageTable *pt);
		virtual std::vector<query_entry> performPrefetch(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count, PageTable *pt) = 0;
	};
}