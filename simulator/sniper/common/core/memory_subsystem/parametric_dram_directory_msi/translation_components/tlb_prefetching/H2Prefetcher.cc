
#include "tlb_prefetcher_base.h"
#include "H2Prefetcher.h"
#include "cache_cntlr.h"
#include "stats.h"
namespace ParametricDramDirectoryMSI
{
	H2Prefetcher::H2Prefetcher(Core *_core, MemoryManagerBase *_memory_manager, ShmemPerfModel *_shmem_perf_model, String name) : 
		TLBPrefetcherBase(_core, _memory_manager, _shmem_perf_model, name), core(_core),
			memory_manager(_memory_manager),
			shmem_perf_model(_shmem_perf_model)
	{
		A_address = 0;
		B_address = 0;
		C_address = 0;
		stats.predictions = 0;
		stats.walks_ok = 0;
		stats.zero_delta_predictions = 0;
		registerStatsMetric("tlb_h2", core->getId(), "predictions", &stats.predictions);
		registerStatsMetric("tlb_h2", core->getId(), "walks_ok", &stats.walks_ok);
		registerStatsMetric("tlb_h2", core->getId(), "zero_delta_predictions", &stats.zero_delta_predictions);
		std::cout << logPrefix() << "config: (none) -- walks C+(C-B) and C+(B-A) on every access" << std::endl;
	}
	std::vector<query_entry> H2Prefetcher::performPrefetch(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count, PageTable *pt, bool instruction, bool tlb_hit, bool pq_hit, int page_size)
	{
		vector<query_entry> result;
		A_address = B_address;
		B_address = C_address;
		C_address = address >> 12;
		IntPtr VPN = address >> 12; // We assume that the page size is 4KB

		// Warm once three VPNs have been seen (0 = unseen).
		if (A_address != 0 && B_address != 0 && C_address != 0)
		{
			const IntPtr deltas[2] = { C_address - B_address, B_address - A_address };
			for (IntPtr d : deltas)
			{
				stats.predictions++;
				if (d == 0)
					stats.zero_delta_predictions++;
				query_entry q = PTWTransparent((VPN + d) << 12, eip, lock, modeled, count, pt);
				if (q.ppn != 0)
					stats.walks_ok++;
				result.push_back(q);
			}
		}
		// std::cout << result.size() << std::endl;
		return result;
	}

}
