// Create a factory class for page fault handlers
# pragma once
# include "page_fault_handler.h"
# include "utopia_page_fault_handler.h"
# include "eager_paging_fault_handler.h"
# include "physical_memory_allocator.h"
# include "page_fault_handler_base.h"
# include <string>


class HandlerFactory {

    public:
        // Create a page fault handler
        static PageFaultHandlerBase* createHandler(String handlerType, PhysicalMemoryAllocator *allocator, String name, bool is_guest) {

            if (handlerType == "default") {
                return new PageFaultHandler(allocator, name, is_guest);
            } else if (handlerType == "utopia") {
                return new UtopiaPageFaultHandler(allocator);
            } else if (handlerType == "eager_paging") {
                return new EagerPagingFaultHandler(allocator);
            }
              else {
                return NULL;
            }
        }
};
