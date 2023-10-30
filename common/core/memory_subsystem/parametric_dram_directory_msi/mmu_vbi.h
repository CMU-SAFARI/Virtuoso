
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

// namespace ParametricDramDirectoryMSI
// {
// 	class TLBHierarchy;

// 	class MemoryManagementUnitVBI : public MemoryManagementUnitBase
// 	{

// 	private:
// 		Core *core;
// 		MemoryManager *memory_manager;
// 		TLBHierarchy *tlb_subsystem;

// 		using AddressPair = std::pair<std::string, std::string>;
// 		std::map<int, AddressPair> vmas;

// 		ShmemPerfModel *shmem_perf_model;

// 		struct
// 		{
// 			UInt64 page_faults;
// 			UInt64 page_table_walks;
// 			UInt64 num_translations;
// 			SubsecondTime total_walk_latency;
// 			SubsecondTime total_translation_latency;
// 			SubsecondTime total_tlb_latency;

// 			SubsecondTime *tlb_latency_per_level;

// 			// UInt64 tlb_hit;
// 			// UInt64 utr_hit;
// 			// UInt64 tlb_hit_l1;
// 			// UInt64 tlb_hit_l2;
// 			// UInt64 tlb_and_utr_miss;

// 			// SubsecondTime tlb_hit_latency;
// 			// SubsecondTime tlb_hit_l1_latency;
// 			// SubsecondTime tlb_hit_l2_latency;

// 			// SubsecondTime utr_hit_latency;
// 			// SubsecondTime tlb_and_utr_miss_latency;
// 			// SubsecondTime total_latency;

// 			// UInt64 llc_miss_l1tlb_hit;
// 			// UInt64 llc_miss_l2tlb_hit;
// 			// UInt64 llc_miss_l2tlb_miss;

// 			// UInt64 llc_hit_l1tlb_hit;
// 			// UInt64 llc_hit_l2tlb_hit;
// 			// UInt64 llc_hit_l2tlb_miss;

// 			// SubsecondTime victima_latency;
// 			// SubsecondTime l1c_hit_tlb_latency;
// 			// SubsecondTime l2c_hit_tlb_latency;
// 			// SubsecondTime nucac_hit_tlb_latency;

// 			// UInt64 l1c_hit_tlb;
// 			// UInt64 l2c_hit_tlb;
// 			// UInt64 nucac_hit_tlb;

// 			// SubsecondTime ptw_contention;

// 		} translation_stats;

// 	public:
// 		MemoryManagementUnitVBI(Core *core, MemoryManager *memory_manager, ShmemPerfModel *shmem_perf_model, MemoryManagementUnitBase *_host_mmu);
// 		~MemoryManagementUnitVBI();
// 		void instantiatePageTable();
// 		void instantiateTLBSubsystem();
// 		void registerMMUStats();
// 		void performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count);
// 		void discoverVMAs();
// 	};
// }