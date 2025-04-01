#pragma once // Ensures the header file is included only once in a compilation unit.

// Include necessary header files for memory management and hardware simulation.
#include "../memory_manager.h"
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "core.h"
#include "shmem_perf_model.h"
#include "pagetable.h"
#include "tlb_subsystem.h"
#include "mmu_base.h"
#include "mmu.h"
#include "spec_engine_base.h"

// Define a namespace for the ParametricDramDirectoryMSI memory model.
namespace ParametricDramDirectoryMSI
{
    class TLBHierarchy; // Forward declaration of TLBHierarchy class.

    // MemoryManagementUnitSpec: Specialized Memory Management Unit (MMU) class
    class MemoryManagementUnitSpec : public MemoryManagementUnitBase
    {
    private:
        SpecEngineBase *spec_engine;       // Pointer to a speculative execution engine.
        MemoryManager *memory_manager;     // Pointer to the memory manager.
        TLBHierarchy *tlb_subsystem;       // Pointer to the hierarchical TLB subsystem.
        TLB *spec_tlb;                     // Pointer to a specialized TLB for speculative execution.
        MSHR *pt_walkers;                  // Miss Status Holding Register (MSHR) for tracking page table walks.
        PWC *pwc;                          // Page Walk Cache (PWC), used for radix page tables only.
        bool m_pwc_enabled;                // Boolean flag indicating if PWC is enabled.

        // Log file handling
        std::ofstream log_file;    // Output file stream for logging.
        std::string log_file_name; // Name of the log file.

        // Structure to store memory translation statistics.
        struct
        {
            UInt64 num_translations;                 // Total number of address translations performed.
            UInt64 page_faults;                      // Number of page faults encountered.
            UInt64 page_table_walks;                 // Number of page table walks performed.
            SubsecondTime total_walk_latency;        // Total latency incurred in page table walks.
            SubsecondTime total_translation_latency; // Total time spent in address translations.
            SubsecondTime total_spec_latency;        // Total latency caused to generate speculative translations.
            SubsecondTime total_tlb_latency;         // Total time spent in TLB lookups.
            SubsecondTime total_fault_latency;       // Total latency incurred due to page faults.
            SubsecondTime walker_is_active;          // Tracks active time for page table walker.
            SubsecondTime *tlb_latency_per_level;    // Array storing TLB lookup latency per level.
            UInt64 *tlb_hit_page_sizes;              // Array storing the size of pages hit in the TLB.
        } translation_stats;

    public:
        // Constructor: Initializes the MMU with required parameters.
        MemoryManagementUnitSpec(Core *core, MemoryManager *memory_manager, ShmemPerfModel *shmem_perf_model, String name, MemoryManagementUnitBase *nested_mmu);

        // Destructor: Cleans up resources used by the MMU.
        ~MemoryManagementUnitSpec();

        // Instantiate a page table walker for handling page faults.
        void instantiatePageTableWalker();

        // Instantiate a metadata table for managing memory metadata.
        void instantiateMetadataTable();

        // Instantiate the TLB subsystem to handle address translations.
        void instantiateTLBSubsystem();

        // Register MMU-related performance statistics.
        void registerMMUStats();

        // Discover Virtual Memory Areas (VMAs) for address translation.
        void discoverVMAs();

        // Filters the result of a page table walk based on given parameters.
        PTWResult filterPTWResult(PTWResult ptw_result, PageTable *page_table, bool count);

        // Performs address translation for a given instruction or data address.
        IntPtr performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count);

        // Returns a pointer to the page table managed by this MMU.
        PageTable *getPageTable();
    };
}
