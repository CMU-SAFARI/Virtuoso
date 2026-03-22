
#include "tlb_prefetcher_base.h"
#include "mmu_base.h"
#include "../memory_manager.h"
#include <tuple>


namespace ParametricDramDirectoryMSI
{
	query_entry TLBPrefetcherBase::PTWTransparent(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count, PageTable *pt)
	{
		query_entry q{};

		if (pt == NULL)
		{
			q.ppn = 0;
			return q;
		}

		// Preserve the caller's notion of time so that prefetching does not
		// advance the main thread.
		const SubsecondTime start_time = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

#ifdef DEBUG_TLB_PREFETCHER
		log_file << "[" << m_name << "] Performing PTW-transparent prefetch for address: " << address << " at time: " << start_time.getNS() << " ns" << std::endl;
#endif

		auto *mmu_base = static_cast<ParametricDramDirectoryMSI::MemoryManagementUnitBase *>(memory_manager->getMMU());
		auto ptw_result = mmu_base->performPTW(address, modeled, count, /*is_prefetch*/ true, eip, lock, pt, /*restart_walk*/ true);

#ifdef DEBUG_TLB_PREFETCHER
		log_file << "[" << m_name << "] PTW result: "
				 << " latency: " << ptw_result.latency.getNS() << " ns"
				 << " page_fault: " << ptw_result.page_fault
				 << " ppn: " << ptw_result.ppn
				 << " page_size: " << ptw_result.page_size
				 << " at time: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD).getNS() << " ns" << std::endl;
#endif

		const SubsecondTime walk_latency = ptw_result.latency;
		const IntPtr ppn_result = ptw_result.ppn;
		const int page_size = ptw_result.page_size;
		const uint64_t payload_bits = ptw_result.payload_bits;

		// Restore the caller time after modeling the prefetch walk
		shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, start_time);

		q.timestamp = start_time + walk_latency;
		q.address = address;
		q.ppn = ppn_result;
		q.page_size = page_size;
		q.payload_bits = payload_bits;
		return q;
	}

}
