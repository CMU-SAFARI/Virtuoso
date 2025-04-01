#include "utopia_page_fault_handler.h"
#include "physical_memory_allocator.h"
#include "simulator.h"
#include "thread.h"
#include "core_manager.h"
#include "mimicos.h"
#include "instruction.h"
#include <cassert>

//#define DEBUG

/*
 * The UtopiaPageFaultHandler class extends PageFaultHandlerBase to handle page faults
 * within the Utopia memory allocation model. When a page fault occurs, this handler
 * attempts to allocate memory for the faulting address. If the memory is allocated
 * in RestSeg, no additional frames are needed; otherwise, it may allocate frames
 * for the page table as well.
 */
UtopiaPageFaultHandler::UtopiaPageFaultHandler(PhysicalMemoryAllocator *allocator)
    : PageFaultHandlerBase(allocator)
{
    // Create a log file specifically for debugging/tracing page faults in Utopia.
    log_file_name = "utopia_page_fault_handler.log";
    log_file_name = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + log_file_name;
    log_file.open(log_file_name);
    log_file << "[UTOPIA_PF_HANDLER] Utopia Page Fault Handler created" << std::endl;
    log_file << "[UTOPIA_PF_HANDLER] Allocator: " << this->allocator->getName() << std::endl;
}

UtopiaPageFaultHandler::~UtopiaPageFaultHandler()
{
    // Any custom cleanup can be placed here
}

/*
 * allocatePagetableFrames(...)
 *   - Some page-table implementations can require additional frames for multi-level
 *     page tables (e.g., to store new page-table entries). This function allocates
 *     those frames if needed.
 *   - 'address'     : The faulting virtual address.
 *   - 'core_id'     : The core that generated the fault.
 *   - 'ppn'         : The physical page number that was allocated for the faulting page.
 *   - 'page_size'   : The size (in bits) of this page. (E.g., 12 for 4KB.)
 *   - 'frame_number': How many extra frames are potentially needed for the page table.
 *
 * The function retrieves 'frame_number' frames via the page table allocator, then
 * updates the page table structure for the (address->ppn) mapping. If fewer frames
 * are actually used, the remainder is freed (handle_page_table_deallocations).
 */
void UtopiaPageFaultHandler::allocatePagetableFrames(UInt64 address, UInt64 core_id, UInt64 ppn,
                                                     int page_size, int frame_number)
{
#ifdef DEBUG
    log_file << "[UTOPIA_PF_HANDLER] Allocating page table frames: " << frame_number << std::endl;
#endif

    int page_table_frames = frame_number;
    std::vector<UInt64> frames;
    frames.reserve(page_table_frames);

    // Allocate 'page_table_frames' 4KB frames for page table usage
    for (int i = 0; i < page_table_frames; i++)
    {
        UInt64 frame = allocator->handle_page_table_allocations(4096);
#ifdef DEBUG
        log_file << "[UTOPIA_PF_HANDLER] Giving away page table frame: " << frame << std::endl;
#endif
        // If we fail to allocate, the system is out of memory.
        if (frame == static_cast<UInt64>(-1))
        {
            assert(false);  // This scenario triggers an assertion failure
        }
        frames.push_back(frame);
    }

    // Retrieve the core and thread that experienced the fault
    Core* core_faulter = Sim()->getCoreManager()->getCoreFromID(core_id);
    Thread* thread_faulter = core_faulter->getThread();
    int app_id_faulter = thread_faulter->getAppId();

    // Update the page table frames for the given address->ppn mapping
    int frames_used = Sim()->getMimicOS()->getPageTable(app_id_faulter)->updatePageTableFrames(
        address, core_id, ppn, page_size, frames);

#ifdef DEBUG
    log_file << "[UTOPIA_PF_HANDLER] Page table frames used: " << frames_used << std::endl;
#endif

    // If not all frames were needed, deallocate the surplus
    for (int i = 0; i < (page_table_frames - frames_used); i++)
    {
        this->allocator->handle_page_table_deallocations(4096);
    }

    return;
}

/*
 * handlePageFault(...)
 *   - This is the main entry point for servicing a page fault. The steps are:
 *       1) Allocate a 4KB page for 'address' using the Utopia allocator.
 *       2) Check if the last allocation ended up in RestSeg. If so, we do not need
 *          additional page-table frames. Otherwise, we call allocatePagetableFrames
 *          to see if the page table requires more frames.
 *
 *   - 'address': The faulting address.
 *   - 'core_id': The ID of the core that caused the fault.
 *   - 'frames' : Number of frames that might be needed for page-table expansions 
 *                (passed directly to allocatePagetableFrames if required).
 */
void UtopiaPageFaultHandler::handlePageFault(UInt64 address, UInt64 core_id, int frames)
{
#ifdef DEBUG
    log_file << "[UTOPIA_PF_HANDLER] Handling page fault for address: " << address 
             << " for core: " << core_id << std::endl;
    log_file << "[UTOPIA_PF_HANDLER] We need to allocate: " << frames << " frames" << std::endl;
#endif

    // Ask the allocator for a 4KB page to handle this fault
    std::pair<UInt64, UInt64> allocation_result = this->allocator->allocate(4096, address, core_id, false);

#ifdef DEBUG
    log_file << "[UTOPIA_PF_HANDLER] Allocation result: " << allocation_result.first 
             << " " << allocation_result.second << std::endl;
#endif

    // Check if the page was allocated in RestSeg (the "fast path" for Utopia)
    Utopia* utopia = dynamic_cast<Utopia*>(allocator);
    if (utopia->getLastAllocatedInRestSeg())
    {
#ifdef DEBUG
        log_file << "[UTOPIA_PF_HANDLER] Last allocation was in RestSeg" << std::endl;
#endif
        return; // No additional frames needed for the page table
    }
    else
    {
        // The page ended up allocated in FlexSeg (buddy allocator). Now see if
        // we need to allocate frames for any page-table expansions
        int page_size = allocation_result.second;
        int max_level = Sim()->getMimicOS()->getPageTable(core_id)->getMaxLevel();
#ifdef DEBUG
        log_file << "[UTOPIA_PF_HANDLER] Maximum number of frames: " << max_level << std::endl;
#endif
        allocatePagetableFrames(address, core_id, allocation_result.first, page_size, max_level);
    }

#ifdef DEBUG
    log_file << "[UTOPIA_PF_HANDLER] Page fault handled with allocation in FlexSeg" << std::endl;
#endif
    return;
}
