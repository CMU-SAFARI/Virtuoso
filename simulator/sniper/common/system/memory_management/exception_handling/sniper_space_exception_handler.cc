
#include "sniper_space_exception_handler.h"
#include "thread.h"
#include "debug_config.h"

// Static lock definition for thread-safe page fault handling across all cores
// SpinLock: non-fair, avoids FIFO ordering overhead of ticket lock
SpinLock ExceptionHandlerBase::s_page_fault_lock;

SniperExceptionHandler::SniperExceptionHandler(Core *core) : ExceptionHandlerBase(core)
{
    // Initialise log structures
    std::cout << std::endl;
    std::cout << "[EXCEPTION_HANDLER] Initializing Exception Handler (sniper-space MimicOS)" << std::endl;
    log_file = std::ofstream();
    std::string log_file_name = "exception_handler.log." + std::to_string(core->getId());
    log_file_name = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + log_file_name;
    log_file.open(log_file_name.c_str());
    log_file << "[EXCEPTION_HANDLER] Initialised from the Core constructor" << std::endl;
    std::cout << "[EXCEPTION_HANDLER] Initialised from the Core constructor" << std::endl;
}

SniperExceptionHandler::~SniperExceptionHandler()
{
    if (log_file.is_open())
    {
        log_file.close();
    }
    std::cout << "[EXCEPTION_HANDLER] SniperExceptionHandler destructor called" << std::endl;
}

void SniperExceptionHandler::handle_exception(int exception_type_code, int argc, uint64_t *argv)
{
    // Handle the exception
    std::cout << "[SniperExceptionHandler] handle_exception not implemented yet... exiting with status code 1" << std::endl;
    exit(1);
}

void SniperExceptionHandler::handle_page_fault(FaultCtx &ctx)
{
    // Acquire global lock for thread-safe page fault handling in multi-core
    ScopedLock sl(ExceptionHandlerBase::s_page_fault_lock);

#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    std::cout << "[EXCEPTION_HANDLER] Handling page fault for address: " << (ctx.vpn << BASE_PAGE_SHIFT) << " in Sniper-space" << std::endl;
#endif
    int core_id = this->m_core->getId();
    Core *core_faulter = Sim()->getCoreManager()->getCoreFromID(core_id);
    Thread *thread_faulter = core_faulter->getThread();
    int app_id = thread_faulter->getAppId();

    // TODO @vlnitu: migrate swapping...
    // bool is_swapped = false;
    // if(Sim()->getMimicOS()->isSwapEnabled()){
    //     // If the swap is enabled, we need to check if the page is in the swap space
    //     is_swapped = handleSwapping(address, core_id);
    // }
    // if(is_swapped)
    //     Sim()->getMimicOS()->setLastPageFaultCausedSwapping(is_swapped);

    // 1. Allocate the frames
#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[EXCEPTION_HANDLER] Handling page fault for address: " << (ctx.vpn << BASE_PAGE_SHIFT) << " for core: " << core_id << std::endl;
    log_file << "[EXCEPTION_HANLDER] We need to allocate: " << (ctx.alloc_in.metadata_frames) << " frames" << std::endl;
#endif

    assert(this->getAllocator() != NULL);
    const auto &[ppn, page_size] = this->getAllocator()->allocate(FOUR_KIB, ctx.vpn << BASE_PAGE_SHIFT, core_id, false, ctx.alloc_in.is_instruction);

#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[EXCEPTION_HANDLER] Allocated page: " << ppn << " with page size: " << page_size << std::endl;
    std::cout << "[EXCEPTION_HANDLER] Allocated page: " << ppn << " with page size: " << page_size << std::endl;
#endif
    // Populate output paramaters
    ctx.alloc_out.ppn = ppn;
    ctx.alloc_out.page_size = page_size;

    // 2. Update PTs
    allocate_page_table_frames(ctx, ctx.vpn << BASE_PAGE_SHIFT, core_id, ppn, page_size, ctx.alloc_in.metadata_frames);
#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[EXCEPTION_HANDLER] Page table frames allocated for address: " << (ctx.vpn << BASE_PAGE_SHIFT) <<
                " -- allocated " << ctx.alloc_out.prealloc_frames.size() << " frames" << std::endl;
    std::cout << "[EXCEPTION_HANDLER] Page table frames allocated for address: " << (ctx.vpn << BASE_PAGE_SHIFT) <<
                " -- allocated " << ctx.alloc_out.prealloc_frames.size() << " frames" << std::endl;
#endif

    // If the page is allocated, return
    return;
}

void SniperExceptionHandler::allocate_page_table_frames(FaultCtx &ctx,
                                                        UInt64 address, UInt64 core_id, UInt64 ppn, int page_size, int num_requested_frames)
{
    // First lets ask the page table if new frames are needed
#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[EXCEPTION_HANDLER] Allocating page table frames: " << num_requested_frames << std::endl;
    std::cout << "[EXCEPTION_HANDLER] Allocating page table frames: " << num_requested_frames << std::endl;
#endif

    int page_table_frames = num_requested_frames;
    std::vector<UInt64> &frames = ctx.alloc_out.prealloc_frames;

    // TODO @vlnitu: migrate other allocators - allocate metadata frames stage (i.e., asap, etc.)
    for (int i = 0; i < page_table_frames; i++)
    {
        UInt64 frame = this->getAllocator()->handle_page_table_allocations(FOUR_KIB, core_id);
#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
        log_file << "[EXCEPTION_HANDLER] Giving away page table frame: " << frame << std::endl;
        std::cout << "[EXCEPTION_HANDLER] Giving away page table frame: " << frame << std::endl;
#endif
        if (frame == static_cast<UInt64>(-1))
        {
            // We are out of memory
            std::cerr << "[FATAL] [EXCEPTION_HANDLER] Out of memory while allocating page table frames" << std::endl;
            std::cerr << "[FATAL] [EXCEPTION_HANDLER] Exiting with status code 1..." << std::endl;
            assert(false);
        }

        frames.push_back(frame);
    }

    // TODO @vlnitu: migrate virtualisation (is_guest)

    // create a function update_page_table_frames that will update the page table frames
    int frames_used = update_page_table_frames(address, core_id, ppn, page_size, frames);

#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[EXCEPTION_HANDLER] Page table frames used: " << frames_used << std::endl;
    std::cout<< "[EXCEPTION_HANDLER] Page table frames used: " << frames_used << std::endl;
#endif

    // TODO @vlnitu: migrate other allocators - dealocation of unused frames stage (i.e., asap, etc.)
#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[EXCEPTION_HANDLER] Frames requested: " << page_table_frames << std::endl;
    std::cout << "[EXCEPTION_HANDLER] Frames requested: " << page_table_frames << std::endl;
    log_file << "[EXCEPTION_HANDLER] Frames used: " << frames_used << std::endl;
    std::cout << "[EXCEPTION_HANDLER] Frames used: " << frames_used << std::endl;
    log_file << "[EXCEPTION_HANDLER] Deallocating page table frames: " << (page_table_frames - frames_used) << std::endl;
    std::cout << "[EXCEPTION_HANDLER] Deallocating page table frames: " << (page_table_frames - frames_used) << std::endl;
#endif
    for (int i = 0; i < (page_table_frames - frames_used); i++)
    {
        this->getAllocator()->handle_page_table_deallocations(FOUR_KIB);
    }
    return;
}

int SniperExceptionHandler::update_page_table_frames(UInt64 address, UInt64 core_id, UInt64 ppn, int page_size, std::vector<UInt64> &frames)
{

#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[EXCEPTION_HANDLER] Updating page table frames for address: " << address << " with ppn: " << ppn << " and page size: " << page_size << std::endl;
    std::cout << "[EXCEPTION_HANDLER] Updating page table frames for address: " << address << " with ppn: " << ppn << " and page size: " << page_size << std::endl;
#endif
    MimicOS *os = Sim()->getMimicOS();
    assert(os);
    Core *core_faulter = Sim()->getCoreManager()->getCoreFromID(core_id);
    assert(core_faulter);
    Thread *thread_faulter = core_faulter->getThread();
    assert(thread_faulter);
    int app_id_faulter = thread_faulter->getAppId();

#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[EXCEPTION_HANDLER] App ID of the faulter: " << app_id_faulter << std::endl;
    std::cout << "[EXCEPTION_HANDLER] App ID of the faulter: " << app_id_faulter << std::endl;
#endif

    // Update the page table frames
#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[EXCEPTION_HANDLER] Updating page table frames for address: " << address << " with ppn: " << ppn << " and page size: " << page_size << std::endl;
    std::cout << "[EXCEPTION_HANDLER] Updating page table frames for address: " << address << " with ppn: " << ppn << " and page size: " << page_size << std::endl;
#endif
    assert(os->getPageTable(app_id_faulter));
    int frames_used = os->getPageTable(app_id_faulter)->updatePageTableFrames(address, core_id, ppn, page_size, frames);
#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[EXCEPTION_HANDLER] Frames used: " << frames_used << std::endl;
    std::cout << "[EXCEPTION_HANDLER] Frames used: " << frames_used << std::endl;
#endif
    // TODO @vlnitu: migrate deallocation allocators as well
    return frames_used;
}


PhysicalMemoryAllocator* SniperExceptionHandler::getAllocator() { 
    if (m_allocator == NULL) {
        std::cout << "[EXCEPTION_HANDLER] Allocator is NULL, getting it from the MimicOS" << std::endl;
        m_allocator = Sim()->getMimicOS()->getMemoryAllocator();
    }

    return m_allocator; 
}