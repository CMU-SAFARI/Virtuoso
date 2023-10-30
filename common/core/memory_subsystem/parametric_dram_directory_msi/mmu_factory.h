#pragma once
#include "mmu.h"
#include "mmu_base.h"
#include "mmu_midgard.h"
#include "mmu_pomtlb.h"
#include "mmu_range.h"
#include "mmu_vbi.h"
#include "mmu_utopia.h"
#include "config.hpp"

namespace ParametricDramDirectoryMSI
{
	class MMUFactory
	{
	public:
		static MemoryManagementUnitBase *createMemoryManagementUnit(String type, Core *core, MemoryManager *memory_manager, ShmemPerfModel *shmem_perf_model, MemoryManagementUnitBase *host_mmu)
		{
			if (type == "midgard") // Gupta et al. "" ISCA
			{
				return new MemoryManagementUnitMidgard(core, memory_manager, shmem_perf_model, host_mmu);
			}
			else if (type == "default") // Baseline MMU design
			{
				return new MemoryManagementUnit(core, memory_manager, shmem_perf_model, host_mmu);
			}
			else if (type == "range") // Karakostas et al. "" ISCA 2015
			{
				return new RangeMMU(core, memory_manager, shmem_perf_model, host_mmu);
			}
			else if (type == "vbi") // Hajinazar et al. "" ISCA 2020
			{
				return new MemoryManagementUnitMidgard(core, memory_manager, shmem_perf_model, host_mmu);
			}

			else if (type == "pomtlb") // Zu et al. "" ISCA 2017
			{
				return new MemoryManagementUnitPOMTLB(core, memory_manager, shmem_perf_model, host_mmu);
			}
			else if (type == "utopia")
			{
				return new MemoryManagementUnitUtopia(core, memory_manager, shmem_perf_model, host_mmu);
			}
		}
	};
}