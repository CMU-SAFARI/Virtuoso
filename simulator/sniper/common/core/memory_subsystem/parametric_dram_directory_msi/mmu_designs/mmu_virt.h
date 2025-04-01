
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

namespace ParametricDramDirectoryMSI
{
	class MemoryManagementUnitVirt : public MemoryManagementUnitBase
	{

	private:
		MemoryManager *memory_manager;
		MemoryManagementUnitBase *host_mmu;
		TLBHierarchy *tlb_subsystem;
		MSHR *pt_walkers; 

		//For the log
		std::ofstream log_file;
		std::string log_file_name;

		struct
		{
			UInt64 num_translations;
			UInt64 num_page_table_walks;
			SubsecondTime total_translation_latency;
			SubsecondTime total_walk_latency;
			SubsecondTime total_tlb_latency;
			SubsecondTime *tlb_latency_per_level;

		} translation_stats;
		
	public:
		MemoryManagementUnitVirt(Core *core, MemoryManager *memory_manager, ShmemPerfModel *shmem_perf_model, String name, MemoryManagementUnitBase *nested_mmu);
		~MemoryManagementUnitVirt();
		void instantiatePageTableWalker();
		void instantiateTLBSubsystem();
		void registerMMUStats();
		void discoverVMAs();
		PTWResult filterPTWResult(PTWResult ptw_result, PageTable *page_table, bool count);

		SubsecondTime accessCache(translationPacket packet, SubsecondTime t_start = SubsecondTime::Zero(),bool is_prefetch = false) override;

		IntPtr performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count);
		PageTable* getPageTable();
	
	};
}