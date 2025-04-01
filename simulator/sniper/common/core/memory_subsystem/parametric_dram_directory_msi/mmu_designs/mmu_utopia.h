
#pragma once
#include "../memory_manager.h"
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "core.h"
#include "shmem_perf_model.h"
#include "pagetable.h"
#include "tlb_subsystem.h"
#include "mmu_base.h"
#include "pwc.h"    
#include "utopia_cache_template.h"
#include "ptmshrs.h"

namespace ParametricDramDirectoryMSI
{
	class TLBHierarchy;
	


	class MemoryManagementUnitUtopia : public MemoryManagementUnitBase
	{

	private:
		MemoryManager *memory_manager;
		TLBHierarchy *tlb_subsystem;
		MSHR *pt_walkers; 
        bool m_pwc_enabled;
        PWC* pwc;

		// Utopia-specific
		UtopiaCache *sf_cache;
		UtopiaCache *tar_cache;
		std::unordered_map<IntPtr, pair<int, int>> ptw_stats;	   // keep track of frequency/cost of PTWs
		std::unordered_map<IntPtr, SubsecondTime> migration_queue; // keep track of migrations

		int ptw_migration_threshold;
		int dram_accesses_migration_threshold;

		//For the log
		std::ofstream log_file;
		std::string log_file_name;

		struct
		{
			UInt64 num_translations;
			UInt64 page_faults;
			UInt64 page_table_walks;

			UInt64 flextorest_migrations;
			UInt64 requests_affected_by_migration;
			SubsecondTime migration_stall_cycles;


			UInt64 data_in_restseg;
			UInt64 data_in_flexseg;
			UInt64 data_in_tlb;


			UInt64 data_in_restseg_no_fault;
			UInt64 data_in_flexseg_no_fault;

			UInt64 total_rsw_memory_requests;

			SubsecondTime total_walk_latency_flexseg_no_fault;
			SubsecondTime total_rsw_latency_restseg_no_fault;

			SubsecondTime total_walk_latency_flexseg;
			SubsecondTime total_rsw_latency_restseg;
			
			SubsecondTime total_tlb_latency_on_tlb_hit;

			SubsecondTime total_walk_latency;
			SubsecondTime total_rsw_latency;
			SubsecondTime total_tlb_latency;

			SubsecondTime total_fault_latency;

			SubsecondTime total_translation_latency;

			SubsecondTime *tlb_latency_per_level;

		} translation_stats;

	public:
		MemoryManagementUnitUtopia(Core *core, MemoryManager *memory_manager, ShmemPerfModel *shmem_perf_model, String name, MemoryManagementUnitBase *nested_mmu);
		~MemoryManagementUnitUtopia();
		void instantiatePageTableWalker();
		void instantiateTLBSubsystem();
		void instantiateRestSegWalker();
		void registerMMUStats();
		IntPtr performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count);
		std::tuple<int, IntPtr, SubsecondTime> RestSegWalk(IntPtr address, bool instruction, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count);
        PTWResult filterPTWResult(PTWResult ptw_result, PageTable *page_table, bool count);

		void discoverVMAs();
	};
}