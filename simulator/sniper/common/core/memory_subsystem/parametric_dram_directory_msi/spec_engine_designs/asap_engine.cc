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
#ifdef DEBUG_ASAP
        log_file << std::endl;
        log_file << "[ASAP] Invoking ASAP with address " << address << std::endl;
#endif
        /*
        ASAP prefetches the 3rd and the 4th level of the Page Table leveraging page table contiguity. 
        */
        if(page_table_speculation){
            stats.invocations++;

            //Calculate the cache address of the page table entry in the 3rd level


                IntPtr current_frame_id = 0;
                IntPtr offset = 0;
                int base = 0;
                int old_base = 0;
                int levels = 4;
                int level = 4; // We have 4 levels of page tables
                for (int i= 0; i < 4; i++)
                {
#ifdef DEBUG_ASAP

                    log_file << "[ASAP] Level: " << level << " for address: " << address << std::endl;
                    log_file << "[ASAP] We need to shift the address by: " << (48 - 9 * (levels - level)) << " bits" << std::endl;
#endif
                    offset = (address >> (48 - 9 * (levels - level))) & 0x1FF;

#ifdef DEBUG_ASAP
                    log_file << "[ASAP] Before mask:" << (address >> (48 - 9 * (levels - level))) << " After mask: " << offset << std::endl;
#endif

                    base += (i>=1) ? pow(512, i-1) : 0; // We start from the base of the                                                                                                                                       
                    current_frame_id = (i>=1) ? ((base-1)+(current_frame_id-old_base)*512 +offset) : 0; // We start from the base of the first level
#ifdef DEBUG_ASAP
                    log_file << "[ASAP] Offset from previous level: " << offset << std::endl;
                    log_file << "[ASAP] Base: " << base << " for level: " << level << std::endl;
                    log_file << "[ASAP] Frame_id: " << current_frame_id << " for level: " << level << std::endl;
#endif



                    if (i > 1){
                        IntPtr page_table_address = (current_frame_id << 12) + ((address >> (48 - 9 * (levels - level+1))) & 0x1FF)*8;
                       
                        if (i==3 || i==2){

                            if (physical_address == page_table_address)
                            {
                                stats.hits++;
                            }
                            else
                            {
#ifdef DEBUG_ASAP
                                log_file << "[ASAP] Prefetching page table address: " << page_table_address << " but physical address is: " << physical_address << std::endl;
#endif
                            }
                        }

                        IntPtr cache_address = page_table_address & (~((64 - 1)));
#ifdef DEBUG_ASAP
                        log_file << "[ASAP] Page Table Address: " << page_table_address << " Cache Address: " << cache_address << std::endl;
#endif
                        memory_manager->getCacheCntlrAt(core->getId(), MemComponent::component_t::L2_CACHE)->handleMMUPrefetch(eip, cache_address, invoke_start_time);
                    }

                old_base = base;
                level--;
            }
        }
    }
}