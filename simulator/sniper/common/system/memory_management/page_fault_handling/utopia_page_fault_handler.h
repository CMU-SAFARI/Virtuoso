
#pragma once
#include "physical_memory_allocator.h"
#include "page_fault_handler_base.h"
#include <iostream>
#include <fstream>
using namespace std;

class UtopiaPageFaultHandler : public PageFaultHandlerBase
{
    private:
        std::ofstream log_file;
        std::string log_file_name;
    public:
        UtopiaPageFaultHandler(PhysicalMemoryAllocator *allocator);
        ~UtopiaPageFaultHandler();

        void allocatePagetableFrames(UInt64 address, UInt64 core_id, UInt64 ppn, int page_size, int frame_number);
        void handlePageFault(UInt64 address, UInt64 core_id, int frames);

};