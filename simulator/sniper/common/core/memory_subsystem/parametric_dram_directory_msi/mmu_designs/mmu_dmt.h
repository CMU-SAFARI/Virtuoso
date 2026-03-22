

#pragma once
#include "../memory_manager.h"
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "core.h"
#include "shmem_perf_model.h"
#include "pagetable.h"
#include "tlb_subsystem.h"
#include "mmu.h"
#include "metadata_table_base.h"
#include "ptmshrs.h"
#include "base_filter.h"
#include "sim_log.h"
#include "debug_config.h"

namespace ParametricDramDirectoryMSI
{
	
	class TLBHierarchy;

	class MemoryManagementUnitDMT : public MemoryManagementUnitBase
	{

	private:
		MemoryManagerBase *memory_manager;
		TLBHierarchy *tlb_subsystem;
		MetadataTableBase *metadata_table;
		MSHR *pt_walkers; 
		BaseFilter* ptw_filter;


		//For the log
		SimLog *mmu_dmt_log;

		struct
		{
			UInt64 num_translations;
			UInt64 page_faults;
			UInt64 page_table_walks;
			SubsecondTime total_walk_latency;
			SubsecondTime total_translation_latency;
			SubsecondTime total_tlb_latency;
			SubsecondTime total_fault_latency;
			SubsecondTime walker_is_active;
			SubsecondTime *tlb_latency_per_level;
			UInt64 *tlb_hit_page_sizes; 

		} translation_stats;
		
	public:
		MemoryManagementUnitDMT(Core *core, MemoryManagerBase *memory_manager, ShmemPerfModel *shmem_perf_model, String name, MemoryManagementUnitBase *nested_mmu);
		~MemoryManagementUnitDMT();
		void instantiatePageTableWalker();
		void instantiateMetadataTable();
		void instantiateTLBSubsystem();
		void registerMMUStats();
		void discoverVMAs();

		PTWResult filterPTWResult(IntPtr address, PTWResult ptw_result, PageTable *page_table, bool count);
		IntPtr performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count);
		PageTable* getPageTable();

	};
}