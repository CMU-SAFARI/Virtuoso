
#pragma once
#include "tlb_prefetcher_base.h"
#include "cache_cntlr.h"
namespace ParametricDramDirectoryMSI
{
	query_entry TLBPrefetcherBase::PTWTransparent(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count, PageTable *pt)
	{
		SubsecondTime total_walk_latency = SubsecondTime::Zero();
		PTWResult page_table_walk_result = pt->initializeWalk(address, count);
		IntPtr ppn_result = get<2>(page_table_walk_result);
		accessedAddresses accesses = get<1>(page_table_walk_result);
		int page_size = get<0>(page_table_walk_result);

		SubsecondTime latency = SubsecondTime::Zero();

		int levels = 0;
		int tables = 0;

		for (int i = 0; i < accesses.size(); i++)
		{
			int level = get<1>(accesses[i]);
			int table = get<0>(accesses[i]);
			if (level > levels)
				levels = level;
			if (table > tables)
				tables = table;
		}

		SubsecondTime latency_per_table_per_level[tables + 1][levels + 1] = {SubsecondTime::Zero()};

		bool is_page_fault = true;
		int correct_table = -1;

		for (int i = 0; i < accesses.size(); i++)
		{

			//----------------------------------------------------------------
			SubsecondTime t_start = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
			IntPtr cache_address = (address) & (~((64 - 1)));
			CacheCntlr *l1d_cache = memory_manager->getCacheCntlrAt(core->getId(), MemComponent::component_t::L1_DCACHE);
			l1d_cache->processMemOpFromCore(
				eip,
				lock,
				Core::mem_op_t::READ,
				cache_address, 0,
				NULL, 8,
				modeled,
				false, CacheBlockInfo::block_type_t::PAGE_TABLE, SubsecondTime::Zero());

			SubsecondTime t_end = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, t_start);
			// memory_manager->tagCachesBlockType(address, CacheBlockInfo::block_type_t::PAGE_TABLE);
			//----------------------------------------------------------------
			latency = t_end - t_start;

			if (latency_per_table_per_level[get<0>(accesses[i])][get<1>(accesses[i])] < latency)
				latency_per_table_per_level[get<0>(accesses[i])][get<1>(accesses[i])] = latency;

			if (get<3>(accesses[i]) == true)
			{
				is_page_fault = false;
				correct_table = get<0>(accesses[i]);
				break;
			}
		}

		SubsecondTime table_latency = SubsecondTime::Zero();

		if (!is_page_fault)
		{

			for (int j = 0; j < levels + 1; j++)
			{
				total_walk_latency += latency_per_table_per_level[correct_table][j];
			}
		}
		else
		{
			table_latency = SubsecondTime::Zero();
			for (int i = 0; i < tables + 1; i++)
			{
				for (int j = 0; j < levels + 1; j++)
				{
					table_latency += latency_per_table_per_level[i][j];
				}
			}
			total_walk_latency = table_latency;
		}
		query_entry q;
		q.timestamp = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) + total_walk_latency;
		q.address = address;
		q.ppn = ppn_result;
		q.page_size = page_size;
		return q;
	}

}