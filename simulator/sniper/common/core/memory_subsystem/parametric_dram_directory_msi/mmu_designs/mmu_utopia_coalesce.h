
#pragma once
// ============================================================================
// @kanellok: MMU Utopia Coalesce - MMU with Dual Radix Walk (2MB + 4KB)
// ============================================================================
//
// Extends MemoryManagementUnitUtopia with awareness of the coalesced 2MB
// radix tree in UtopiaHashCoalesce.  During a RestSeg walk the 2MB radix
// tree is checked FIRST (3 cache-line reads); on a miss the standard 4KB
// radix tree is walked (4 cache-line reads).
//
// The rest of the MMU logic (TLB hierarchy, UTLB, CATS speculation,
// FlexSeg PTW, migration) is inherited unchanged from the base class.
//
// ============================================================================

#include "mmu_utopia.h"

namespace ParametricDramDirectoryMSI
{
	class MemoryManagementUnitUtopiaCoalesce : public MemoryManagementUnitUtopia
	{
	private:
		// ====================================================================
		// Coalesced 2MB Radix Walk Stats (per-core)
		// ====================================================================
		struct CoalesceStats {
			UInt64 radix_2mb_lookups  = 0;   ///< 2MB radix tree lookups
			UInt64 radix_2mb_hits     = 0;   ///< Hits in 2MB radix tree (skip 4KB walk)
			UInt64 radix_2mb_misses   = 0;   ///< Misses in 2MB radix tree (fall through to 4KB)

			// 2MB radix walk memory hierarchy stats
			UInt64 radix_2mb_pwc_hits   = 0;   ///< 2MB radix level accesses that hit in PWC
			UInt64 radix_2mb_pwc_misses = 0;   ///< 2MB radix level accesses that missed in PWC

			UInt64 radix_2mb_internal_hit_l2   = 0;
			UInt64 radix_2mb_internal_hit_nuca = 0;
			UInt64 radix_2mb_internal_hit_dram = 0;
			UInt64 radix_2mb_internal_accesses = 0;

			UInt64 radix_2mb_leaf_hit_l2   = 0;
			UInt64 radix_2mb_leaf_hit_nuca = 0;
			UInt64 radix_2mb_leaf_hit_dram = 0;
			UInt64 radix_2mb_leaf_accesses = 0;

			SubsecondTime radix_2mb_latency_total = SubsecondTime::Zero();
		} coalesce_stats;

	public:
		MemoryManagementUnitUtopiaCoalesce(Core *core, MemoryManagerBase *memory_manager,
		                                    ShmemPerfModel *shmem_perf_model, String name,
		                                    MemoryManagementUnitBase *nested_mmu);
		~MemoryManagementUnitUtopiaCoalesce();

		// Override RestSegWalk to walk both 2MB and 4KB radix trees
		std::tuple<int, IntPtr, SubsecondTime> RestSegWalk(IntPtr address, bool instruction,
		                                                    IntPtr eip, Core::lock_signal_t lock,
		                                                    bool modeled, bool count) override;

	private:
		void registerCoalesceMMUStats();
	};
}
