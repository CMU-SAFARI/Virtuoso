// TODO @vlnitu: REMOVE STALE FILE

// Create a factory class for page fault handlers
// # pragma once
// # include "page_fault_handler.h"
// # include "eager_paging_fault_handler.h"
// # include "page_fault_handler_base.h"
// # include <string>

// #include "memory_management/physical_memory_allocators/physical_memory_allocator.h"

// class HandlerFactory {

//     public:
//         // Create a page fault handler
//         statie PageFaultHandlerBase* createHandler(String handlerType, PhysicalMemoryAllocator *allocator, String name, bool is_guest) {
//             return NULL;
//             // TODO @vlnitu: remove createHandler() function, as well as all data members of PageFaultHandlerBase 

//             // if (handlerType == "default") {
//             //     return new PageFaultHandler(allocator, name, is_guest);
//             // }
//             // // } else if (handlerType == "utopia") {
//             // //     return new UtopiaPageFaultHandler(allocator);
//             // // } 
//             // else if (handlerType == "eager_paging") {
//             //     return new EagerPagingFaultHandler(allocator);
//             // }
//             // // else if (handlerType == "spot") {
//             // //     return new SpotPageFaultHandler(allocator, name, is_guest);
//             // // }
//             // else {
//             //     return NULL;
//             // }
//         }
// };
