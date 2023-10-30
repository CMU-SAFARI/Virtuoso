
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

namespace ParametricDramDirectoryMSI
{
	class TLBHierarchy;

	class MemoryManagementUnitPOMTLB : public MemoryManagementUnitBase
	{

	private:
		MemoryManager *memory_manager;
		TLBHierarchy *tlb_subsystem;
		TLB *m_pom_tlb;
		char *software_tlb; // pom-related parameter
		UInt32 m_size;
		UInt32 m_associativity;
		int page_sizes;
		int *page_size_list;
		struct
		{
			UInt64 page_faults;
			UInt64 page_table_walks;
			UInt64 num_translations;
			SubsecondTime total_walk_latency;
			SubsecondTime total_translation_latency;
			SubsecondTime total_tlb_latency;
			SubsecondTime software_tlb_latency;
			SubsecondTime *tlb_latency_per_level;

		} translation_stats;

	public:
		MemoryManagementUnitPOMTLB(Core *core, MemoryManager *memory_manager, ShmemPerfModel *shmem_perf_model, MemoryManagementUnitBase *_host_mmu);
		~MemoryManagementUnitPOMTLB();
		void instantiatePageTable();
		void instantiateTLBSubsystem();
		void registerMMUStats();
		pair<SubsecondTime, IntPtr> performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count);
		void discoverVMAs();
	};
}