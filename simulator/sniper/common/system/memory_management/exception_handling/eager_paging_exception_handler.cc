#include "eager_paging_exception_handler.h"
#include "thread.h"
#include "debug_config.h"
#include "mimicos.h"
#include "simulator.h"
#include "core_manager.h"

EagerPagingExceptionHandler::EagerPagingExceptionHandler(Core *core) : ExceptionHandlerBase(core)
{
    std::cout << std::endl;
    std::cout << "[EXCEPTION_HANDLER] Initializing Eager Paging Exception Handler (sniper-space MimicOS)" << std::endl;
    log_file = std::ofstream();
    std::string log_file_name = "eager_paging_exception_handler.log." + std::to_string(core->getId());
    log_file_name = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + log_file_name;
    log_file.open(log_file_name.c_str());
    log_file << "[EXCEPTION_HANDLER] Initialised from the Core constructor" << std::endl;
    std::cout << "[EXCEPTION_HANDLER] Initialised from the Core constructor" << std::endl;
}

EagerPagingExceptionHandler::~EagerPagingExceptionHandler()
{
    if (log_file.is_open())
    {
        log_file.close();
    }
    std::cout << "[EXCEPTION_HANDLER] EagerPagingExceptionHandler destructor called" << std::endl;
}

void EagerPagingExceptionHandler::handle_exception(int exception_type_code, int argc, uint64_t *argv)
{
    std::cout << "[EagerPagingExceptionHandler] handle_exception not implemented yet... exiting with status code 1" << std::endl;
    exit(1);
}

void EagerPagingExceptionHandler::handle_page_fault(FaultCtx &ctx)
{
    // Acquire global lock for thread-safe page fault handling in multi-core
    ScopedLock sl(ExceptionHandlerBase::s_page_fault_lock);

#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    std::cout << "[EAGER_EXCEPTION_HANDLER] Handling page fault for address: " << (ctx.vpn << BASE_PAGE_SHIFT) << " in Sniper-space" << std::endl;
#endif

    int core_id = this->m_core->getId();
    Core *core_faulter = Sim()->getCoreManager()->getCoreFromID(core_id);
    Thread *thread_faulter = core_faulter->getThread();
    int app_id = thread_faulter->getAppId();

#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[EAGER_EXCEPTION_HANDLER] Handling page fault for address: " << (ctx.vpn << BASE_PAGE_SHIFT) 
             << " for core: " << core_id << std::endl;
    log_file << "[EAGER_EXCEPTION_HANDLER] We need to allocate: " << (ctx.alloc_in.metadata_frames) << " frames" << std::endl;
#endif

    // Eager paging: allocate the entire VMA that contains this address
    allocate_vma_eagerly(ctx, ctx.vpn << BASE_PAGE_SHIFT, app_id, ctx.alloc_in.metadata_frames);

    return;
}

void EagerPagingExceptionHandler::allocate_vma_eagerly(FaultCtx& ctx, UInt64 address, int app_id, int frames)
{
#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[EAGER_EXCEPTION_HANDLER] Allocating VMA eagerly for address: " << address << std::endl;
#endif

    // Find the VMA that contains this address
    std::vector<VMA> vma_list = Sim()->getMimicOS()->getVMA(app_id);
    VMA final_vma(0, 0);
    bool found = false;
    
    for (UInt32 i = 0; i < vma_list.size(); i++)
    {
        if (address >= vma_list[i].getBase() && address < vma_list[i].getEnd())
        {
#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
            log_file << "[EAGER_EXCEPTION_HANDLER] VMA found for address: " << address 
                     << " in VMA: " << vma_list[i].getBase() << " - " << vma_list[i].getEnd() << std::endl;
#endif
            final_vma = vma_list[i];
            found = true;
            break;
        }
    }

    if (!found)
    {
        std::cerr << "[FATAL] [EAGER_EXCEPTION_HANDLER] No VMA found for address: " << address << std::endl;
        assert(false);
    }

    assert(this->getAllocator() != NULL);

    // Allocate ranges for the entire VMA
    std::vector<Range> ranges = getAllocator()->allocate_ranges(final_vma.getBase(), final_vma.getEnd(), app_id);
    
#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[EAGER_EXCEPTION_HANDLER] Ranges allocated: " << ranges.size() << std::endl;
    log_file << "[EAGER_EXCEPTION_HANDLER] Allocating page table frames for VMA: " 
             << final_vma.getBase() << " - " << final_vma.getEnd() << std::endl;
#endif

    IntPtr current_vma_address = final_vma.getBase();

    for (UInt32 i = 0; i < ranges.size(); i++)
    {
#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
        log_file << "[EAGER_EXCEPTION_HANDLER] Range allocated: vpn=" << ranges[i].vpn 
                 << " bounds=" << ranges[i].bounds << " offset=" << ranges[i].offset << std::endl;
#endif

        int pages = ranges[i].bounds;
        IntPtr current_vpn = ranges[i].vpn;

#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
        log_file << "[EAGER_EXCEPTION_HANDLER] Allocating page table frames from vpn: " << current_vpn 
                 << " to " << current_vpn + pages << std::endl;
#endif

        for (int j = 0; j < pages; j++)
        {
            IntPtr current_address = (current_vpn + j) * FOUR_KIB;
            IntPtr ppn = ranges[i].offset + j;

#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
            log_file << "[EAGER_EXCEPTION_HANDLER] Allocating page table frames for vpn: " << current_vpn + j 
                     << " with ppn: " << ppn << std::endl;
#endif

            // Create a temporary context for this allocation
            FaultCtx temp_ctx = ctx;
            temp_ctx.alloc_out.ppn = ppn;
            temp_ctx.alloc_out.page_size = 12; // 4KB page

            allocate_page_table_frames(temp_ctx, current_address, m_core->getId(), ppn, 12, frames);
        }

        // Add range to VMA and range table
        final_vma.addPhysicalRange(ranges[i]);

        ParametricDramDirectoryMSI::RangeEntry entry;
        entry.offset = ranges[i].offset;

#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
        log_file << "[EAGER_EXCEPTION_HANDLER] Inserting range into range table: vpn=" << ranges[i].vpn 
                 << " bounds=" << ranges[i].bounds << " offset=" << ranges[i].offset << std::endl;
#endif

        Sim()->getMimicOS()->getRangeTable(app_id)->insert(
            std::make_pair(current_vma_address, current_vma_address + FOUR_KIB * ranges[i].bounds), entry);

        current_vma_address += FOUR_KIB * ranges[i].bounds;
    }

    final_vma.setAllocated(true);

    // Populate output for the original faulting address
    // Find the range that contains the faulting address
    for (const auto& range : ranges)
    {
        IntPtr range_start = range.vpn * FOUR_KIB;
        IntPtr range_end = range_start + range.bounds * FOUR_KIB;
        IntPtr fault_addr = ctx.vpn << BASE_PAGE_SHIFT;
        
        if (fault_addr >= range_start && fault_addr < range_end)
        {
            IntPtr offset_in_range = (fault_addr - range_start) / FOUR_KIB;
            ctx.alloc_out.ppn = range.offset + offset_in_range;
            ctx.alloc_out.page_size = 12;
            break;
        }
    }

    return;
}

void EagerPagingExceptionHandler::allocate_page_table_frames(FaultCtx &ctx,
                                                              UInt64 address, UInt64 core_id, UInt64 ppn, 
                                                              int page_size, int num_requested_frames)
{
#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[EAGER_EXCEPTION_HANDLER] Allocating page table frames: " << num_requested_frames << std::endl;
#endif

    int page_table_frames = num_requested_frames;
    std::vector<UInt64> &frames = ctx.alloc_out.prealloc_frames;
    frames.clear();

    for (int i = 0; i < page_table_frames; i++)
    {
        UInt64 frame = this->getAllocator()->handle_page_table_allocations(FOUR_KIB);
        
#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
        log_file << "[EAGER_EXCEPTION_HANDLER] Giving away page table frame: " << frame << std::endl;
#endif

        if (frame == static_cast<UInt64>(-1))
        {
            std::cerr << "[FATAL] [EAGER_EXCEPTION_HANDLER] Out of memory while allocating page table frames" << std::endl;
            assert(false);
        }

        frames.push_back(frame);
    }

    int frames_used = update_page_table_frames(address, core_id, ppn, page_size, frames);

#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[EAGER_EXCEPTION_HANDLER] Page table frames used: " << frames_used << std::endl;
    log_file << "[EAGER_EXCEPTION_HANDLER] Deallocating unused frames: " << (page_table_frames - frames_used) << std::endl;
#endif

    for (int i = 0; i < (page_table_frames - frames_used); i++)
    {
        this->getAllocator()->handle_page_table_deallocations(FOUR_KIB);
    }

    return;
}

int EagerPagingExceptionHandler::update_page_table_frames(UInt64 address, UInt64 core_id, UInt64 ppn, 
                                                           int page_size, std::vector<UInt64> &frames)
{
#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[EAGER_EXCEPTION_HANDLER] Updating page table frames for address: " << address 
             << " with ppn: " << ppn << " and page size: " << page_size << std::endl;
#endif

    MimicOS *os = Sim()->getMimicOS();
    assert(os);
    Core *core_faulter = Sim()->getCoreManager()->getCoreFromID(core_id);
    assert(core_faulter);
    Thread *thread_faulter = core_faulter->getThread();
    assert(thread_faulter);
    int app_id_faulter = thread_faulter->getAppId();

    int frames_used = os->getPageTable(app_id_faulter)->updatePageTableFrames(address, core_id, ppn, page_size, frames);

#if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
    log_file << "[EAGER_EXCEPTION_HANDLER] Frames used: " << frames_used << std::endl;
#endif

    return frames_used;
}

PhysicalMemoryAllocator* EagerPagingExceptionHandler::getAllocator() 
{
    if (m_allocator == NULL) {
        std::cout << "[EAGER_EXCEPTION_HANDLER] Allocator is NULL, getting it from the MimicOS" << std::endl;
        m_allocator = Sim()->getMimicOS()->getMemoryAllocator();
    }

    return m_allocator;
}
