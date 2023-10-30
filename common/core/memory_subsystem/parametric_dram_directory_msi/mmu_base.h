
#pragma once
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "core.h"
#include "shmem_perf_model.h"
#include "pagetable.h"
#include "tlb_subsystem.h"
#include "cache_block_info.h"

namespace ParametricDramDirectoryMSI
{
	class TLBHierarchy;

	class MemoryManagementUnitBase
	{

	protected:
		Core *core;
		MemoryManager *memory_manager;
		TLBHierarchy *tlb_subsystem;
		PageTable *page_table;

		ShmemPerfModel *shmem_perf_model;
		MemoryManagementUnitBase *host_mmu;

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

		struct translationPacket
		{
			IntPtr address;
			IntPtr eip;
			bool instruction;
			Core::lock_signal_t lock_signal;
			CacheBlockInfo::block_type_t type;
			bool modeled;
			bool count;
			translationPacket(IntPtr _address, IntPtr _eip, bool _instruction, Core::lock_signal_t _lock_signal, bool _modeled, bool _count, CacheBlockInfo::block_type_t _type) : address(_address),
																																												   eip(_eip),
																																												   instruction(_instruction),
																																												   lock_signal(_lock_signal),
																																												   modeled(_modeled),
																																												   count(_count),
																																												   type(_type)
			{
			}
			translationPacket()
			{
			}
		};

	public:
		MemoryManagementUnitBase(Core *_core, MemoryManager *_memory_manager, ShmemPerfModel *_shmem_perf_model, MemoryManagementUnitBase *_host_mmu) : core(_core),
																																						memory_manager(_memory_manager),
																																						shmem_perf_model(_shmem_perf_model),
																																						host_mmu(_host_mmu)
		{
		}
		void instantiatePageTable();
		void instantiateTLBSubsystem();
		virtual void registerMMUStats() = 0;
		virtual pair<SubsecondTime, IntPtr> performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count) = 0;
		virtual pair<SubsecondTime, IntPtr> performAddressTranslationFrontend(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count){};
		virtual pair<SubsecondTime, IntPtr> performAddressTranslationBackend(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count){};
		virtual SubsecondTime accessCache(translationPacket packet);
		PageTable *getPageTable() { return page_table; };

		virtual void discoverVMAs() = 0;
	};
}