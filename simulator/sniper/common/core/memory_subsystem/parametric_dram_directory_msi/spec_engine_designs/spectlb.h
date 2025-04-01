#pragma once // Ensure this header file is included only once in compilation

#include "mmu.h"
#include "mmu_base.h"
#include "mmu_pomtlb.h"
#include "mmu_range.h"
#include "mmu_utopia.h"
#include "nested_mmu.h"
#include "config.hpp"
#include "mmu_spec.h"
#include "spec_engine_base.h"
#include "spectlb.h"
#include "tlb.h"
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "core.h"
#include "shmem_perf_model.h"
#include "pagetable.h"
#include "cache_block_info.h"
#include "stats.h"

namespace ParametricDramDirectoryMSI
{
    /**
     * @class SpecTLB
     * @brief A specialized speculative execution engine for handling speculative TLB lookups.
     */
    class SpecTLB : public SpecEngineBase
    {
    protected:
        SubsecondTime spec_access_time;      // time the access starts
        bool spec_hit;                       ///< Flag to indicate if the speculative access was a hit.
        MemoryManager *memory_manager;       ///< Pointer to the memory manager instance.
        String name;                         ///< Name of the speculative engine instance.
        TLB *spec_tlb;                       ///< Pointer to the speculative TLB instance.
        CacheBlockInfo *spec_tlb_block_info; ///< Pointer to the cache block info retrieved from the TLB lookup.
        std::ofstream log_file;              ///< Log file for the speculative TLB.
        std::string log_file_name;           ///< Name of the log file for the speculative TLB.
        
    public:
        /**
         * @brief Constructor for SpecTLB.
         *
         * Initializes a SpecTLB instance for speculative TLB lookups.
         *
         * @param core Pointer to the core instance associated with this speculative TLB.
         * @param _memory_manager Pointer to the memory manager handling memory operations.
         * @param shmem_perf_model Pointer to the shared memory performance model.
         * @param _name A string representing the name of this SpecTLB instance.
         */
        SpecTLB(Core *core, MemoryManager *_memory_manager, ShmemPerfModel *shmem_perf_model, String _name);

        /**
         * @brief Invokes the speculative engine for handling memory access.
         *
         * @param address The memory address to be accessed.
         * @param count Number of accesses to be performed.
         * @param lock Lock signal type associated with the access.
         * @param eip Instruction pointer address.
         * @param modeled Boolean flag indicating whether the access is modeled.
         */
        void invokeSpecEngine(IntPtr address, int count, Core::lock_signal_t lock, IntPtr eip, bool modeled);

        /**
         * @brief Allocates an entry in the speculative TLB.
         *
         * @param address The virtual memory address.
         * @param ppn The physical page number associated with the virtual address.
         * @param count Number of accesses to be performed.
         * @param lock Lock signal type associated with the allocation.
         * @param eip Instruction pointer address.
         * @param modeled Boolean flag indicating whether the allocation is modeled.
         */
        void allocateInSpecEngine(IntPtr address, IntPtr ppn, int count, Core::lock_signal_t lock, IntPtr eip, bool modeled);
    };
}
