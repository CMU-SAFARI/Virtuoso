

#include <cassert>


#include "eager_paging_fault_handler.h"
#include "physical_memory_allocator.h"
#include "simulator.h"
#include "thread.h"
#include "core_manager.h"
#include "mimicos.h"
#include "instruction.h"

//#define DEBUG

EagerPagingFaultHandler::EagerPagingFaultHandler(PhysicalMemoryAllocator *allocator): PageFaultHandlerBase(allocator)
{  
    log_file_name = "eaer_paging_fault_handler.log";
    log_file_name = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + log_file_name;
    log_file.open(log_file_name);

}

EagerPagingFaultHandler::~EagerPagingFaultHandler()
{
}

void EagerPagingFaultHandler::allocatePagetableFrames(UInt64 address, UInt64 app_id, UInt64 ppn, int page_size, int frame_number)
{
    // First lets ask the page table if new frames are needed
    #ifdef DEBUG
        log_file << "[EAGER_PF_HANDLER] Allocating page table frames: " << frame_number << std::endl;
    #endif
    int page_table_frames = frame_number;


    std::vector<UInt64> frames;
    for (int i = 0; i < page_table_frames; i++)
    {
    
        UInt64 frame = allocator->handle_page_table_allocations(4096);
        #ifdef DEBUG
            log_file << "[EAGER_PF_HANDLER] Giving away page table frame: " << frame << std::endl;
        #endif
        if (frame == static_cast<UInt64>(-1))
        {
            // We are out of memory
            assert (false);

        }
        
        frames.push_back(frame);
    }


    int frames_used = Sim()->getMimicOS()->getPageTable(app_id)->updatePageTableFrames(address, app_id, ppn, page_size, frames);

    #ifdef DEBUG
        log_file << "[EAGER_PF_HANDLER] Page table frames used: " << frames_used << std::endl;
    #endif

    for (int i = 0; i < (page_table_frames - frames_used); i++)
    {
        allocator->handle_page_table_deallocations(4096);
    }
    // Now lets update the page table with the new frames



    return;

} 





void EagerPagingFaultHandler::handlePageFault(UInt64 address, UInt64 app_id, int frames)
{
    // Now lets try to allocate the page
    // The allocator will return a pair with the address and the size of the page
#ifdef DEBUG
    log_file << "[EAGER_PF_HANDLER] Handling page fault for address: " << address << " for app: " << app_id << std::endl;
    log_file << "[EAGER_PF_HANDLER] We need to allocate: " << frames << " frames" << std::endl;
#endif

    // Find the VMA that contains the address

    std::vector<VMA> vma_list = Sim()->getMimicOS()->getVMA(app_id);
    VMA final_vma(0,0);
    for (UInt32 i = 0; i < vma_list.size(); i++)
    {
        if (address >= vma_list[i].getBase() && address < vma_list[i].getEnd())
        {
#ifdef DEBUG_MMU
            log_file << "VMA found for address: " << address << " in VMA: " << vma_list[i].getBase() << " - " << vma_list[i].getEnd() << std::endl;
#endif  
            final_vma = vma_list[i];
            break;
        }
    }

    std::vector<Range> ranges = allocator->allocate_ranges(final_vma.getBase(), final_vma.getEnd(), app_id);
#ifdef DEBUG
    log_file << "[EAGER_PF_HANDLER] Ranges allocated: " << ranges.size() << std::endl;
#endif

    IntPtr current_vma_address = final_vma.getBase();

#ifdef DEBUG
    log_file << "[EAGER_PF_HANDLER] Allocating page table frames for VMA: " << final_vma.getBase() << " - " << final_vma.getEnd() << std::endl;
#endif

    for (UInt32 i = 0; i < ranges.size(); i++)
    {
        #ifdef DEBUG
            log_file << "[EAGER_PF_HANDLER] Range allocated: " << ranges[i].vpn << " - " << ranges[i].bounds << " - " << ranges[i].offset << std::endl;
        #endif

        int pages = (ranges[i].bounds);
#ifdef DEBUG
        log_file << "[EAGER_PF_HANDLER] Pages to allocate: " << pages << std::endl;
#endif
        IntPtr current_vpn = ranges[i].vpn;

#ifdef DEBUG
        log_file << "[EAGER_PF_HANDLER] Allocating page table frames from current vpn: " << current_vpn << " to " << current_vpn + pages << std::endl;
#endif
        for (int j = 0; j < pages; j++)
        {
#ifdef DEBUG
        log_file << "[EAGER_PF_HANDLER] Allocating page table frames for vpn: " << current_vpn + j << std::endl;
        log_file << "[EAGER_PF_HANDLER] Updating the ppn to: " << ranges[i].offset + j << std::endl;
#endif
            IntPtr current_address = (current_vpn+j)*4096;
            // Update the page table with the new range -> we need to insert all the pages of the range inside the page table

            allocatePagetableFrames(current_address, app_id, ranges[i].offset + j, 12, frames);
        }

        final_vma.addPhysicalRange(ranges[i]);

        ParametricDramDirectoryMSI::RangeEntry entry;
        entry.offset = ranges[i].offset; 
#ifdef DEBUG
            log_file << "[EAGER_PF_HANDLER] Inserting range into range table: " << ranges[i].vpn << " - " << ranges[i].bounds << " - " << ranges[i].offset << std::endl;
            log_file << "[EAGER_PF_HANDLER] Inserting (address) range into range table: " << current_vma_address << " - " << current_vma_address+4096*ranges[i].bounds << " - " << ranges[i].offset << std::endl;
#endif
        Sim()->getMimicOS()->getRangeTable(app_id)->insert(std::make_pair(current_vma_address, current_vma_address+4096*ranges[i].bounds), entry);

        current_vma_address += 4096*ranges[i].bounds;

    }
    final_vma.setAllocated(true);

    
    //Next lets try to allocate the page table frames
    // This function will return if no frames are needed
    //allocatePagetableFrames(address, app_id, allocation_result.first, page_size, frames);

    // If the page is allocated, return
    return;
}