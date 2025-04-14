#include "spectlb.h"
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
#include "reserve_thp.h"

//#define DEBUG_SPEC_TLB

namespace ParametricDramDirectoryMSI
{
    SpecTLB::SpecTLB(Core *core, MemoryManager *_memory_manager, ShmemPerfModel *shmem_perf_model, String _name) : SpecEngineBase(core, _memory_manager, shmem_perf_model, _name), memory_manager(_memory_manager), name(_name)
    {

        log_file_name = "spec_tlb.log." + std::to_string(core->getId());
        log_file_name = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + log_file_name;
        log_file.open(log_file_name.c_str());
        // Initialize spec_hit to false (though it's not used anywhere in the current code)
        spec_hit = false;

        // Define the config string used for TLB configuration
        String tlbconfigstring = "perf_model/spec_tlb";

        // Get the configuration values for TLB attributes from the simulator configuration
        String type = Sim()->getCfg()->getString("perf_model/spec_tlb/type");      // TLB type (e.g., L1, L2, etc.)
        int size = Sim()->getCfg()->getInt("perf_model/spec_tlb/size");            // Size of the TLB
        int assoc = Sim()->getCfg()->getInt("perf_model/spec_tlb/assoc");          // Associativity of the TLB
        int page_sizes = Sim()->getCfg()->getInt("perf_model/spec_tlb/page_size"); // Number of different page sizes

        // Dynamically allocate memory for page_size_list based on the number of page sizes
        int *page_size_list = (int *)malloc(sizeof(int) * (page_sizes));

        // The following line is commented out but might be used in future for allocate_on_miss configuration
        // bool allocate_on_miss = Sim()->getCfg()->getBool("perf_model/spec_tlb/allocate_on_miss");

        // Get the access latency for the TLB. If there's a core, use its DVFS domain, otherwise use the global DVFS domain
        ComponentLatency latency = ComponentLatency(
            core ? core->getDvfsDomain() : Sim()->getDvfsManager()->getGlobalDomain(DvfsManager::DvfsGlobalDomain::DOMAIN_GLOBAL_DEFAULT),
            Sim()->getCfg()->getInt("perf_model/spec_tlb/access_latency"));

        // Populate the page_size_list array with the page sizes from the configuration
        for (int i = 0; i < page_sizes; i++)
        {
            page_size_list[i] = Sim()->getCfg()->getIntArray("perf_model/spec_tlb/page_size_list", i); // Get each page size from the config
        }

        // Create a new TLB object using the retrieved configuration values
        spec_tlb = new TLB(
            "spec_tlb",               // Name of the TLB
            tlbconfigstring,          // Configuration string used to identify the TLB
            core ? core->getId() : 0, // Core ID (if a core exists)
            latency,                  // Latency of the TLB
            size,                     // Size of the TLB
            assoc,                    // Associativity of the TLB
            page_size_list,           // List of page sizes
            page_sizes,               // Number of page sizes
            type,                     // Type of the TLB
            false                     // Whether to allocate on miss (this option is currently unused)
        );
    }

    void SpecTLB::invokeSpecEngine(IntPtr address, int count, Core::lock_signal_t lock, IntPtr eip, bool modeled, SubsecondTime invoke_start_time, IntPtr physical_address, bool page_table_speculation)
    {
        if (page_table_speculation)
            return;
#ifdef DEBUG_SPEC_TLB
        log_file << std::endl;
        log_file << "[SpecTLB] Invoking Speculative TLB Engine for address: " << address << std::endl;
#endif
        // Initialize variables for speculative TLB access
        spec_hit = false;                         // Flag indicating whether the TLB access resulted in a hit
        spec_access_time = SubsecondTime::Zero(); // Initialize the access time to zero
        spec_tlb_block_info = NULL;               // Pointer to hold block info on TLB lookup, initially null

        // Speculative TLB access
        spec_access_time = invoke_start_time;
        // Get the current elapsed time for the user thread (for speculative access timing)

        // Perform a lookup in the speculative TLB to check if the address is already cached
        spec_tlb_block_info = spec_tlb->lookup(address, spec_access_time, count, lock, eip, modeled, count, NULL);

        // If the TLB lookup returns a valid block (i.e., hit), proceed with further processing
        if (spec_tlb_block_info != NULL) // If TLB hit is detected
        {
#ifdef DEBUG_SPEC_TLB
            log_file << "[SpecTLB] TLB Hit for address: " << address << std::endl;
            log_file << "[SpecTLB] TLB Block VPN: " << (address >> 21) << std::endl;
            log_file << "[SpecTLB] TLB Block PPN: " << spec_tlb_block_info->getPPN() << std::endl;
            log_file << "[SpecTLB] TLB Block Page Size: " << spec_tlb_block_info->getPageSize() << std::endl;
            log_file << "[SpecTLB] TLB Block Valid: " << spec_tlb_block_info->isValid() << std::endl;
#endif
            spec_hit = true; // Set the hit flag to true
        }
        else
        {
            spec_hit = false; // Set the hit flag to false
        }

        // If we had a TLB hit, prefetch data based on the TLB information
        if (spec_hit) // If TLB hit was successful
        {
            int base_page_size = 1 << 12;  // Base page size is 4KB
            int large_page_size = 1 << 21; // Large page size is 2MB

            IntPtr address_to_prefetch = (spec_tlb_block_info->getPPN() * base_page_size + address % large_page_size) & (~((64 - 1)));
            // Perform prefetch for L2 cache based on the TLB block's physical page number (PPN)
#ifdef DEBUG_SPEC_TLB
            log_file << "[SpecTLB] Final Address to Prefetch: " << address_to_prefetch << std::endl;
#endif
            memory_manager->getCacheCntlrAt(core->getId(), MemComponent::component_t::L2_CACHE)->doPrefetch(eip,                                         // The instruction pointer (address) from which to prefetch
                                                                                                            address_to_prefetch,                         // Calculated address for prefetch
                                                                                                            spec_access_time + spec_tlb->getLatency(),   // Time of the access for prefetching
                                                                                                            CacheBlockInfo::block_type_t::NON_PAGE_TABLE // Type of block (indicating it's not a page table)
            );
        }
    }
    void SpecTLB::allocateInSpecEngine(IntPtr address, IntPtr ppn, int count, Core::lock_signal_t lock, IntPtr eip, bool modeled)
    {
        // If there was no hit in the speculative TLB (i.e., if spec_hit is false)
#ifdef DEBUG_SPEC_TLB
        log_file << "[SpecTLB] Allocating in Speculative TLB for address: " << address << std::endl;
#endif
        if (!spec_hit)
        {

#ifdef DEBUG_SPEC_TLB
            log_file << "[SpecTLB] TLB Miss & SpecTLB Miss for address: " << address << std::endl;
#endif
            ReservationTHPAllocator *res_allocator = (ReservationTHPAllocator *)(Sim()->getMimicOS()->getMemoryAllocator());

            IntPtr is_reserved = res_allocator->isLargePageReserved(address);


#ifdef DEBUG_SPEC_TLB
            log_file << "[SpecTLB] Is Large Page Reserved: " << is_reserved << std::endl;
#endif
            // Allocate a new entry in the speculative TLB since there was a miss
            // Call the 'allocate' method of the spec_tlb object to handle allocation
            if (is_reserved != static_cast<IntPtr>(-1))
            {

                spec_tlb->allocate(
                    address,          // The address for which we need to allocate a new TLB entry
                    spec_access_time, // The time at which this access happens (used for updating the access time)
                    count,            // The count or the access frequency; might be used for tracking or updating access stats
                    lock,             // The lock status; could indicate if the TLB access should be locked or not
                    21,               // Page size (hardcoded to 21, which is the maximum page size)
                    is_reserved              // The physical page number (PPN) associated with the address, used for allocation in the TLB
                );
            }
        }
    }
}