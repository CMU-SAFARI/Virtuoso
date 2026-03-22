
#include "default_filter.h"
#include "cwc.h"
#include "config.hpp"
#include "simulator.h"
#include "core.h"
#include "pagetable.h"
#include <iostream>
#include <cmath>


namespace ParametricDramDirectoryMSI
{
    DefaultFilter::DefaultFilter(String _name, Core *_core):
                                BaseFilter(_name, _core)
        
    {

    }

    DefaultFilter::~DefaultFilter()
    {

    }


    PTWResult DefaultFilter::filterPTWResult(IntPtr virtual_address, PTWResult ptw_result, PageTable *page_table, bool count) 
    {
        return ptw_result;
    }
};