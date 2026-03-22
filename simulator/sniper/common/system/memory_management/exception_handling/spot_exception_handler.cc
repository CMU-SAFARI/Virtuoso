#include "spot_exception_handler.h"
#include "thread.h"
#include "debug_config.h"
#include "mimicos.h"
#include "simulator.h"
#include "core_manager.h"

SpotExceptionHandler::SpotExceptionHandler(Core *core, bool is_guest_) 
    : ExceptionHandlerBase(core), is_guest(is_guest_)
{
    std::cout << std::endl;
    std::cout << "[EXCEPTION_HANDLER] Initializing Spot Exception Handler (sniper-space MimicOS)" << std::endl;
    log_file = std::ofstream();
    std::string log_file_name = "spot_exception_handler.log." + std::to_string(core->getId());
    log_file_name = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + log_file_name;
    log_file.open(log_file_name.c_str());
    log_file << "[EXCEPTION_HANDLER] Initialised from the Core constructor" << std::endl;
    std::cout << "[EXCEPTION_HANDLER] Initialised from the Core constructor" << std::endl;
}

SpotExceptionHandler::~SpotExceptionHandler()
{
    if (log_file.is_open())
    {
        log_file.close();
    }
    std::cout << "[EXCEPTION_HANDLER] SpotExceptionHandler destructor called" << std::endl;
}

void SpotExceptionHandler::handle_exception(int exception_type_code, int argc, uint64_t *argv)
{
    std::cout << "[SpotExceptionHandler] handle_exception not implemented yet... exiting with status code 1" << std::endl;
    exit(1);
}

void SpotExceptionHandler::handle_page_fault(FaultCtx &ctx)
{
    // Acquire global lock for thread-safe page fault handling in multi-core
    ScopedLock sl(ExceptionHandlerBase::s_page_fault_lock);

#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    std::cout << "[SPOT_EXCEPTION_HANDLER] Handling page fault for address: " << (ctx.vpn << BASE_PAGE_SHIFT) 
              << " in Sniper-space (guest=" << is_guest << ")" << std::endl;
#endif

    int core_id = this->m_core->getId();
    Core *core_faulter = Sim()->getCoreManager()->getCoreFromID(core_id);
    Thread *thread_faulter = core_faulter->getThread();
    int app_id = thread_faulter->getAppId();

#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[SPOT_EXCEPTION_HANDLER] Handling page fault for address: " << (ctx.vpn << BASE_PAGE_SHIFT) 
             << " for core: " << core_id << std::endl;
    log_file << "[SPOT_EXCEPTION_HANDLER] We need to allocate: " << (ctx.alloc_in.metadata_frames) << " frames" << std::endl;
#endif

    assert(this->getAllocator() != NULL);

    // 1. Allocate the frame for the faulting page
    const auto &[ppn, page_size] = this->getAllocator()->allocate(FOUR_KIB, ctx.vpn << BASE_PAGE_SHIFT, core_id, false);

#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[SPOT_EXCEPTION_HANDLER] Allocated page: " << ppn << " with page size: " << page_size << std::endl;
    std::cout << "[SPOT_EXCEPTION_HANDLER] Allocated page: " << ppn << " with page size: " << page_size << std::endl;
#endif

    // Populate output parameters
    ctx.alloc_out.ppn = ppn;
    ctx.alloc_out.page_size = page_size;

    // 2. Update page tables
    allocate_page_table_frames(ctx, ctx.vpn << BASE_PAGE_SHIFT, core_id, ppn, page_size, ctx.alloc_in.metadata_frames);

#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[SPOT_EXCEPTION_HANDLER] Page table frames allocated for address: " << (ctx.vpn << BASE_PAGE_SHIFT)
             << " -- allocated " << ctx.alloc_out.prealloc_frames.size() << " frames" << std::endl;
#endif

    return;
}

void SpotExceptionHandler::allocate_page_table_frames(FaultCtx &ctx,
                                                       UInt64 address, UInt64 core_id, UInt64 ppn, 
                                                       int page_size, int num_requested_frames)
{
#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[SPOT_EXCEPTION_HANDLER] Allocating page table frames: " << num_requested_frames << std::endl;
#endif

    int page_table_frames = num_requested_frames;
    std::vector<UInt64> &frames = ctx.alloc_out.prealloc_frames;

    for (int i = 0; i < page_table_frames; i++)
    {
        UInt64 frame = this->getAllocator()->handle_page_table_allocations(FOUR_KIB);

#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
        log_file << "[SPOT_EXCEPTION_HANDLER] Giving away page table frame: " << frame << std::endl;
#endif

        if (frame == static_cast<UInt64>(-1))
        {
            std::cerr << "[FATAL] [SPOT_EXCEPTION_HANDLER] Out of memory while allocating page table frames" << std::endl;
            assert(false);
        }

        frames.push_back(frame);
    }

    int frames_used = update_page_table_frames(address, core_id, ppn, page_size, frames);

#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[SPOT_EXCEPTION_HANDLER] Page table frames used: " << frames_used << std::endl;
    log_file << "[SPOT_EXCEPTION_HANDLER] Deallocating unused frames: " << (page_table_frames - frames_used) << std::endl;
#endif

    for (int i = 0; i < (page_table_frames - frames_used); i++)
    {
        this->getAllocator()->handle_page_table_deallocations(FOUR_KIB);
    }

    return;
}

int SpotExceptionHandler::update_page_table_frames(UInt64 address, UInt64 core_id, UInt64 ppn, 
                                                    int page_size, std::vector<UInt64> &frames)
{
#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[SPOT_EXCEPTION_HANDLER] Updating page table frames for address: " << address 
             << " with ppn: " << ppn << " and page size: " << page_size << std::endl;
#endif

    MimicOS *os;
    if (is_guest) {
        os = Sim()->getMimicOS_VM();
    } else {
        os = Sim()->getMimicOS();
    }
    assert(os);

    Core *core_faulter = Sim()->getCoreManager()->getCoreFromID(core_id);
    assert(core_faulter);
    Thread *thread_faulter = core_faulter->getThread();
    assert(thread_faulter);
    int app_id_faulter = thread_faulter->getAppId();

#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[SPOT_EXCEPTION_HANDLER] App ID of the faulter: " << app_id_faulter << std::endl;
#endif

    int frames_used = os->getPageTable(app_id_faulter)->updatePageTableFrames(address, core_id, ppn, page_size, frames);

#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[SPOT_EXCEPTION_HANDLER] Frames used: " << frames_used << std::endl;
#endif

    return frames_used;
}

PhysicalMemoryAllocator* SpotExceptionHandler::getAllocator() 
{
    if (m_allocator == NULL) {
        std::cout << "[SPOT_EXCEPTION_HANDLER] Allocator is NULL, getting it from the MimicOS" << std::endl;
        if (is_guest) {
            m_allocator = Sim()->getMimicOS_VM()->getMemoryAllocator();
        } else {
            m_allocator = Sim()->getMimicOS()->getMemoryAllocator();
        }
    }

    return m_allocator;
}
