
// #pragma once
// #include "memory_manager.h"
// #include "cache_cntlr.h"
// #include "subsecond_time.h"
// #include "fixed_types.h"
// #include "core.h"
// #include "shmem_perf_model.h"
// #include "pagetable.h"
// #include "tlb_subsystem.h"
// #include "mmu_base.h"
// #include "rangetable.h"
// #include "rangelb.h"
// #include "tlb.h"

// namespace ParametricDramDirectoryMSI
// {
// 	class TLBHierarchy;

// 	class MemoryManagementUnitMidgard : public MemoryManagementUnitBase
// 	{

// 	private:
// 		Core *core;
// 		MemoryManager *memory_manager;
// 		TLBHierarchy *tlb_subsystem;
// 		TLB *frontend_l1vlb;
//         RLB *frontend_l2vlb;

// 		ShmemPerfModel *shmem_perf_model;


// 		struct
// 		{
// 			UInt64 page_faults;
// 			UInt64 page_table_walks;
// 			UInt64 num_frontend_translations;
// 			UInt64 num_frontend_memory_accesses;
// 			UInt64 num_backend_translations;
// 			SubsecondTime total_walk_latency;
// 			SubsecondTime total_translation_latency;
// 			SubsecondTime total_tlb_latency;
// 			SubsecondTime *tlb_latency_per_level;
// 			SubsecondTime frontend_latency;
// 			SubsecondTime backend_latency;
// 			SubsecondTime total_fault_latency;

// 		} translation_stats;

// 	public:
// 		MemoryManagementUnitMidgard(Core *core, MemoryManager *memory_manager, ShmemPerfModel *shmem_perf_model, String name, MemoryManagementUnitBase *nested_mmu);
// 		~MemoryManagementUnitMidgard();
// 		void instantiateRLB();
// 		void instantiatePageTable();
// 		void instantiateRangeTable();
// 		void instantiateTLBSubsystem();
// 		void instantiateFrontendL1VLB();
// 		void registerMMUStats();
// 		pair<SubsecondTime, IntPtr> performAddressTranslationFrontend(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count);
// 		pair<SubsecondTime, IntPtr> performAddressTranslationBackend(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count);
// 		pair<SubsecondTime, IntPtr> performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count){ return std::make_pair(SubsecondTime::Zero(), IntPtr(0)); };
// 		void discoverVMAs();
// 	};
// }