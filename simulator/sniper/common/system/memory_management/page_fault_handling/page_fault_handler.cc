// #include "page_fault_handler.h"
// #include "physical_memory_allocator.h"
// #include "simulator.h"
// #include "thread.h"
// #include "core_manager.h"
// #include "mimicos.h"
// #include "instruction.h"
// #include <cassert>

// #undef DEBUG

// PageFaultHandler::PageFaultHandler(PhysicalMemoryAllocator *allocator, String name_, bool is_guest_): PageFaultHandlerBase(allocator)
// {  
//     this->allocator = allocator;
//     is_guest = is_guest_;
//     name = name_;
    
//     log_file_name = std::string(name.c_str()) + ".page_fault_handler.log";
//     log_file_name = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + log_file_name;
//     log_file.open(log_file_name);

// }

// PageFaultHandler::~PageFaultHandler()
// {
// }

// void PageFaultHandler::allocatePagetableFrames(UInt64 address, UInt64 core_id, UInt64 ppn, int page_size, int frame_number)
// {
//     // First lets ask the page table if new frames are needed
//     #ifdef DEBUG
//         log_file << "[PF_HANDLER] Allocating page table frames: " << frame_number << std::endl;
//     #endif

//     int page_table_frames = frame_number;



//     std::vector<UInt64> frames;

//     if(allocator->getName() == "hash_based" || allocator->getName() == "hash_based_open" || allocator->getName() == "hash_based_thp")
//     {
//         // Cast allocator to hash-based allocator
//         // We are using hash-based allocator, so we need to allocate page table frames
//         for (int i = 0; i < page_table_frames; i++)
//         {
//             auto result = allocator->allocate(4096, address, core_id, true);

//             IntPtr frame = result.first;    
//             #ifdef DEBUG
//                 log_file << "[PF_HANDLER] Giving away page table frame: " << frame << std::endl;
//             #endif

//             if (frame == static_cast<UInt64>(-1))
//             {
//                 // We are out of memory
//                 assert (false);
//             }
            
//             frames.push_back(frame);
//         }

//         //Reverse the frames if we are using hash-based allocator
//         // This is because the hash-based allocator allocates the frames in reverse order
//         std::reverse(frames.begin(), frames.end());

//         #ifdef DEBUG
//             log_file << "[PF_HANDLER] Allocated page table frames using hash-based allocator: " << page_table_frames << std::endl;
//         #endif

//     }
//     else if (allocator->getName()== "asap_allocator")
//     {
//         // We are using asap, so we need contiguous and ordered allocation of the page table frames
//         // We will allocate the frames in a contiguous manner (we have reserved a large portion of the kernel memory for this purpose to ease simulation)

//         #ifdef DEBUG
//             log_file << "[PF_HANDLER] Allocating page table frames using ASAP allocator: " << page_table_frames << std::endl;
//         #endif

//         int current_frame = page_table_frames; // Fix the 4 -> read from config file
//         int current_frame_reverse = 4 - page_table_frames; // Fix the 4 -> read from config file        

//         IntPtr current_frame_id = 0;
//         IntPtr offset = 0;
//         IntPtr previous_offset = 0;
//         int base = 0;
//         int old_base = 0;
//         int levels = 4;
//         int level = 4; // We have 4 levels of page tables

//         for (int i= 0; i < 4; i++)
//         {
// #ifdef DEBUG

//             log_file << "[PF_HANDLER] Level: " << level << " for address: " << address << std::endl;
//             log_file << "[PF_HANDLER] We need to shift the address by: " << (48 - 9 * (levels - level)) << " bits" << std::endl;
// #endif
//             offset = (address >> (48 - 9 * (levels - level))) & 0x1FF;

// #ifdef DEBUG
//             log_file << "[PF_HANDLER] Before mask:" << (address >> (48 - 9 * (levels - level))) << " After mask: " << offset << std::endl;
// #endif
    
//             base += (i>=1) ? pow(512, i-1) : 0; // We start from the base of the                                                                                                                                       
//             current_frame_id = (i>=1) ? ((base-1)+(current_frame_id-old_base)*512 +offset) : 0; // We start from the base of the first level
// #ifdef DEBUG
//             log_file << "[PF_HANDLER] Offset from previous level: " << offset << std::endl;
//             log_file << "[PF_HANDLER] Base: " << base << " for level: " << level << std::endl;
//             log_file << "[PF_HANDLER] Frame_id: " << current_frame_id << " for level: " << level << std::endl;
// #endif

//             old_base = base;

//             frames.push_back(current_frame_id);

//             level--;
//         }
//         // We will allocate the frames in a contiguous manner
//     }
//     else{

//         for (int i = 0; i < page_table_frames; i++)
//         {
//             UInt64 frame = allocator->handle_page_table_allocations(4096);
//             #ifdef DEBUG
//                 log_file << "[PF_HANDLER] Giving away page table frame: " << frame << std::endl;
//             #endif
//             if (frame == static_cast<UInt64>(-1))
//             {
//                 // We are out of memory
//                 assert (false);


//             }
//             frames.push_back(frame);
//         }
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

//     if(allocator->getName() == "hash_based" || allocator->getName() == "hash_based_open" || allocator->getName() == "hash_based_thp")
//     {
// #ifdef DEBUG
//         log_file << "[PF_HANDLER] Deallocating page table frames: " << page_table_frames << std::endl;
// #endif
//         for (int i = frames_used; i< page_table_frames; i++)
//         {
// #ifdef DEBUG
//             log_file << "[PF_HANDLER] Deallocating page table frame: " << frames[i] << std::endl;
// #endif
//             //upcast allocator to hash-based allocator
//              allocator->deallocate(frames[i], core_id);
//         }
//     }
//     else if (allocator->getName() == "asap_allocator")
//     {
//         // We are using asap, so we do not need to deallocate the page table frames
// #ifdef DEBUG
//         log_file << "[PF_HANDLER] Deallocating page table frames using ASAP allocator: " << page_table_frames - frames_used << std::endl;
// #endif

//     }
//     else{        
// #ifdef DEBUG
//         log_file << "[PF_HANDLER] Frames requested: " << page_table_frames << std::endl;
//         log_file << "[PF_HANDLER] Frames used: " << frames_used << std::endl;   
//         log_file << "[PF_HANDLER] Deallocating page table frames: " << (page_table_frames - frames_used) << std::endl;
// #endif
//         for (int i = 0; i < (page_table_frames - frames_used); i++)
//         {
//             allocator->handle_page_table_deallocations(4096);
//         }
//     }

//     return;


// } 

// bool PageFaultHandler::handleSwapping(UInt64 address, UInt64 app_id)
// {

//         auto swap_cache = Sim()->getMimicOS()->getSwapCache();

//         //Check if the page is in the swap space
//         bool in_swap  = swap_cache->lookup(address, app_id);

//         if (in_swap)
//         {   
//             IntPtr vpn = address >> 12; // Assuming 4KB pages, shift right by 12 bits to get the VPN
//             swap_cache->swapIn(vpn,app_id);
//             #ifdef DEBUG
//                 log_file << "[PF_HANDLER] Page " << address << " found in swap space, swapped in." << std::endl;
//             #endif
//             return true; // Page was found in swap space and swapped in
//         }
//         return false; // Page was not found in swap space

//             // If the page is not in the swap space, we need to swap out a page
//             // Now we have the page in the physical memory, we can return
    
    
// }

// void PageFaultHandler::handlePageFault(UInt64 address, UInt64 core_id, int frames)
// {

//     Core* core_faulter = Sim()->getCoreManager()->getCoreFromID(core_id);
//     Thread* thread_faulter = core_faulter->getThread();
//     int app_id = thread_faulter->getAppId();

//     bool is_swapped = false;
//     if(Sim()->getMimicOS()->isSwapEnabled()){
//         // If the swap is enabled, we need to check if the page is in the swap space
//        is_swapped = handleSwapping(address, core_id);
//     }

//     if(is_swapped) 
//         Sim()->getMimicOS()->setLastPageFaultCausedSwapping(is_swapped);
//     // Now lets try to allocate the page
//     // The allocator will return a pair with the address and the size of the page
// #ifdef DEBUG
//     log_file << "[PF_HANDLER] Handling page fault for address: " << address << " for core: " << core_id << std::endl;
//     log_file << "[PF_HANDLER] We need to allocate: " << frames << " frames" << std::endl;
// #endif

//     std::pair<UInt64, UInt64> allocation_result = allocator->allocate(4096, address, app_id, false);

//     //Next lets try to allocate the page table frames
//     // This function will return if no frames are needed
//     int page_size = allocation_result.second;
//     allocatePagetableFrames(address, core_id, allocation_result.first, page_size, frames);

//     // If the page is allocated, return
//     return;
// }
