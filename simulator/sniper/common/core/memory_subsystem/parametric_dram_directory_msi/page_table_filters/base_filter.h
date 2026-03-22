

#pragma once
#include "pagetable.h"
#include "core.h"

namespace ParametricDramDirectoryMSI
{
    class BaseFilter {
        
        public:

            // Returns true if the filter accepts the given address
            virtual PTWResult filterPTWResult(IntPtr virtual_address, PTWResult ptw_result, PageTable *page_table, bool count) = 0;

            /**
             * @brief Look up a single address in the page walk cache (PWC)
             * @param address Physical address to look up
             * @param now Current time
             * @param level PWC level (internal node level or leaf level index)
             * @param count Whether to update statistics
             * @return true if hit in PWC, false if miss
             * 
             * Used by RadixWay lookup to reuse PTW caching infrastructure.
             * Default implementation returns false (no caching).
             */
            virtual bool lookupPWC(IntPtr address, SubsecondTime now, int level, bool count) {
                (void)address; (void)now; (void)level; (void)count;
                return false;  // Default: no caching
            }

            BaseFilter(String _name, Core* _core):
                name(_name), core(_core) {}
            
            ~BaseFilter() = default;

        private:
            String name;
            Core* core;
    };
};