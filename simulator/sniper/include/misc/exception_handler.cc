// TODO @vlnitu: REMOVE STALE FILE 

// #include "exception_handler.h"
// #include "log.h"
// #include <fstream>
// #include "simulator.h"
// #include "core.h"
// #include "thread.h"
// #include "mimicos.h"
// #include "memory_manager_base.h"

// #include "debug_config.h"

// #define BASE_PAGE_SHIFT 12UL

// void ExceptionHandler::handle_exception(int exception_type_code, int argc, uint64_t *argv) {
// }

// void ExceptionHandler::mapFrames(ParametricDramDirectoryMSI::PageTable* page_table, UInt64 address, UInt64 core_id, UInt64 ppn, int page_size,
//                                                const std::vector<UInt64>& frames_already_allocated_by_virtuos)
// {
//     // First lets ask the page table if new frames are needed
// #if DEBUG_EXCEPTION_HANDLER >= DEBUG_DETAILED
//         log_file << "[EXCEPTION_HANDLER] Mapping " << frames_already_allocated_by_virtuos.size() << " frames - updating Page Table structure..." << std::endl;
// #endif

//     Core* core_faulter = Sim()->getCoreManager()->getCoreFromID(core_id);
//     Thread* thread_faulter = core_faulter->getThread();

//     int frames_used = page_table->updatePageTableFrames(address, core_id, ppn, page_size, frames_already_allocated_by_virtuos);
// #if DEBUG_EXCEPTION_HANDLER >= DEBUG_DETAILED
//         log_file << "[EXCEPTION_HANDLER] Frames used: " << frames_used << std::endl;
// #endif

//     // TODO @vlnitu: migrate deallocation allocators as well
//     int frames_requested = frames_already_allocated_by_virtuos.size();
// #if DEBUG_EXCEPTION_HANDLER >= DEBUG_DETAILED
//     log_file << "[EXCEPTION_HANDLER] Frames requested: " <<  frames_requested << std::endl;
//     log_file << "[EXCEPTION_HANDLER] Frames used: " << frames_used << std::endl;   
//     log_file << "[EXCEPTION_HANDLER] Leaking !! page table frames: " << (frames_requested - frames_used) << std::endl;
// #endif

//     // ----------- !! IMPORTANT !! -----------------
//     // TODO @vlnitu: design a communication protocol in which we pass back the frames we've not used, so that VirtuOS can deallocate these frames
//     // @vlnitu's idea: 
//     //      >> keep a pool of unused frames here that can be reused / deallocated later by VirtuOS, and in the future page fault request, pass these frames back to VirtuOS
//     //      >> so that it can reuse them, w/o reallocating again

//     // for (int i = 0; i < (frames_requested - frames_used); i++)
//     // {
//     //     allocator->handle_page_table_deallocations(4096);
//     // }

//     return;


// } 

// void ExceptionHandler::handle_page_fault(ParametricDramDirectoryMSI::PageTable* page_table, IntPtr vpn, IntPtr ppn, int page_size,
//                                          const std::vector<UInt64>& frames_already_allocated_by_virtuos)
// {
// #if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
//     std::cout << "[EXCEPTION_HANDLER] Handling page fault for page_table = " << page_table <<
//                  " - mapping vpn = " << vpn <<
//                  " to ppn = "        << ppn  << std::endl;
// #endif
    
//     // TODO: @vlnitu add Swapping - migrate @kanellok's implementation from page_fault_handler.h [STALE]

//     int core_id = m_core->getId();
//     // TODO @vlnitu: use Policy-design pattern to handle page faults, depending whether we handle it user-space or sniper-space MimicOS
//     // TODO @vlnitu: Policy::updatePageTable
//     bool userspace_mimicos_enabled = Sim()->getCoreManager()->getCoreFromID(core_id)->getMemoryManager()->getIsUserspaceMimicosEnabled();
//     if (userspace_mimicos_enabled) {
//         this->mapFrames(page_table, vpn << BASE_PAGE_SHIFT, core_id, ppn, page_size, frames_already_allocated_by_virtuos); // TODO @vlnitu: use policy
//     }
//     else {
//         // this->allocateAndMapFrames(page_table, vpn << BASE_PAGE_SHIFT, core_id); // TODO @vlnitu: use policy
//         // Call old 
//     }

// #if DEBUG_EXCEPTION_HANDLER >= DEBUG_BASIC
//     std::cout << "[EXCEPTION_HANDLER] Installed " << frames_already_allocated_by_virtuos.size() << " frames (that were already alloc'd by VirtuOS), as a result of handling the PF" << std::endl;
//     std::cout << "[EXCEPTION_HANDLER] vpn = 0x" << std::hex << vpn << std::dec << std::endl;
//     std::cout << "[EXCEPTION_HANDLER] accesses_per_vpn[vpn] = " << page_table->getAccessesPerVPN(vpn)  << std::endl;
// #endif

//     return;
// }
