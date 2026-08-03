
#pragma once
#include "subsecond_time.h"
#include "fixed_types.h"
#include "core.h"
#include "../memory_manager.h"
#include "shmem_perf_model.h"
#include "pagetable.h"
#include "cache_block_info.h"
#include "trans_defs.h"
#include <vector>

namespace ParametricDramDirectoryMSI
{

	class TLB;  // Forward declaration for TLB residency check

	class TLBPrefetcherBase
	{

	protected:
		std::vector<TLB*> m_tlb_hierarchy;  // TLB pointers for residency check
	public:
		Core *core;
		MemoryManagerBase *memory_manager;
		ShmemPerfModel *shmem_perf_model;
		String m_name;

		string log_file_name;
		std::ofstream log_file;

		TLBPrefetcherBase(Core *_core, MemoryManagerBase *_memory_manager, ShmemPerfModel *_shmem_perf_model, String name) : core(_core),
																											memory_manager(_memory_manager),
																											shmem_perf_model(_shmem_perf_model),
																											m_name(name)
		{
			log_file = std::ofstream();
			log_file_name = "tlb_prefetcher_core_" + std::to_string(core->getId()) + "_" + m_name.c_str() + ".log";
			log_file_name = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + log_file_name;
			log_file.open(log_file_name.c_str());
		}
		virtual ~TLBPrefetcherBase() {}

		void setTLBHierarchy(const std::vector<TLB*>& tlbs) { m_tlb_hierarchy = tlbs; }

		// Called when a TLB evicts a victim during allocate().
		// Override in prefetchers that need victim info (e.g., recency stack).
		virtual void notifyVictim(IntPtr /*victim_address*/, int /*page_size*/, IntPtr /*ppn*/) {}

		// Called when a prefetch-queue entry is materialized into the TLB
		// (i.e., the prefetched translation has been installed).
		// Override in prefetchers that track in-flight prefetches.
		virtual void notifyInstall(IntPtr /*address*/, int /*page_size*/) {}

		virtual query_entry PTWTransparent(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count, PageTable *pt);
		virtual std::vector<query_entry> performPrefetch(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count, PageTable *pt, bool instruction = false, bool tlb_hit = false, bool pq_hit = false, int page_size = 12) = 0;

		// Called by the MMU AFTER the demand page-table walk completes, so any
		// PTE cacheline a prefetcher updated this access is now resident.
		// Prefetchers that model in-PTE payload writes (TRAIL) override this to
		// mark the updated PTE line(s) dirty -> realistic writeback traffic.
		virtual void flushPendingPayloadWritebacks(PageTable * /*pt*/) {}
	};
}
