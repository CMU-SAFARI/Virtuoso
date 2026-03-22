
#include "radix_filter.h"
#include "simulator.h"
#include "config.hpp"
#include "core.h"
#include "pwc.h"
#include "pagetable.h"

using namespace std;


namespace ParametricDramDirectoryMSI
{
    RadixFilter::RadixFilter(String _name, Core* _core)
        : BaseFilter(_name, _core) 
    {

        max_pwc_level = Sim()->getCfg()->getInt("perf_model/"+_name+"/pwc/levels");
        m_pwc_enabled = Sim()->getCfg()->getBool("perf_model/"+_name+"/pwc/enabled");

        if (m_pwc_enabled)
        {

            std::cout << "[MMU] Page walk caches are enabled" << std::endl;

            UInt32 *entries = (UInt32 *)malloc(sizeof(UInt64) * max_pwc_level);
            UInt32 *associativities = (UInt32 *)malloc(sizeof(UInt64) * max_pwc_level);
            for (int i = 0; i < max_pwc_level; i++)
            {
                entries[i] = Sim()->getCfg()->getIntArray("perf_model/" + _name + "/pwc/entries", i);
                associativities[i] = Sim()->getCfg()->getIntArray("perf_model/" + _name + "/pwc/associativity", i);
            }

            ComponentLatency pwc_access_latency = ComponentLatency(_core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/" + _name + "/pwc/access_penalty"));
            ComponentLatency pwc_miss_latency = ComponentLatency(_core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/" + _name + "/pwc/miss_penalty"));
            pwc = new PWC("pwc", "perf_model/" + _name + "/pwc", _core->getId(), associativities, entries, max_pwc_level, pwc_access_latency, pwc_miss_latency, false);
        
        }

    }

    RadixFilter::~RadixFilter() {

    }


    PTWResult RadixFilter::filterPTWResult(IntPtr virtual_address, PTWResult ptw_result, PageTable *page_table, bool count) 
    {
        accessedAddresses ptw_accesses;

        if (m_pwc_enabled)
        {
            accessedAddresses original_ptw_accesses = ptw_result.accesses;
            // We need to filter based on the page walk caches
            for (UInt32 i = 0; i < ptw_result.accesses.size(); i++)
            {
                bool pwc_hit = false;

                    // We need to check if the entry is in the PWC
                    // If it is, we need to remove it from the PTW result
                    // If it is not, we need to add it to the PTW result
                    // Only check page walk caches if the level is not the first one

                    int level = original_ptw_accesses[i].depth;

                    IntPtr pwc_address = original_ptw_accesses[i].physical_addr;
    #ifdef DEBUG_MMU
                    log_file << "[RadixFilter] Checking PWC for address: " << pwc_address << " at level: " << level << std::endl;
    #endif
                    if (level < max_pwc_level){
                        pwc_hit = pwc->lookup(pwc_address, SubsecondTime::Zero(), true, level, count);
                    }
                    

    #ifdef DEBUG_MMU
                    log_file << "[RadixFilter] PWC HIT: " << pwc_hit << " level: " << level << std::endl;
    #endif
                    // If the entry is not in the cache, we need to access the memory

                    if (!pwc_hit)
                    {
                        // The entry is stored in: current_frame->emulated_ppn * 4096 which is the physical address of the frame
                        // The offset is the index of the entry in the frame
                        // The size of the entry is 8 bytes
                        // The physical address of the entry is: current_frame->emulated_ppn * 4096 + offset*8
                        ptw_accesses.push_back(ptw_result.accesses[i]);
                    }
            }
        }

        return PTWResult(ptw_result.page_size, ptw_accesses, ptw_result.ppn, ptw_result.pwc_latency, ptw_result.fault_happened);
    }

    bool RadixFilter::lookupPWC(IntPtr address, SubsecondTime now, int level, bool count)
    {
        if (!m_pwc_enabled || !pwc) {
            return false;  // No PWC available
        }
        
        // Check if level is within PWC range
        if (level < 0 || level >= max_pwc_level) {
            return false;  // Level not cached by PWC
        }
        
        // Perform PWC lookup - allocates on miss automatically
        return pwc->lookup(address, now, true /* allocate_on_miss */, level, count);
    }
}
