
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
#include "metadata_table_base.h"

namespace ParametricDramDirectoryMSI
{
	class TLBHierarchy;

	class MemoryManagementUnit : public MemoryManagementUnitBase
	{

	private:
		MemoryManager *memory_manager;
		TLBHierarchy *tlb_subsystem;
		MetadataTableBase *metadata_table;

		struct
		{
			UInt64 page_faults;
			UInt64 page_table_walks;
			UInt64 num_translations;
			SubsecondTime total_walk_latency;
			SubsecondTime total_translation_latency;
			SubsecondTime total_tlb_latency;

			SubsecondTime *tlb_latency_per_level;

		} translation_stats;

	public:
		MemoryManagementUnit(Core *core, MemoryManager *memory_manager, ShmemPerfModel *shmem_perf_model, MemoryManagementUnitBase *_host_mmu);
		~MemoryManagementUnit();
		void instantiatePageTable();
		void instantiateMetadataTable();

		void instantiateTLBSubsystem();
		void registerMMUStats();
		pair<SubsecondTime, IntPtr> performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count);
		void discoverVMAs();
	};
}