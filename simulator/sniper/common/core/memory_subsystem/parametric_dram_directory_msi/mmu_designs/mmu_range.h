
#pragma once
#include <vector>
#include <tuple>
#include <iostream>
#include <fstream>

#include "memory_manager.h"
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "core.h"
#include "shmem_perf_model.h"
#include "pagetable.h"
#include "tlb_subsystem.h"
#include "mmu_base.h"
#include "rangelb.h"
#include "ptmshrs.h"
#include "instruction.h"

using namespace std;

namespace ParametricDramDirectoryMSI
{
	class TLBHierarchy;

	class RangeMMU : public MemoryManagementUnitBase
	{

	private:
		RLB *range_lb;
        TLBHierarchy *tlb_subsystem;
		MSHR *pt_walkers; 
		PWC *pwc; // Only used for radix page tables
		bool m_pwc_enabled;

		//For the log
		std::ofstream log_file;
		std::string log_file_name;

		struct
		{
			UInt64 num_translations;

            
            UInt64 requests_resolved_by_rlb;
            SubsecondTime requests_resolved_by_rlb_latency;
            SubsecondTime total_range_walk_latency;


            UInt64 page_table_walks;
			SubsecondTime total_walk_latency;


            UInt64 page_faults;
			SubsecondTime total_fault_latency;
            
			SubsecondTime total_tlb_latency;
            SubsecondTime requests_resolved_by_tlb_latency;
			SubsecondTime *tlb_latency_per_level;

            SubsecondTime total_translation_latency;

		} translation_stats;



	public:
		RangeMMU(Core *core, MemoryManager *memory_manager, ShmemPerfModel *shmem_perf_model, String name, MemoryManagementUnitBase *nested_mmu);
		~RangeMMU();
		void instantiatePageTableWalker();
        void instantiateRangeTableWalker();
		void instantiateTLBSubsystem();
		void registerMMUStats();

		IntPtr performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count);
		void discoverVMAs();
		PTWResult filterPTWResult(PTWResult ptw_result, PageTable *page_table, bool count);
		std::tuple<SubsecondTime, IntPtr, int> performRangeWalk(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count);
		VMA findVMA(IntPtr address);
	};
} // namespace ParametricDramDirectoryMSI