
#pragma once
#include "memory_manager.h"
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "core.h"
#include "shmem_perf_model.h"
#include "pagetable.h"
#include "tlb_subsystem.h"
#include "mmu_base.h"
#include "utopia_cache_template.h"

namespace ParametricDramDirectoryMSI
{
	class TLBHierarchy;

	class MemoryManagementUnitUtopia : public MemoryManagementUnitBase
	{

	private:
		MemoryManager *memory_manager;
		TLBHierarchy *tlb_subsystem;

		// Utopia-specific
		UtopiaCache *sf_cache;
		UtopiaCache *tar_cache;
		std::unordered_map<IntPtr, pair<int, int>> ptw_stats;	   // keep track of frequency/cost of PTWs
		std::unordered_map<IntPtr, SubsecondTime> migration_queue; // keep track of migrations

		struct
		{
			UInt64 page_faults;
			UInt64 page_table_walks;
			UInt64 tlb_hierarchy_misses;
			UInt64 num_translations;
			UInt64 flextorest_migrations;
			UInt64 requests_affected_by_migration;
			UInt64 data_in_restseg;
			UInt64 total_rsw_requests;

			SubsecondTime total_walk_latency;
			SubsecondTime total_translation_latency;
			SubsecondTime total_tlb_latency;
			SubsecondTime *tlb_latency_per_level;
			SubsecondTime total_rsw_latency;
			SubsecondTime migration_stall_cycles;

		} translation_stats;

	public:
		MemoryManagementUnitUtopia(Core *core, MemoryManager *memory_manager, ShmemPerfModel *shmem_perf_model, MemoryManagementUnitBase *_host_mmu);
		~MemoryManagementUnitUtopia();
		void instantiatePageTable();
		void instantiateTLBSubsystem();
		void instantiateRestSegWalker();
		void registerMMUStats();
		pair<SubsecondTime, IntPtr> performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count);
		std::tuple<bool, int, vector<pair<IntPtr, int>>> RestSegWalk(IntPtr address, bool count);
		void discoverVMAs();
	};
}