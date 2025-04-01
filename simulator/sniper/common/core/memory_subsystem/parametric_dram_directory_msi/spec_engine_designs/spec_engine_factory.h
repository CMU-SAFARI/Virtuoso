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

namespace ParametricDramDirectoryMSI
{
    /**
     * @class SpecEngineFactory
     * @brief Factory class for creating different types of Speculative Execution Engines.
     */
    class SpecEngineFactory
    {
    public:
        /**
         * @brief Creates a Speculative Execution Engine (SpecEngineBase) based on the specified type.
         *
         * This function dynamically allocates and returns an instance of the appropriate
         * SpecEngineBase subclass based on the given type. If the type is invalid, the program aborts.
         *
         * @param type The type of Speculative Engine to create (e.g., "specTLB").
         * @param core Pointer to the Core instance that this speculative engine will be associated with.
         * @param memory_manager Pointer to the MemoryManager instance handling memory operations.
         * @param shmem_perf_model Pointer to the shared memory performance model.
         * @param name A string representing the name of the SpecEngine instance.
         *
         * @return Pointer to a dynamically allocated SpecEngineBase instance.
         */
        static SpecEngineBase *createSpecEngineBase(
            String type,
            Core *core,
            MemoryManager *memory_manager,
            ShmemPerfModel *shmem_perf_model,
            String name)
        {
            // Check Speculative Engine type and instantiate the appropriate class
            if (type == "specTLB") // Baseline speculative TLB design
            {
                return new SpecTLB(core, memory_manager, shmem_perf_model, name);
            }
            // Placeholder for additional speculative engine types
            // else if (type == "Revelator")
            // {
            //     return new SpecEngineRevelator(memory_manager, name);
            // }
            else
            {
                // Handle invalid SpecEngine type error
                std::cerr << "Invalid SpecEngine type: " << type << std::endl;
                abort(); // Terminate execution due to invalid input
            }
        }
    };
}
