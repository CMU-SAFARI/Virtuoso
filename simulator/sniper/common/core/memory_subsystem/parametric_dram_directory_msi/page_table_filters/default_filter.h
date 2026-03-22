
#pragma once
#include "cwc.h"
#include "base_filter.h"
#include "core.h"


namespace ParametricDramDirectoryMSI
{
    class DefaultFilter: public BaseFilter
    {

        public:
            DefaultFilter(String name, Core* core); 
            ~DefaultFilter();

            PTWResult filterPTWResult(IntPtr virtual_address, PTWResult ptw_result, PageTable *page_table, bool count);


    };
};