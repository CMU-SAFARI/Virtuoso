#include "utopia_exception_handler.h"
#include "thread.h"
#include "debug_config.h"
#include "mimicos.h"
#include "simulator.h"
#include "core_manager.h"
#include "utopia_policy.h"

UtopiaExceptionHandler::UtopiaExceptionHandler(Core *core, bool is_guest_) 
    : ExceptionHandlerBase(core), 
      sim_log("UTOPIA_EXC_HANDLER", core ? core->getId() : -1, DEBUG_UTOPIA),
      is_guest(is_guest_)
{
    std::cout << std::endl;
    std::cout << "[UTOPIA_EXCEPTION_HANDLER] Initializing Utopia Exception Handler" << std::endl;
    std::cout << "[UTOPIA_EXCEPTION_HANDLER]   ├─ RestSeg allocations: SKIP page table updates" << std::endl;
    std::cout << "[UTOPIA_EXCEPTION_HANDLER]   └─ FlexSeg allocations: UPDATE page tables" << std::endl;
    
    // Initialize logging
    log_file = std::ofstream();
    std::string log_file_name = "utopia_exception_handler.log." + std::to_string(core->getId());
    log_file_name = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + log_file_name;
    log_file.open(log_file_name.c_str());
    log_file << "[UTOPIA_EXCEPTION_HANDLER] Initialized for core " << core->getId() << std::endl;
}

UtopiaExceptionHandler::~UtopiaExceptionHandler()
{
    if (log_file.is_open())
    {
        log_file.close();
    }
    std::cout << "[UTOPIA_EXCEPTION_HANDLER] Destructor called" << std::endl;
}

void UtopiaExceptionHandler::handle_exception(int exception_type_code, int argc, uint64_t *argv)
{
    std::cout << "[UTOPIA_EXCEPTION_HANDLER] handle_exception not implemented yet... exiting with status code 1" << std::endl;
    exit(1);
}

void UtopiaExceptionHandler::handle_page_fault(FaultCtx &ctx)
{
    // Acquire global lock for thread-safe page fault handling in multi-core
    ScopedLock sl(ExceptionHandlerBase::s_page_fault_lock);

    UInt64 fault_address = ctx.vpn << BASE_PAGE_SHIFT;

#if DEBUG_UTOPIA >= DEBUG_BASIC
    sim_log.info("╔══════════════════════════════════════════════════════════════════╗");
    sim_log.info("║          UTOPIA EXCEPTION HANDLER - PAGE FAULT                   ║");
    sim_log.info("╠══════════════════════════════════════════════════════════════════╣");
    sim_log.info("║ Address: 0x%lx (VPN: 0x%lx)", fault_address, ctx.vpn);
    sim_log.info("║ Guest Mode: %s", is_guest ? "YES" : "NO");
    sim_log.info("╚══════════════════════════════════════════════════════════════════╝");
#endif

    int core_id = this->m_core->getId();
    Core *core_faulter = Sim()->getCoreManager()->getCoreFromID(core_id);
    Thread *thread_faulter = core_faulter->getThread();
    int app_id = thread_faulter->getAppId();

#if DEBUG_UTOPIA >= DEBUG_DETAILED
    log_file << "[UTOPIA_EXCEPTION_HANDLER] Page fault at address: 0x" << std::hex << fault_address
             << " for core: " << std::dec << core_id << " app: " << app_id << std::endl;
    log_file << "[UTOPIA_EXCEPTION_HANDLER] Requested metadata frames: " << ctx.alloc_in.metadata_frames << std::endl;
#endif

    assert(this->getAllocator() != NULL);

    // ========================================================================
    // Step 1: Allocate the physical frame via Utopia's 4-level hierarchy
    // ========================================================================
    // The allocator will try: RestSeg-2MB → FlexSeg-THP → RestSeg-4KB → FlexSeg-4KB
    // After allocation, we can query if RestSeg was used via getLastAllocatedInRestSeg()
    
    const auto &[ppn, page_size] = this->getAllocator()->allocate(FOUR_KIB, fault_address, core_id, false);

#if DEBUG_UTOPIA >= DEBUG_BASIC
    sim_log.info("  ├─ Allocation Result:");
    sim_log.info("  │    PPN: 0x%lx", ppn);
    sim_log.info("  │    Page Size: %d bytes (%s)", page_size, 
                 page_size == (2 << 20) ? "2MB THP" : "4KB base");
#endif

    // Populate output parameters
    ctx.alloc_out.ppn = ppn;
    ctx.alloc_out.page_size = page_size;

    // ========================================================================
    // Step 2: Check if allocation was in RestSeg or FlexSeg
    // ========================================================================
    bool in_restseg = checkLastAllocWasRestSeg();

#if DEBUG_UTOPIA >= DEBUG_BASIC
    if (in_restseg) {
        sim_log.info("  ├─ Segment: RestSeg (range-based translation)");
        sim_log.info("  │    → SKIPPING page table update");
        sim_log.info("  │    → Translation: PA = VA + offset");
    } else {
        sim_log.info("  ├─ Segment: FlexSeg (conventional page tables)");
        sim_log.info("  │    → UPDATING page table entries");
        sim_log.info("  │    → Allocating %d PT frames", ctx.alloc_in.metadata_frames);
    }
#endif

    // ========================================================================
    // Step 3: Update page tables ONLY for FlexSeg allocations
    // ========================================================================
    // RestSeg uses Utopia's range-based translation hardware:
    //   - Virtual-to-physical is computed as: PA = VA + segment_offset
    //   - No page table entries needed
    //   - This is the key optimization of Utopia!
    //
    // FlexSeg uses conventional x86-64 page tables:
    //   - Need to allocate page table frames (PML4, PDPT, PD, PT)
    //   - Need to create PTE mappings
    
    if (!in_restseg) {
        // FlexSeg: Need to allocate and update page table frames
        allocate_page_table_frames(ctx, fault_address, core_id, ppn, page_size, ctx.alloc_in.metadata_frames);
        
#if DEBUG_UTOPIA >= DEBUG_DETAILED
        log_file << "[UTOPIA_EXCEPTION_HANDLER] FlexSeg: Allocated " << ctx.alloc_out.prealloc_frames.size() 
                 << " page table frames" << std::endl;
#endif
    } else {
        // RestSeg: No page table frames needed
        // Just ensure prealloc_frames is empty
        ctx.alloc_out.prealloc_frames.clear();
        
#if DEBUG_UTOPIA >= DEBUG_DETAILED
        log_file << "[UTOPIA_EXCEPTION_HANDLER] RestSeg: Skipped page table frame allocation" << std::endl;
#endif
    }

#if DEBUG_UTOPIA >= DEBUG_BASIC
    sim_log.info("  └─ Page Fault Handling Complete");
    sim_log.info("");
#endif

    return;
}

bool UtopiaExceptionHandler::checkLastAllocWasRestSeg()
{
    // The Utopia allocator tracks whether the last allocation was in RestSeg
    // We need to dynamically check this since we're using a template class
    
    // Try to get the allocator and check its type
    PhysicalMemoryAllocator* alloc = getAllocator();
    if (!alloc) {
        return false;
    }
    
    // The allocator should have a method getLastAllocatedInRestSeg()
    // We use a simple approach: check if the allocator name contains "utopia"
    // and if so, use the virtual method
    
    // For now, we'll use a dynamic approach via the allocator interface
    // The PhysicalMemoryAllocator base class should have been extended with this method
    // If not, we default to false (assume FlexSeg, safer option)
    
    // Check if allocator is UtopiaAllocator using RTTI
    // Note: This requires RTTI to be enabled in the build
    
    // Simple approach: The Utopia allocator is configured through MimicOS
    // We can check the allocator's virtual method if it exists
    MimicOS* os = is_guest ? Sim()->getMimicOS_VM() : Sim()->getMimicOS();
    if (!os) return false;
    
    // The allocator is from MimicOS - check if it's a Utopia type
    // For Utopia allocators, we need to call getLastAllocatedInRestSeg()
    // This requires the method to be virtual in the base class or we use RTTI
    
    // For now, use a workaround: store state in the handler from allocation result
    // The proper solution is to add a virtual method to PhysicalMemoryAllocator
    
    // Use dynamic_cast with the full policy type
    // Utopia = UtopiaAllocator<Sniper::Utopia::MetricsPolicy>
    auto* utopia_alloc = dynamic_cast<Utopia*>(alloc);
    if (utopia_alloc) {
        return utopia_alloc->getLastAllocatedInRestSeg();
    }
    
    // Not a Utopia allocator, default to FlexSeg behavior
    return false;
}

void UtopiaExceptionHandler::allocate_page_table_frames(FaultCtx &ctx,
                                                         UInt64 address, UInt64 core_id, UInt64 ppn, 
                                                         int page_size, int num_requested_frames)
{
#if DEBUG_UTOPIA >= DEBUG_DETAILED
    log_file << "[UTOPIA_EXCEPTION_HANDLER] Allocating " << num_requested_frames << " page table frames" << std::endl;
    sim_log.debug("  │    Allocating %d page table frames...", num_requested_frames);
#endif

    int page_table_frames = num_requested_frames;
    std::vector<UInt64> &frames = ctx.alloc_out.prealloc_frames;

    for (int i = 0; i < page_table_frames; i++)
    {
        UInt64 frame = this->getAllocator()->handle_page_table_allocations(FOUR_KIB);

#if DEBUG_UTOPIA >= DEBUG_DETAILED
        log_file << "[UTOPIA_EXCEPTION_HANDLER] Allocated PT frame: 0x" << std::hex << frame << std::dec << std::endl;
#endif

        if (frame == static_cast<UInt64>(-1))
        {
            std::cerr << "[FATAL] [UTOPIA_EXCEPTION_HANDLER] Out of memory allocating page table frames" << std::endl;
            assert(false);
        }

        frames.push_back(frame);
    }

    int frames_used = update_page_table_frames(address, core_id, ppn, page_size, frames);

#if DEBUG_UTOPIA >= DEBUG_DETAILED
    log_file << "[UTOPIA_EXCEPTION_HANDLER] PT frames used: " << frames_used 
             << " / " << page_table_frames << std::endl;
    sim_log.debug("  │    PT frames used: %d/%d", frames_used, page_table_frames);
#endif

    // Deallocate unused frames
    for (int i = 0; i < (page_table_frames - frames_used); i++)
    {
        this->getAllocator()->handle_page_table_deallocations(FOUR_KIB);
    }

    return;
}

int UtopiaExceptionHandler::update_page_table_frames(UInt64 address, UInt64 core_id, UInt64 ppn, 
                                                      int page_size, std::vector<UInt64> &frames)
{
#if DEBUG_UTOPIA >= DEBUG_DETAILED
    log_file << "[UTOPIA_EXCEPTION_HANDLER] Updating PT for address: 0x" << std::hex << address 
             << " → PPN: 0x" << ppn << std::dec << " (size: " << page_size << ")" << std::endl;
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

#if DEBUG_UTOPIA >= DEBUG_DETAILED
    log_file << "[UTOPIA_EXCEPTION_HANDLER] Updating PT for app: " << app_id_faulter << std::endl;
#endif

    int frames_used = os->getPageTable(app_id_faulter)->updatePageTableFrames(address, core_id, ppn, page_size, frames);

#if DEBUG_UTOPIA >= DEBUG_DETAILED
    log_file << "[UTOPIA_EXCEPTION_HANDLER] PT update complete, frames_used: " << frames_used << std::endl;
#endif

    return frames_used;
}

PhysicalMemoryAllocator* UtopiaExceptionHandler::getAllocator() 
{
    if (m_allocator == NULL) {
        std::cout << "[UTOPIA_EXCEPTION_HANDLER] Getting allocator from MimicOS" << std::endl;
        if (is_guest) {
            m_allocator = Sim()->getMimicOS_VM()->getMemoryAllocator();
        } else {
            m_allocator = Sim()->getMimicOS()->getMemoryAllocator();
        }
        
        // Verify it's a Utopia allocator
        // Utopia = UtopiaAllocator<Sniper::Utopia::MetricsPolicy>
        auto* utopia_alloc = dynamic_cast<Utopia*>(m_allocator);
        if (utopia_alloc) {
            std::cout << "[UTOPIA_EXCEPTION_HANDLER] Confirmed Utopia allocator" << std::endl;
        } else {
            std::cout << "[UTOPIA_EXCEPTION_HANDLER] Warning: Not a Utopia allocator, RestSeg optimization disabled" << std::endl;
        }
    }

    return m_allocator;
}
