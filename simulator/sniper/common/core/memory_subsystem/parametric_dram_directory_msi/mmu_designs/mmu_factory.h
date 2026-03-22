#pragma once

#include "mmu.h"
#include "mmu_base.h"
#include "memory_manager_base.h"
#include "mmu_pomtlb.h"
#include "mmu_range.h"
#include "mmu_virt.h"
#include "mmu_dmt.h"
#include "mmu_hw_faults.h"
#include "mmu_spec.h"
#include "mmu_utopia.h"
#include "mmu_utopia_coalesce.h"
#include "config.hpp"


namespace ParametricDramDirectoryMSI
{
    class MMUFactory
    {
    public:
        static MemoryManagementUnitBase *createMemoryManagementUnit(String type, Core *core, MemoryManagerBase *memory_manager, ShmemPerfModel *shmem_perf_model, String name, MemoryManagementUnitBase *nested_mmu = nullptr)
        {

			if (type == "default") // Baseline MMU design
			{
				return new MemoryManagementUnit(core, memory_manager, shmem_perf_model, name, nested_mmu);
			}
			else if (type == "pomtlb") // Zu et al. ISCA 2017
			{
				return new MemoryManagementUnitPOMTLB(core, memory_manager, shmem_perf_model, name, nested_mmu);
			}
			else if (type == "range") // Karakostas et al. ISCA 2015
			{
				return new RangeMMU(core, memory_manager, shmem_perf_model, name, nested_mmu);
			}
			else if (type == "virt") // Baseline  MMU design that support virtualized execution
			{
				return new MemoryManagementUnitVirt(core, memory_manager, shmem_perf_model, name, nested_mmu);
			}
			else if (type == "dmt") // Direct Memory Translation MMU design ASPLOS 2024
			{	
				return new MemoryManagementUnitDMT(core, memory_manager, shmem_perf_model, name, nested_mmu);
			}
			else if (type == "hw_fault")	// Hardware Fault Handling MMU design (i.e., generic design that enables handling page faults in hardware)
			{
				return new MemoryManagementUnitHWFault(core, memory_manager, shmem_perf_model, name, nested_mmu);
			}
			else if (type == "spec")	// Speculative MMU design (Kanellopoulos et al. Under Submission)
			{
				return new MemoryManagementUnitSpec(core, memory_manager, shmem_perf_model, name, nested_mmu);
			}
			else if (type == "utopia")	// Utopia MMU design (Kanellopoulos et al. MICRO 2023)
			{
				return new MemoryManagementUnitUtopia(core, memory_manager, shmem_perf_model, name, nested_mmu);
			}
			else if (type == "utopia_coalesce")	// Utopia with coalesced 2MB radix walk
			{
				return new MemoryManagementUnitUtopiaCoalesce(core, memory_manager, shmem_perf_model, name, nested_mmu);
			}
			else
			{
				std::cerr << "Invalid MMU type: " << type << std::endl;
				abort();
			}
		}
	};
}