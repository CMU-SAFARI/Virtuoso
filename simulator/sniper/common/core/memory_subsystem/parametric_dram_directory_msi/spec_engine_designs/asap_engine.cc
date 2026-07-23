#include "asap_engine.h"
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

//#define DEBUG_ASAP
 
namespace ParametricDramDirectoryMSI
{
    ASAP::ASAP(Core *core, MemoryManagerBase *_memory_manager, ShmemPerfModel *shmem_perf_model, String _name) : SpecEngineBase(core, _memory_manager, shmem_perf_model, _name), name(_name), memory_manager(_memory_manager)
    {

        // mmu_N.log is the log file for the MMU of core N
        log_file = std::ofstream();
        log_file_name = "asap.log." + std::to_string(core->getId());
        log_file_name = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + log_file_name;
        log_file.open(log_file_name.c_str());

#ifdef DEBUG_ASAP
        std::cout << "[ASAP] Initializing ASAP" << std::endl;
#endif
        registerStatsMetric("asap", core->getId(), "asap_hits", &stats.hits);
        registerStatsMetric("asap", core->getId(), "asap_invocations", &stats.invocations);

        stats.hits = 0;
        stats.invocations = 0;
    }

    void ASAP::invokeSpecEngine(IntPtr address, int count, Core::lock_signal_t lock, IntPtr eip, bool modeled, SubsecondTime invoke_start_time, IntPtr physical_address, bool page_table_speculation)
    {
        // Legacy overload without visited_pts — nothing to do
    }

    void ASAP::invokeSpecEngine(IntPtr address, int count, Core::lock_signal_t lock, IntPtr eip, bool modeled, SubsecondTime invoke_start_time, IntPtr physical_address, const accessedAddresses& visited_pts, bool page_table_speculation)
    {
        if (!page_table_speculation)
            return;

        stats.invocations++;

#ifdef DEBUG_ASAP
        log_file << std::endl;
        log_file << "[ASAP] Invoking ASAP with address " << address
                 << " visited_pts size=" << visited_pts.size() << std::endl;
#endif

        // Prefetch the last two levels of the page table walk (PD: depth 2, PT: depth 3)
        // using the real physical addresses from initializeWalk()
        for (const auto& pt_access : visited_pts)
        {
            if (pt_access.depth >= 2)
            {
                IntPtr cache_address = pt_access.physical_addr & ~((IntPtr)(64 - 1));
#ifdef DEBUG_ASAP
                log_file << "[ASAP] Prefetching depth=" << pt_access.depth
                         << " physical_addr=" << pt_access.physical_addr
                         << " cache_line=" << cache_address << std::endl;
#endif
                memory_manager->getCacheCntlrAt(core->getId(), MemComponent::component_t::L2_CACHE)
                    ->handleMMUPrefetch(eip, cache_address, invoke_start_time);
                stats.hits++;
            }
        }
    }
}