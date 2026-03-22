#ifndef EXCEPTION_HANDLER_H
#define EXCEPTION_HANDLER_H

#include "log.h"
#include "lock.h"
#include <cstdint>
#include <iostream>
#include "mimicos.h"
#include "memory_management/physical_memory_allocators/physical_memory_allocator.h"

#define FOUR_KIB 4096UL
#define BASE_PAGE_SHIFT 12UL

class ExceptionHandlerBase
{
public:
    // Static lock for thread-safe page fault handling across all cores
    // SpinLock: non-fair, avoids FIFO ordering overhead of ticket lock
    static SpinLock s_page_fault_lock;
public:

    /**
     * @brief 
     * Set by caller if user-space MimicOS is enabled (since allocation is done by the kernel)
     * Set by callee (inside handle_page_fault) if sniper-space MimicOS is enabled (since allocation is done by the Sniper)
     */
    struct AllocatorOut {
        int                          page_size;                 
        std::vector<UInt64>          prealloc_frames;           
        IntPtr                       ppn;                       
    };

    /**
     * @brief 
     * UNUSED if user-space MimicOS is enabled
     * Set by caller if sniper-space MimicOS is enabled, to let the allocator on sniper-space MimicOS side know how many frames to allocate
     */
    struct AllocatorIn {
        int                          metadata_frames; // Number of frames to allocate for metadata (i.e., page table frames)
        bool                         is_instruction;  // True if this is an instruction page allocation
    };

    struct FaultCtx
    {
        ParametricDramDirectoryMSI::PageTable*                   page_table;
        IntPtr                                                   vpn;
        AllocatorIn                                              alloc_in;                       // IN  – caller must fill
        AllocatorOut                                             alloc_out;                       // OUT – callee must fill
    };


    virtual void handle_exception(int exception_type_code, int argc, uint64_t *argv) = 0;
    virtual void handle_page_fault(FaultCtx& ctx) = 0;
    virtual PhysicalMemoryAllocator* getAllocator() = 0;


    ExceptionHandlerBase(Core* core) : m_core(core) {
        m_allocator = NULL; // Instantiate on demand, when getAllocator is called
    };
    virtual ~ExceptionHandlerBase() {};

protected:
    Core* m_core;
    PhysicalMemoryAllocator *m_allocator;

    // void mapFrames(ParametricDramDirectoryMSI::PageTable* page_table, UInt64 address, UInt64 core_id, UInt64 ppn, int page_size,
    //                              const std::vector<UInt64>& frames_already_allocated_by_virtuos);


    // static ExceptionHandler* m_instance;
};

#endif
