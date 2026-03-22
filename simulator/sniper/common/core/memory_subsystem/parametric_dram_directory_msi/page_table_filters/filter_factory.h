
#pragma once
#include "cuckoo_filter.h"
#include "radix_filter.h"
#include "base_filter.h"
#include "default_filter.h"


namespace ParametricDramDirectoryMSI
{
    class FilterPTWFactory
    {
        public:

            static BaseFilter *createFilterPTWBase(String type, String mmu_name, Core* core)
            {
                if (type == "pwc") // Page Walk Caches for Radix
                {
                    return new RadixFilter(mmu_name, core);
                }

                else if(type == "cwc") //  Cuckoo Walk Caches for ECH
                {
                    return new CuckooFilter(mmu_name, core);
                }

                else if(type == "default") // Default filter - nothing gets filtered
                {
                    return new DefaultFilter(mmu_name, core);
                }
                else
                {
                    std::cerr << "Error: Invalid filter type for PTW: " << type << std::endl;
                    exit(EXIT_FAILURE);
                }

            };

    };

};
