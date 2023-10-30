
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
#include "rangelb.h"

namespace ParametricDramDirectoryMSI
{
	class TLBHierarchy;

	class RangeMMU : public MemoryManagementUnitBase
	{

	private:
		RLB *range_lb;

		struct VMA
		{
			IntPtr vbase;
			IntPtr vend;
			bool allocated;
			std::vector<Range> physical_ranges;
		};

		std::map<IntPtr, VMA> vmas;

	public:
		RangeMMU(Core *core, MemoryManager *memory_manager, ShmemPerfModel *shmem_perf_model, MemoryManagementUnitBase *_host_mmu);
		~RangeMMU();
		void instantiateRLB();
		void instantiatePageTable();
		void instantiateTLBSubsystem();
		void registerMMUStats();
		pair<SubsecondTime, IntPtr> performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count);
		SubsecondTime accessCache(translationPacket packet);
		void discoverVMAs();
	};
}