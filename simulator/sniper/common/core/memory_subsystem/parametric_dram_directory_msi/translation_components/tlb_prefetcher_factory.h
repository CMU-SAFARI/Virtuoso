#pragma once

#include "tlb_prefetcher_base.h"
#include "stride_prefetcher.h"
#include "H2Prefetcher.h"
#include "arbitrary_stride_prefetcher.h"

namespace ParametricDramDirectoryMSI
{
	class TLBprefetcherFactory
	{
	public:
		static TLBPrefetcherBase *createASPPrefetcher(String name, String pq_index, Core *core, MemoryManager *memory_manager, ShmemPerfModel *shmem_perf_model)
		{
			int table_size = Sim()->getCfg()->getInt("perf_model/tlb_prefetch/pq" + pq_index + "/asp_prefetcher/table_size");
			int prefetch_threshold = Sim()->getCfg()->getInt("perf_model/tlb_prefetch/pq" + pq_index + "/asp_prefetcher/prefetch_threshold");
			bool extra_prefetch = Sim()->getCfg()->getBool("perf_model/tlb_prefetch/pq" + pq_index + "/asp_prefetcher/extra_prefetch");
			int lookahead = Sim()->getCfg()->getInt("perf_model/tlb_prefetch/pq" + pq_index + "/asp_prefetcher/lookahead");
			int degree = Sim()->getCfg()->getInt("perf_model/tlb_prefetch/pq" + pq_index + "/asp_prefetcher/degree");
			return new ArbitraryStridePrefetcher(core, memory_manager, shmem_perf_model, table_size, prefetch_threshold, extra_prefetch, lookahead, degree);
		}
		static TLBPrefetcherBase *createTLBPrefetcherStride(String name, String pq_index, Core *core, MemoryManager *memory_manager, ShmemPerfModel *shmem_perf_model)
		{
			int length = Sim()->getCfg()->getInt("perf_model/tlb_prefetch/pq" + pq_index + "/stride_prefetcher/length");
			return new StridePrefetcher(core, memory_manager, shmem_perf_model, length);
		}
		static TLBPrefetcherBase *createTLBPrefetcherH2(String name, String pq_index, Core *core, MemoryManager *memory_manager, ShmemPerfModel *shmem_perf_model)
		{
			return new H2Prefetcher(core, memory_manager, shmem_perf_model);
		}
		static TLBPrefetcherBase *createTLBPrefetcher(String name, String pq_index, Core *core, MemoryManager *memory_manager, ShmemPerfModel *shmem_perf_model)
		{
			if (name == "stride")
			{
				return createTLBPrefetcherStride(name, pq_index, core, memory_manager, shmem_perf_model);
			}
			else if (name == "h2")
			{
				return createTLBPrefetcherH2(name, pq_index, core, memory_manager, shmem_perf_model);
			}
			else if (name == "asp")
			{

				return createASPPrefetcher(name, pq_index, core, memory_manager, shmem_perf_model);
			}
			else
			{
				std::cout << "[CONFIG ERROR] No such TLB prefetcher: " << name << std::endl;
			}
		}
	};
}