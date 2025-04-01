
#include "tlb_prefetcher_base.h"
#include "mmu_base.h"
#include "../memory_manager.h"

namespace ParametricDramDirectoryMSI
{
	query_entry TLBPrefetcherBase::PTWTransparent(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count, PageTable *pt)
	{
		// SubsecondTime total_walk_latency = SubsecondTime::Zero();
		// SubsecondTime total_fault_latency = SubsecondTime::Zero();

		// ParametricDramDirectoryMSI::MemoryManager *memory_manager = (ParametricDramDirectoryMSI::MemoryManager *)(Sim()->getCoreManager()->getCoreFromID(core->getId())->getMemoryManager());

		// ParametricDramDirectoryMSI::MemoryManagementUnitBase *mmu_base = (ParametricDramDirectoryMSI::MemoryManagementUnitBase *)(memory_manager->getMMU());

		// auto ptw_result = mmu_base->performPTW(address, modeled, count, false, eip, lock);
		// total_walk_latency = get<0>(ptw_result);

		// if (get<1>(ptw_result) > SubsecondTime::Zero())
		// {
		// 	total_fault_latency = get<1>(ptw_result);
		// }

		// IntPtr ppn_result = get<2>(ptw_result);
		// int page_size = get<3>(ptw_result);

		// if (ppn_result == 0)
		// { // Initialize walk failed
		// 	query_entry q;
		// 	q.ppn = 0;
		// 	return q;
		// }

		// query_entry q;
		// q.timestamp = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) + total_walk_latency + total_fault_latency;
		// q.address = address;
		// q.ppn = ppn_result;
		// q.page_size = page_size;
		// return q;
	}

}