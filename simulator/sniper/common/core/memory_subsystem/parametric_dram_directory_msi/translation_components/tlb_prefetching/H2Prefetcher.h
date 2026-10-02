
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

	/**
	 * @brief H2: two-delta history TLB prefetcher (no PC, no confidence).
	 *
	 * Keeps the last three VPNs seen at the PQ level, A (oldest), B, C (the
	 * current one), and on every access walks
	 *     C + (C - B)   -- repeat the latest delta
	 *     C + (B - A)   -- repeat the delta before it
	 * Both walks are issued unconditionally once three VPNs have been seen.
	 * A zero delta (the same page twice in a row) makes that candidate the
	 * accessed page itself; these are counted in zero_delta_predictions.
	 * VPN 0 doubles as "not yet seen".  Assumes 4KB pages.
	 */
	class H2Prefetcher : public TLBPrefetcherBase
	{

	protected:
		int length;

	public:
		Core *core;
		MemoryManagerBase *memory_manager;

		ShmemPerfModel *shmem_perf_model;

		IntPtr A_address;   // VPN two accesses ago
		IntPtr B_address;   // VPN one access ago
		IntPtr C_address;   // current VPN

		struct
		{
			UInt64 predictions;             // candidates generated (2 per access once warm)
			UInt64 walks_ok;                // candidates whose walk resolved a translation
			UInt64 zero_delta_predictions;  // candidates equal to the accessed page
		} stats;

		H2Prefetcher(Core *_core, MemoryManagerBase *_memory_manager, ShmemPerfModel *_shmem_perf_model, String name);
		std::vector<query_entry> performPrefetch(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count, PageTable *pt, bool instruction = false, bool tlb_hit = false, bool pq_hit = false, int page_size = 12) override;

	protected:
		void appendSummary(std::ostream &os) const override
		{
			os << " predictions=" << stats.predictions
			   << " walks_ok=" << stats.walks_ok
			   << " zero_delta=" << stats.zero_delta_predictions;
		}
	};
}