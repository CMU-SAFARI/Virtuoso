#include "oracle_spec.h"
#include "simulator.h"
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "core.h"
#include "shmem_perf_model.h"
#include "pagetable.h"
#include "tlb_subsystem.h"
#include "cache_block_info.h"
#include "stats.h"
#include "mimicos.h"
#include "dvfs_manager.h"
#include "mimicos.h"

// #define DEBUG_SPEC_TLB

namespace ParametricDramDirectoryMSI
{
    OracleSpec::OracleSpec(Core *core, MemoryManagerBase *_memory_manager, ShmemPerfModel *shmem_perf_model, String _name) : SpecEngineBase(core, _memory_manager, shmem_perf_model, _name), name(_name), memory_manager(_memory_manager)
    {
#ifdef DEBUG_ORACLE_SPEC
        std::cout << "[ORACLE SPEC] Initializing Oracle Spec Engine" << std::endl;
#endif
    }

    void OracleSpec::invokeSpecEngine(IntPtr address, int count, Core::lock_signal_t lock, IntPtr eip, bool modeled, SubsecondTime invoke_start_time, IntPtr physical_address, bool page_table_speculation)
    {
#ifdef DEBUG_ORACLE_SPEC
        std::cout << "[ORACLE SPEC] Invoking Oracle Spec with address " << address << std::endl;
#endif
        if(!page_table_speculation){
            IntPtr cache_address = ((IntPtr)(physical_address)) & (~((64 - 1)));
            memory_manager->getCacheCntlrAt(core->getId(), MemComponent::component_t::L2_CACHE)->handleMMUPrefetch(eip, cache_address, invoke_start_time);
        }

        
        
        
    }
}