
#include "tlb_prefetcher_base.h"
#include "H2Prefetcher.h"
#include "cache_cntlr.h"
namespace ParametricDramDirectoryMSI
{
	H2Prefetcher::H2Prefetcher(Core *_core, MemoryManager *_memory_manager, ShmemPerfModel *_shmem_perf_model) : TLBPrefetcherBase(_core, _memory_manager, _shmem_perf_model), core(_core),
																												 memory_manager(_memory_manager),
																												 shmem_perf_model(_shmem_perf_model)
	{
		A_address = NULL;
		B_address = NULL;
		C_address = NULL;
	}
	std::vector<query_entry> H2Prefetcher::performPrefetch(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count, PageTable *pt)
	{
		vector<query_entry> result;
		A_address = B_address;
		B_address = C_address;
		C_address = address >> 12;
		IntPtr VPN = address >> 12; // We assume that the page size is 4KB

		if (A_address != NULL && B_address != NULL && C_address != NULL)
		{
			result.push_back(PTWTransparent((VPN + (C_address - B_address)) << 12, eip, lock, modeled, count, pt));
			result.push_back(PTWTransparent((VPN + (B_address - A_address)) << 12, eip, lock, modeled, count, pt));
		}
		// std::cout << result.size() << std::endl;
		return result;
	}

}