#include "log.h"
#include <fstream>
#include "simulator.h"
#include "core.h"
#include "thread.h"
#include "mimicos.h"
#include "memory_manager_base.h"

#include "user_space_exception_handler.h"
#include "thread.h"

#include "debug_config.h"

#define BASE_PAGE_SHIFT 12UL

VirtuosExceptionHandler::VirtuosExceptionHandler(Core *core) : ExceptionHandlerBase(core)
{
    // Initialise log structures
    std::cout << std::endl;
    std::cout << "[EXCEPTION_HANDLER] Initializing Exception Handler (user-space MimicOS)" << std::endl;
    std::cout << "[EXCEPTION_HANDLER] Initialised from the Core constructor" << std::endl;
}

VirtuosExceptionHandler::~VirtuosExceptionHandler()
{
    std::cout << "[EXCEPTION_HANDLER] VirtuosExceptionHandler destructor called" << std::endl;
}

void VirtuosExceptionHandler::handle_exception(int exception_type_code, int argc, uint64_t *argv)
{
    // Handle the exception
#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    std::cout << "[EXCEPTION_HANDLER] handle_exception " << std::endl;
#endif

    assert(MimicOS::protocol_codes_decode.find(exception_type_code) != MimicOS::protocol_codes_decode.end()); // Otherwise: exception is invalid

    const std::string &exception_name = MimicOS::protocol_codes_decode[exception_type_code];
#if DEBUG_EXCEPTION_HANDLER >= DEBUG_DETAILED
    std::cout << "[EXCEPTION_HANDLER] Handling exception: " << exception_name << std::endl;
#endif

    if (exception_name == "page_fault")
    {
        int app_id = m_core->getThread()->getAppId();
        auto *page_table = Sim()->getMimicOS()->getPageTable(app_id);
        assert(argc >= 4 && argv[0] == static_cast<uint64_t>(exception_type_code));
        uint64_t vpn = argv[1];
        uint64_t ppn = argv[2];
        uint64_t page_size = argv[3];
        std::vector<UInt64> frames_already_allocated_by_virtuos;
        for (int i = 4; i < argc; ++i)
        {
            frames_already_allocated_by_virtuos.push_back(argv[i]);
        }

        // Populate FaultCtx
        FaultCtx ctx{
            .page_table = page_table,
            .vpn = vpn,
            .alloc_in = {
                .metadata_frames = -1, // UNUSED
                .is_instruction = false // Default to data for userspace page faults
            },
            .alloc_out = {
                .page_size = static_cast<int>(page_size),
                .prealloc_frames = std::move(frames_already_allocated_by_virtuos),
                .ppn = ppn,
            },
        };

        //   handle_page_fault(page_table, vpn, ppn, page_size, frames_already_allocated_by_virtuos);
        // Call the handle_page_fault method
        handle_page_fault(ctx);
    }
    else
    {
        std::cout << "[FATAL] [EXCEPTION_HANDLER] Exception " << exception_name << " was not implemented yet..." << std::endl;
        assert(false);
    }
}

void VirtuosExceptionHandler::handle_page_fault(FaultCtx &ctx)
{
    // Acquire global lock for thread-safe page fault handling in multi-core
    ScopedLock sl(ExceptionHandlerBase::s_page_fault_lock);

    // 1. Deserialize ctx
    auto* page_table = ctx.page_table;
    IntPtr vpn = ctx.vpn;
    int page_size = ctx.alloc_out.page_size;
    IntPtr ppn = ctx.alloc_out.ppn;
    std::vector<UInt64>& frames_already_allocated_by_virtuos = ctx.alloc_out.prealloc_frames;

#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    std::cout << "[EXCEPTION_HANDLER] Handling page fault for page_table = " << page_table <<
                 " - mapping vpn = " << vpn <<
                 " to ppn = "        << ppn  << std::endl;
#endif
    
    // TODO: @vlnitu add Swapping - migrate @kanellok's implementation from page_fault_handler.h [STALE]

    // 2. Update the page table with the frames that were already allocated by user-space MimicOS
    int core_id = m_core->getId();
    update_page_table_frames(page_table, vpn << BASE_PAGE_SHIFT, core_id, ppn, page_size, frames_already_allocated_by_virtuos);

#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    std::cout << "[EXCEPTION_HANDLER] Installed " << frames_already_allocated_by_virtuos.size() << " frames (that were already alloc'd by VirtuOS), as a result of handling the PF" << std::endl;
    std::cout << "[EXCEPTION_HANDLER] vpn = 0x" << std::hex << vpn << std::dec << std::endl;
    std::cout << "[EXCEPTION_HANDLER] accesses_per_vpn[vpn] = " << page_table->getAccessesPerVPN(vpn)  << std::endl;
#endif

    return;
}

int VirtuosExceptionHandler::update_page_table_frames(ParametricDramDirectoryMSI::PageTable* page_table, UInt64 address, UInt64 core_id, UInt64 ppn,
                             int page_size, const std::vector<UInt64>& frames_already_allocated_by_virtuos)
{
    // First lets ask the page table if new frames are needed
#if DEBUG_EXCEPTION_HANDLER >= DEBUG_DETAILED
        std::cout << "[EXCEPTION_HANDLER] Mapping " << frames_already_allocated_by_virtuos.size() << " frames - updating Page Table structure..." << std::endl;
#endif

    int frames_used = page_table->updatePageTableFrames(address, core_id, ppn, page_size, frames_already_allocated_by_virtuos);
#if DEBUG_EXCEPTION_HANDLER >= DEBUG_DETAILED
        std::cout << "[EXCEPTION_HANDLER] Frames used: " << frames_used << std::endl;
#endif
    (void)frames_used;

    // TODO @vlnitu: migrate deallocation allocators as well
    int frames_requested = frames_already_allocated_by_virtuos.size();
#if DEBUG_EXCEPTION_HANDLER >= DEBUG_DETAILED
    std::cout << "[EXCEPTION_HANDLER] Frames requested: " <<  frames_requested << std::endl;
    std::cout << "[EXCEPTION_HANDLER] Frames used: " << frames_used << std::endl;   
    std::cout << "[EXCEPTION_HANDLER] Leaking !! page table frames: " << (frames_requested - frames_used) << std::endl;
#endif

    return frames_requested;
}

PhysicalMemoryAllocator* VirtuosExceptionHandler::getAllocator() { 
    std::cerr << "[EXCEPTION_HANDLER] getAllocator called for user-space MimicOS (VirtuOS)" << std::endl;
    std::cerr << "[EXCEPTION_HANDLER] there's no allocator exposed... exiting with status code 1" << std::endl;
    exit(1);
}
