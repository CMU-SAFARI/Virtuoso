// #include "spot_page_fault_handler.h"
// #include "physical_memory_allocator.h"
// #include "simulator.h"
// #include "thread.h"
// #include "core_manager.h"
// #include "mimicos.h"
// #include "instruction.h"
// #include <cassert>

// #undef DEBUG

// SpotPageFaultHandler::SpotPageFaultHandler(PhysicalMemoryAllocator *allocator, String name_, bool is_guest_): PageFaultHandlerBase(allocator)
// {  
//     this->allocator = allocator;
//     is_guest = is_guest_;
//     name = name_;
    
//     log_file_name = std::string(name.c_str()) + ".page_fault_handler.log";
//     log_file_name = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + log_file_name;
//     log_file.open(log_file_name);

// }

// SpotPageFaultHandler::~SpotPageFaultHandler()
// {
// }

// void SpotPageFaultHandler::allocatePagetableFrames(UInt64 address, UInt64 core_id, UInt64 ppn, int page_size, int frame_number)
// {

//     // First lets ask the page table if new frames are needed
//     #ifdef DEBUG
//         log_file << "[PF_HANDLER] Allocating page table frames: " << frame_number << std::endl;
//     #endif

//     int page_table_frames = frame_number;

//     std::vector<UInt64> frames;


//     for (int i = 0; i < page_table_frames; i++)
//     {
//         UInt64 frame = allocator->handle_page_table_allocations(4096);
//         #ifdef DEBUG
//             log_file << "[PF_HANDLER] Giving away page table frame: " << frame << std::endl;
//         #endif
//         if (frame == static_cast<UInt64>(-1))
//         {
//             // We are out of memory
//             assert (false);


//         }
//         frames.push_back(frame);
//     }
    

//     Core* core_faulter = Sim()->getCoreManager()->getCoreFromID(core_id);
//     Thread* thread_faulter = core_faulter->getThread();

//     int app_id_faulter = thread_faulter->getAppId();

//     MimicOS* os;
//     if (is_guest) {
//         os = Sim()->getMimicOS_VM();
//     }
//     else {
//         os = Sim()->getMimicOS();
//     }

//     int frames_used = os->getPageTable(app_id_faulter)->updatePageTableFrames(address, core_id, ppn, page_size, frames);

//     #ifdef DEBUG
//         log_file << "[PF_HANDLER] Page table frames used: " << frames_used << std::endl;
//     #endif

   

//     for (int i = 0; i < (page_table_frames - frames_used); i++)
//     {
//         allocator->handle_page_table_deallocations(4096);
//     }


//     return;


// } 



// void SpotPageFaultHandler::handlePageFault(UInt64 address, UInt64 core_id, int frames)
// {

//     // Now lets try to allocate the page
//     // The allocator will return a pair with the address and the size of the page
// #ifdef DEBUG
//     log_file << "[SPOT_PF_HANDLER] Handling page fault for address: " << address << " for core: " << core_id << std::endl;
//     log_file << "[SPOT_PF_HANDLER] We need to allocate: " << frames << " frames" << std::endl;
// #endif

//     std::pair<UInt64, UInt64> allocation_result = allocator->allocate(4096, address, core_id, false);
// #ifdef DEBUG
//     auto ppn = allocation_result.first;
//     assert(ppn != static_cast<UInt64>(-1)); // Ensure that the PPN is valid; otherwise: Buddy Allocator is broken
// #endif

//     //Next lets try to allocate the page table frames
//     // This function will return if no frames are needed
//     int page_size = allocation_result.second;
//     allocatePagetableFrames(address, core_id, allocation_result.first, page_size, frames);

//     // If the page is allocated, return
//     return;
// }