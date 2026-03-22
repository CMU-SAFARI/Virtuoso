
#pragma once
#include "cwc.h"
#include "base_filter.h"
#include "core.h"
#include "pagetable_cuckoo.h"


namespace ParametricDramDirectoryMSI
{
    class CuckooFilter: public BaseFilter
    {

        public:
            CuckooFilter(String name, Core* core); 
            ~CuckooFilter();

            PTWResult filterPTWResult(IntPtr virtual_address, PTWResult ptw_result, PageTable *page_table, bool count);

        private:
            CWCache *cwc;
            std::ofstream log_file;
            std::string log_file_name;


    };
};