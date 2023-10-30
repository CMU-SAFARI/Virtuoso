
#pragma once
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "core.h"
#include "shmem_perf_model.h"
#include "memory_manager.h"
#include "mmu_base.h"

namespace ParametricDramDirectoryMSI
{
	SubsecondTime MemoryManagementUnitBase::accessCache(translationPacket packet)
	{
		if (host_mmu != NULL)
		{
			host_mmu->performAddressTranslation(packet.eip, packet.address, packet.instruction, packet.lock_signal, packet.modeled, packet.count);
		}
		SubsecondTime t_start = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
		IntPtr cache_address = ((IntPtr)(packet.address)) & (~((64 - 1)));
		CacheCntlr *l1d_cache = memory_manager->getCacheCntlrAt(core->getId(), MemComponent::component_t::L1_DCACHE);
		l1d_cache->processMemOpFromCore(
			packet.eip,
			packet.lock_signal,
			Core::mem_op_t::READ,
			cache_address, 0,
			NULL, 8,
			packet.modeled,
			packet.count, packet.type, SubsecondTime::Zero());

		SubsecondTime t_end = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

		shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, t_start);
		memory_manager->tagCachesBlockType(packet.address, packet.type);

		return t_end - t_start;
	}

}