#pragma once // Ensure this header file is included only once in compilation

#include "mmu.h"
#include "mmu_base.h"
#include "mmu_pomtlb.h"
#include "mmu_range.h"
#include "mmu_utopia.h"
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
#include "bloom_filter.h"
#include "city.h"

namespace ParametricDramDirectoryMSI
{
    typedef struct SpotEntry_t
    {
        bool valid;
        UInt64 tag;
        int64_t offset;
        int saturation_counter;
        UInt64 LRU_counter;

        /* Equality operator */
        friend bool operator==(const SpotEntry_t& lhs, const SpotEntry_t& rhs) noexcept
        {
            return  lhs.valid             == rhs.valid &&
                    lhs.tag               == rhs.tag   &&
                    lhs.offset            == rhs.offset &&
                    lhs.saturation_counter== rhs.saturation_counter &&
                    lhs.LRU_counter       == rhs.LRU_counter;
        }
    } SpotEntry;

    using Ways                = std::vector<SpotEntry>;
    using SetAssociativeCache = std::vector<Ways>;

    extern SpotEntry EMPTY_SPOT_ENTRY;   // Declaration only

    /**
     * @class Spot
     * @brief A specialized speculative execution engine for handling speculative TLB lookups.
     */
    class Spot : public SpecEngineBase
    {
    protected:
        String name; ///< Name of the speculative engine instance.

        int number_of_entries_bits; // log2(number of entries)
        int number_of_entries;      // number of entries
        int number_of_ways;         // number of ways
        int number_of_bits; 
        int number_of_sets_bits; 
        int number_of_sets; 
        UInt64 global_LRU_counter = 0; // Global counter for LRU replacement policy; Incremented on every access.

        struct {
            UInt64 cache_accesses;
            UInt64 cache_hits;
            UInt64 cache_misses;
            UInt64 predictions_made;
            UInt64 prediction_not_made_saturation;
            UInt64 predictions_correct;
            UInt64 prediction_accuracy;
            UInt64 allocations;
            UInt64 no_allocation_confidence;
            UInt64 no_allocation_offset_mismatch;

            UInt64 evictions_zero_confidence;
            UInt64 allocations_bypassed_confident_set;
            UInt64 allocation_in_invalid_way;
            UInt64 refreshed_hit_entry;

        } stats; ///< Statistics for the speculative engine.

        SetAssociativeCache cache; // num_sets * num_ways 2D Matrix (entries)
        MemoryManagerBase *memory_manager; ///< Pointer to the memory manager instance.
        int predefined_allocation_threshold;

        SubsecondTime m_last_spec_completion;    ///< When the last speculative prefetch completed
        bool m_last_prediction_correct;           ///< Whether the last invocation's prediction was correct
        
        std::string log_file_name; ///< Name of the log file for output.
        std::ofstream log_file;    ///< Output stream for the log file.

    public:
        /**
         * @brief Constructor for Spot.
         *
         * Initializes a Spot instance for speculative TLB lookups.
         *
         * @param core Pointer to the core instance associated with this speculative TLB.
         * @param _memory_manager Pointer to the memory manager handling memory operations.
         * @param shmem_perf_model Pointer to the shared memory performance model.
         * @param _name A string representing the name of this SpecTLB instance.
         */
        Spot(Core *core, MemoryManagerBase *_memory_manager, ShmemPerfModel *shmem_perf_model, String _name);

        /**
         * @brief Invokes the speculative engine for handling memory access.
         *
         * @param address The memory address to be accessed.
         * @param count Number of accesses to be performed.
         * @param lock Lock signal type associated with the access.
         * @param eip Instruction pointer address.
         * @param modeled Boolean flag indicating whether the access is modeled.
         */
        void invokeSpecEngine(IntPtr address, int count, Core::lock_signal_t lock, IntPtr eip, bool modeled, SubsecondTime invoke_start_time, IntPtr physical_address, bool page_table_speculation = false);

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
        void allocateInSpecEngine(IntPtr address, IntPtr ppn, int count, Core::lock_signal_t lock, IntPtr eip, bool modeled) ;

        SubsecondTime getLastSpecCompletionTime() const override { return m_last_spec_completion; }
        bool wasLastPredictionCorrect() const override { return m_last_prediction_correct; }

    };
}