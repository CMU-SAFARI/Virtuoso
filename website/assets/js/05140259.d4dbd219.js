"use strict";(self.webpackChunkvirtuoso=self.webpackChunkvirtuoso||[]).push([[965],{3538:(e,n,s)=>{s.r(n),s.d(n,{assets:()=>t,contentTitle:()=>a,default:()=>h,frontMatter:()=>o,metadata:()=>r,toc:()=>c});const r=JSON.parse('{"id":"Physical Memory Allocators/reserve_thp","title":"Reservation-based Transparent Huge Pages","description":"This document provides an overview and explanation of the ReservationTHPAllocator class, a specialized physical memory allocator that reserves and promotes Transparent Huge Pages (THPs). The code is designed to integrate with a buddy allocator (provided through the Buddy class) to manage physical memory. The ultimate goal is to optimize memory allocations by utilizing 2MB pages wherever possible while allowing fallback to 4KB pages when needed.","source":"@site/docs/Physical Memory Allocators/reserve_thp.md","sourceDirName":"Physical Memory Allocators","slug":"/Physical Memory Allocators/reserve_thp","permalink":"/Virtuoso/docs/Physical Memory Allocators/reserve_thp","draft":false,"unlisted":false,"editUrl":"https://github.com/facebook/docusaurus/tree/main/packages/create-docusaurus/templates/shared/docs/Physical Memory Allocators/reserve_thp.md","tags":[],"version":"current","sidebarPosition":2,"frontMatter":{"sidebar_position":2},"sidebar":"tutorialSidebar","previous":{"title":"Physical Memory Allocators","permalink":"/Virtuoso/docs/category/physical-memory-allocators"},"next":{"title":"Translation Lookaside Buffer (TLB) Overview","permalink":"/Virtuoso/docs/TLB Subsystem/tlb"}}');var l=s(4848),i=s(8453);const o={sidebar_position:2},a="Reservation-based Transparent Huge Pages",t={},c=[{value:"Table of Contents",id:"table-of-contents",level:2},{value:"Introduction",id:"introduction",level:2},{value:"High-Level Design",id:"high-level-design",level:2},{value:"Class Overview",id:"class-overview",level:2},{value:"Constructor",id:"constructor",level:3},{value:"Destructor",id:"destructor",level:3},{value:"Key Methods",id:"key-methods",level:2},{value:"demote_page()",id:"demote_page",level:3},{value:"checkFor2MBAllocation()",id:"checkfor2mballocation",level:3},{value:"allocate()",id:"allocate",level:3},{value:"givePageFast()",id:"givepagefast",level:3},{value:"deallocate()",id:"deallocate",level:3},{value:"allocate_ranges()",id:"allocate_ranges",level:3},{value:"getFreePages()",id:"getfreepages",level:3},{value:"getTotalPages()",id:"gettotalpages",level:3},{value:"getLargePageRatio()",id:"getlargepageratio",level:3},{value:"getAverageSizeRatio()",id:"getaveragesizeratio",level:3},{value:"fragment_memory()",id:"fragment_memory",level:3},{value:"Logging and Debugging",id:"logging-and-debugging",level:2},{value:"Dependencies",id:"dependencies",level:2},{value:"How to Use",id:"how-to-use",level:2},{value:"Example Usage",id:"example-usage",level:2}];function d(e){const n={a:"a",code:"code",h1:"h1",h2:"h2",h3:"h3",header:"header",hr:"hr",li:"li",ol:"ol",p:"p",pre:"pre",strong:"strong",ul:"ul",...(0,i.R)(),...e.components};return(0,l.jsxs)(l.Fragment,{children:[(0,l.jsx)(n.header,{children:(0,l.jsx)(n.h1,{id:"reservation-based-transparent-huge-pages",children:"Reservation-based Transparent Huge Pages"})}),"\n",(0,l.jsx)(n.h1,{id:"reservation-based-transparent-huge-page-thp-allocator",children:"Reservation-based Transparent Huge Page (THP) Allocator"}),"\n",(0,l.jsxs)(n.p,{children:["This document provides an overview and explanation of the ",(0,l.jsx)(n.strong,{children:"ReservationTHPAllocator"})," class, a specialized physical memory allocator that reserves and promotes ",(0,l.jsx)(n.strong,{children:"Transparent Huge Pages (THPs)"}),". The code is designed to integrate with a ",(0,l.jsx)(n.strong,{children:"buddy allocator"})," (provided through the ",(0,l.jsx)(n.code,{children:"Buddy"})," class) to manage physical memory. The ultimate goal is to optimize memory allocations by utilizing 2MB pages wherever possible while allowing fallback to 4KB pages when needed."]}),"\n",(0,l.jsx)(n.hr,{}),"\n",(0,l.jsx)(n.h2,{id:"table-of-contents",children:"Table of Contents"}),"\n",(0,l.jsxs)(n.ol,{children:["\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.a,{href:"#introduction",children:"Introduction"})}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.a,{href:"#high-level-design",children:"High-Level Design"})}),"\n",(0,l.jsxs)(n.li,{children:[(0,l.jsx)(n.a,{href:"#class-overview",children:"Class Overview"}),"\n",(0,l.jsxs)(n.ul,{children:["\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.a,{href:"#constructor",children:"Constructor"})}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.a,{href:"#destructor",children:"Destructor"})}),"\n"]}),"\n"]}),"\n",(0,l.jsxs)(n.li,{children:[(0,l.jsx)(n.a,{href:"#key-methods",children:"Key Methods"}),"\n",(0,l.jsxs)(n.ul,{children:["\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.a,{href:"#demote_page",children:"demote_page()"})}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.a,{href:"#checkfor2mballocation",children:"checkFor2MBAllocation()"})}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.a,{href:"#allocate",children:"allocate()"})}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.a,{href:"#givepagefast",children:"givePageFast()"})}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.a,{href:"#deallocate",children:"deallocate()"})}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.a,{href:"#allocate_ranges",children:"allocate_ranges()"})}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.a,{href:"#getfreepages",children:"getFreePages()"})}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.a,{href:"#gettotalpages",children:"getTotalPages()"})}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.a,{href:"#getlargepageratio",children:"getLargePageRatio()"})}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.a,{href:"#getaveragesizeratio",children:"getAverageSizeRatio()"})}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.a,{href:"#fragment_memory",children:"fragment_memory()"})}),"\n"]}),"\n"]}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.a,{href:"#logging-and-debugging",children:"Logging and Debugging"})}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.a,{href:"#dependencies",children:"Dependencies"})}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.a,{href:"#how-to-use",children:"How to Use"})}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.a,{href:"#example-usage",children:"Example Usage"})}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.a,{href:"#license",children:"License"})}),"\n"]}),"\n",(0,l.jsx)(n.hr,{}),"\n",(0,l.jsx)(n.h2,{id:"introduction",children:"Introduction"}),"\n",(0,l.jsxs)(n.p,{children:["Transparent Huge Pages (THPs) allow the system to manage memory in larger chunks (commonly ",(0,l.jsx)(n.strong,{children:"2MB"})," pages) rather than standard ",(0,l.jsx)(n.strong,{children:"4KB"})," pages. The ",(0,l.jsx)(n.code,{children:"ReservationTHPAllocator"})," uses a reservation-based approach to allocate and promote 2MB pages when specific criteria (e.g., utilization thresholds) are met. If reservations cannot be fulfilled, the allocator falls back to allocating standard 4KB pages from the underlying ",(0,l.jsx)(n.strong,{children:"buddy allocator"}),"."]}),"\n",(0,l.jsxs)(n.p,{children:[(0,l.jsx)(n.strong,{children:"Key benefits"})," of using THPs:"]}),"\n",(0,l.jsxs)(n.ol,{children:["\n",(0,l.jsxs)(n.li,{children:[(0,l.jsx)(n.strong,{children:"Reduced TLB Pressure"}),": Fewer TLB entries are required since each entry covers a larger memory range."]}),"\n",(0,l.jsxs)(n.li,{children:[(0,l.jsx)(n.strong,{children:"Improved Performance"}),": Larger pages can reduce page-table lookups and cache misses in certain workloads."]}),"\n",(0,l.jsxs)(n.li,{children:[(0,l.jsx)(n.strong,{children:"Memory Efficiency"}),": Depending on usage, THPs can reduce fragmentation. However, if pages are underutilized, demotion or fallback to 4KB pages prevents excessive waste."]}),"\n"]}),"\n",(0,l.jsx)(n.hr,{}),"\n",(0,l.jsx)(n.h2,{id:"high-level-design",children:"High-Level Design"}),"\n",(0,l.jsxs)(n.ol,{children:["\n",(0,l.jsxs)(n.li,{children:[(0,l.jsx)(n.strong,{children:"Check for 2MB Page"}),": When a request arrives for a specific ",(0,l.jsx)(n.strong,{children:"virtual address"})," (and that address is not for a page table), the allocator checks whether a 2MB region is already reserved in ",(0,l.jsx)(n.code,{children:"two_mb_map"}),"."]}),"\n",(0,l.jsxs)(n.li,{children:[(0,l.jsx)(n.strong,{children:"Reserve or Allocate 2MB Page"}),":","\n",(0,l.jsxs)(n.ul,{children:["\n",(0,l.jsx)(n.li,{children:"If the region does not exist, the allocator attempts to reserve a 2MB chunk from the buddy allocator."}),"\n",(0,l.jsxs)(n.li,{children:["If successful, it records metadata in ",(0,l.jsx)(n.code,{children:"two_mb_map"})," (e.g., the physical start address, a bitset to track used 4KB pages, and promotion status)."]}),"\n"]}),"\n"]}),"\n",(0,l.jsxs)(n.li,{children:[(0,l.jsx)(n.strong,{children:"Mark 4KB as Allocated"}),": Within the 2MB reservation, a ",(0,l.jsx)(n.strong,{children:"4KB offset"})," is marked as allocated in the bitset."]}),"\n",(0,l.jsxs)(n.li,{children:[(0,l.jsx)(n.strong,{children:"Promote if Threshold is Met"}),": If the ratio of used pages in the 2MB region exceeds ",(0,l.jsx)(n.code,{children:"threshold_for_promotion"}),", the entire 2MB region is promoted (flag set to ",(0,l.jsx)(n.code,{children:"true"}),")."]}),"\n",(0,l.jsxs)(n.li,{children:[(0,l.jsx)(n.strong,{children:"Fallback"}),": If a 2MB reservation cannot be satisfied, the allocator falls back to standard 4KB allocations from the buddy allocator."]}),"\n",(0,l.jsxs)(n.li,{children:[(0,l.jsx)(n.strong,{children:"Demotion"}),": When memory pressure is high or if an allocation fails, the allocator may ",(0,l.jsx)(n.strong,{children:"demote"})," the least-utilized 2MB region back into 4KB frames, freeing those pages in the buddy system."]}),"\n"]}),"\n",(0,l.jsx)(n.hr,{}),"\n",(0,l.jsx)(n.h2,{id:"class-overview",children:"Class Overview"}),"\n",(0,l.jsx)(n.h3,{id:"constructor",children:"Constructor"}),"\n",(0,l.jsx)(n.pre,{children:(0,l.jsx)(n.code,{className:"language-cpp",children:"ReservationTHPAllocator::ReservationTHPAllocator(\n     String name,\n     int memory_size,\n     int max_order,\n     int kernel_size,\n     String frag_type,\n     float _threshold_for_promotion\n) : PhysicalMemoryAllocator(name, memory_size, kernel_size),\n     threshold_for_promotion(_threshold_for_promotion)\n{\n     // ...\n}\n"})}),"\n",(0,l.jsx)(n.p,{children:(0,l.jsx)(n.strong,{children:"Parameters:"})}),"\n",(0,l.jsxs)(n.ul,{children:["\n",(0,l.jsxs)(n.li,{children:[(0,l.jsx)(n.code,{children:"name"}),": A string identifier for the allocator instance."]}),"\n",(0,l.jsxs)(n.li,{children:[(0,l.jsx)(n.code,{children:"memory_size"}),": Total size of the memory in pages (depends on how your system or simulation tracks memory)."]}),"\n",(0,l.jsxs)(n.li,{children:[(0,l.jsx)(n.code,{children:"max_order"}),": The maximum order of pages that the underlying buddy allocator can handle (often related to the largest block size)."]}),"\n",(0,l.jsxs)(n.li,{children:[(0,l.jsx)(n.code,{children:"kernel_size"}),": The size of the kernel or reserved portion of memory."]}),"\n",(0,l.jsxs)(n.li,{children:[(0,l.jsx)(n.code,{children:"frag_type"}),": Indicates a fragmentation type or strategy for the buddy allocator."]}),"\n",(0,l.jsxs)(n.li,{children:[(0,l.jsx)(n.code,{children:"_threshold_for_promotion"}),": A floating-point number specifying the fraction of the 2MB region that must be utilized before promotion occurs."]}),"\n"]}),"\n",(0,l.jsx)(n.p,{children:(0,l.jsx)(n.strong,{children:"Initialization:"})}),"\n",(0,l.jsxs)(n.ul,{children:["\n",(0,l.jsxs)(n.li,{children:["Opens ",(0,l.jsx)(n.code,{children:"reservation_thp.log"})," for logging."]}),"\n",(0,l.jsxs)(n.li,{children:["Instantiates internal statistics counters (e.g., ",(0,l.jsx)(n.code,{children:"stats.four_kb_allocated"}),", ",(0,l.jsx)(n.code,{children:"stats.two_mb_promoted"}),", etc.)."]}),"\n",(0,l.jsxs)(n.li,{children:["Initializes an internal instance of the buddy allocator: ",(0,l.jsx)(n.code,{children:"buddy_allocator"}),"."]}),"\n"]}),"\n",(0,l.jsx)(n.h3,{id:"destructor",children:"Destructor"}),"\n",(0,l.jsx)(n.pre,{children:(0,l.jsx)(n.code,{className:"language-cpp",children:"ReservationTHPAllocator::~ReservationTHPAllocator()\n{\n     delete buddy_allocator;\n}\n"})}),"\n",(0,l.jsxs)(n.ul,{children:["\n",(0,l.jsxs)(n.li,{children:["Deallocates the ",(0,l.jsx)(n.code,{children:"buddy_allocator"})," instance."]}),"\n",(0,l.jsx)(n.li,{children:"Ensures a graceful cleanup of dynamically allocated resources."}),"\n"]}),"\n",(0,l.jsx)(n.hr,{}),"\n",(0,l.jsx)(n.h2,{id:"key-methods",children:"Key Methods"}),"\n",(0,l.jsx)(n.h3,{id:"demote_page",children:"demote_page()"}),"\n",(0,l.jsx)(n.pre,{children:(0,l.jsx)(n.code,{className:"language-cpp",children:"bool ReservationTHPAllocator::demote_page();\n"})}),"\n",(0,l.jsxs)(n.p,{children:[(0,l.jsx)(n.strong,{children:"Purpose:"})," Frees a 2MB region (selected by lowest utilization) and returns those pages back to the buddy allocator as 4KB frames."]}),"\n",(0,l.jsx)(n.p,{children:(0,l.jsx)(n.strong,{children:"Process:"})}),"\n",(0,l.jsxs)(n.ul,{children:["\n",(0,l.jsxs)(n.li,{children:["Collect all non-promoted 2MB regions in ",(0,l.jsx)(n.code,{children:"two_mb_map"}),"."]}),"\n",(0,l.jsx)(n.li,{children:"Calculate the utilization of each region."}),"\n",(0,l.jsx)(n.li,{children:"Sort by utilization and pick the least-utilized region for demotion."}),"\n",(0,l.jsx)(n.li,{children:"For each unused 4KB page in that 2MB region, free it in the buddy allocator."}),"\n",(0,l.jsxs)(n.li,{children:["Erase the region from ",(0,l.jsx)(n.code,{children:"two_mb_map"}),"."]}),"\n",(0,l.jsxs)(n.li,{children:["Increment ",(0,l.jsx)(n.code,{children:"stats.two_mb_demoted"}),"."]}),"\n",(0,l.jsx)(n.li,{children:"Return true upon successful demotion; false if no suitable candidate exists."}),"\n"]}),"\n",(0,l.jsx)(n.h3,{id:"checkfor2mballocation",children:"checkFor2MBAllocation()"}),"\n",(0,l.jsx)(n.pre,{children:(0,l.jsx)(n.code,{className:"language-cpp",children:"std::pair<UInt64,bool> ReservationTHPAllocator::checkFor2MBAllocation(UInt64 address, UInt64 core_id);\n"})}),"\n",(0,l.jsxs)(n.p,{children:[(0,l.jsx)(n.strong,{children:"Purpose:"})," Checks if a 2MB page can be (or has already been) reserved for the given virtual address."]}),"\n",(0,l.jsxs)(n.p,{children:[(0,l.jsx)(n.strong,{children:"Returns:"})," A pair ",(0,l.jsx)(n.code,{children:"(physicalAddress, isPromoted)"}),"."]}),"\n",(0,l.jsxs)(n.ul,{children:["\n",(0,l.jsxs)(n.li,{children:[(0,l.jsx)(n.code,{children:"physicalAddress"}),": Either the corresponding 4KB physical address in the 2MB region or -1 if reservation fails."]}),"\n",(0,l.jsxs)(n.li,{children:[(0,l.jsx)(n.code,{children:"isPromoted"}),": A boolean indicating if the entire 2MB region was promoted as a result of allocation."]}),"\n"]}),"\n",(0,l.jsx)(n.p,{children:(0,l.jsx)(n.strong,{children:"Process:"})}),"\n",(0,l.jsxs)(n.ul,{children:["\n",(0,l.jsx)(n.li,{children:"Computes the 2MB region index from address by shifting right by 21 bits."}),"\n",(0,l.jsxs)(n.li,{children:["If the region does not exist in ",(0,l.jsx)(n.code,{children:"two_mb_map"}),", attempts to reserve a 2MB page via ",(0,l.jsx)(n.code,{children:"buddy_allocator->reserve_2mb_page(...)"}),"."]}),"\n",(0,l.jsxs)(n.li,{children:["If successful, adds an entry in ",(0,l.jsx)(n.code,{children:"two_mb_map"})," with a 512-bit bitset (each bit representing one 4KB page)."]}),"\n",(0,l.jsx)(n.li,{children:"Marks the specific 4KB page as allocated in the bitset."}),"\n",(0,l.jsxs)(n.li,{children:["Checks if ",(0,l.jsx)(n.code,{children:"(bitset.count() / 512.0) > threshold_for_promotion"}),"."]}),"\n",(0,l.jsxs)(n.li,{children:["If above threshold, sets promotion flag to true and increments ",(0,l.jsx)(n.code,{children:"stats.two_mb_promoted"}),"."]}),"\n",(0,l.jsx)(n.li,{children:"Otherwise, continues using 4KB pages within that 2MB region."}),"\n"]}),"\n",(0,l.jsx)(n.h3,{id:"allocate",children:"allocate()"}),"\n",(0,l.jsx)(n.pre,{children:(0,l.jsx)(n.code,{className:"language-cpp",children:"std::pair<UInt64, UInt64> ReservationTHPAllocator::allocate(\n     UInt64 size,\n     UInt64 address,\n     UInt64 core_id,\n     bool is_pagetable_allocation\n);\n"})}),"\n",(0,l.jsxs)(n.p,{children:[(0,l.jsx)(n.strong,{children:"Purpose:"})," Allocates size bytes of memory at the given address on behalf of ",(0,l.jsx)(n.code,{children:"core_id"}),". Supports promotion/fallback/demotion logic."]}),"\n",(0,l.jsxs)(n.p,{children:[(0,l.jsx)(n.strong,{children:"Returns:"})," A pair ",(0,l.jsx)(n.code,{children:"(physicalAddress, pageSizeFlag)"}),":"]}),"\n",(0,l.jsxs)(n.ul,{children:["\n",(0,l.jsxs)(n.li,{children:[(0,l.jsx)(n.code,{children:"physicalAddress"}),": The actual physical address allocated."]}),"\n",(0,l.jsxs)(n.li,{children:[(0,l.jsx)(n.code,{children:"pageSizeFlag"}),":","\n",(0,l.jsxs)(n.ul,{children:["\n",(0,l.jsx)(n.li,{children:"21 if the allocation was promoted to a 2MB page."}),"\n",(0,l.jsx)(n.li,{children:"12 if it is a regular 4KB page.\n(These flag values are illustrative in this code; you could use more robust enumerations in production.)"}),"\n"]}),"\n"]}),"\n"]}),"\n",(0,l.jsx)(n.p,{children:(0,l.jsx)(n.strong,{children:"Process:"})}),"\n",(0,l.jsxs)(n.ul,{children:["\n",(0,l.jsxs)(n.li,{children:["If ",(0,l.jsx)(n.code,{children:"is_pagetable_allocation == true"}),", directly requests a 4KB page from the buddy allocator."]}),"\n",(0,l.jsxs)(n.li,{children:["Otherwise, calls ",(0,l.jsx)(n.code,{children:"checkFor2MBAllocation()"}),":","\n",(0,l.jsxs)(n.ul,{children:["\n",(0,l.jsxs)(n.li,{children:["If successful, decide if it was promoted (",(0,l.jsx)(n.code,{children:"pageSizeFlag = 21"}),") or not (",(0,l.jsx)(n.code,{children:"pageSizeFlag = 12"}),")."]}),"\n",(0,l.jsxs)(n.li,{children:["If unsuccessful, fall back to ",(0,l.jsx)(n.code,{children:"buddy_allocator->allocate(...)"}),"."]}),"\n",(0,l.jsxs)(n.li,{children:["If the buddy allocator fails for a 4KB fallback, calls ",(0,l.jsx)(n.code,{children:"demote_page()"})," to free up space and retries."]}),"\n"]}),"\n"]}),"\n",(0,l.jsx)(n.li,{children:"Returns the final allocation outcome."}),"\n"]}),"\n",(0,l.jsx)(n.h3,{id:"givepagefast",children:"givePageFast()"}),"\n",(0,l.jsx)(n.pre,{children:(0,l.jsx)(n.code,{className:"language-cpp",children:"UInt64 ReservationTHPAllocator::givePageFast(UInt64 bytes, UInt64 address, UInt64 core_id);\n"})}),"\n",(0,l.jsxs)(n.p,{children:[(0,l.jsx)(n.strong,{children:"Purpose:"})," Provides a fast path allocation for straightforward 4KB pages via the buddy allocator, bypassing any THP logic."]}),"\n",(0,l.jsxs)(n.p,{children:[(0,l.jsx)(n.strong,{children:"Use Cases:"})," May be handy for special allocations where THP logic is not needed or during urgent allocations requiring minimal overhead."]}),"\n",(0,l.jsx)(n.h3,{id:"deallocate",children:"deallocate()"}),"\n",(0,l.jsx)(n.pre,{children:(0,l.jsx)(n.code,{className:"language-cpp",children:"void ReservationTHPAllocator::deallocate(UInt64 region_begin, UInt64 core_id);\n"})}),"\n",(0,l.jsxs)(n.p,{children:[(0,l.jsx)(n.strong,{children:"Purpose:"})," Placeholder for deallocation logic."]}),"\n",(0,l.jsxs)(n.p,{children:[(0,l.jsx)(n.strong,{children:"Status:"})," Not yet implemented."]}),"\n",(0,l.jsx)(n.p,{children:(0,l.jsx)(n.strong,{children:"Intended Behavior:"})}),"\n",(0,l.jsxs)(n.ul,{children:["\n",(0,l.jsx)(n.li,{children:"Would free a region or part of a region back to the allocator (buddy or THP map)."}),"\n",(0,l.jsx)(n.li,{children:"In a fully implemented system, this should handle partial or complete deallocation of 2MB regions or 4KB pages."}),"\n"]}),"\n",(0,l.jsx)(n.h3,{id:"allocate_ranges",children:"allocate_ranges()"}),"\n",(0,l.jsx)(n.pre,{children:(0,l.jsx)(n.code,{className:"language-cpp",children:"std::vector<Range> ReservationTHPAllocator::allocate_ranges(UInt64 size, UInt64 core_id);\n"})}),"\n",(0,l.jsxs)(n.p,{children:[(0,l.jsx)(n.strong,{children:"Purpose:"})," Future extension for allocating multiple contiguous or non-contiguous ranges at once."]}),"\n",(0,l.jsxs)(n.p,{children:[(0,l.jsx)(n.strong,{children:"Status:"})," Not implemented in this code snippet."]}),"\n",(0,l.jsxs)(n.p,{children:[(0,l.jsx)(n.strong,{children:"Returns:"})," An empty ",(0,l.jsx)(n.code,{children:"std::vector<Range>"})," as a placeholder."]}),"\n",(0,l.jsx)(n.h3,{id:"getfreepages",children:"getFreePages()"}),"\n",(0,l.jsx)(n.pre,{children:(0,l.jsx)(n.code,{className:"language-cpp",children:"UInt64 ReservationTHPAllocator::getFreePages() const;\n"})}),"\n",(0,l.jsxs)(n.p,{children:[(0,l.jsx)(n.strong,{children:"Purpose:"})," Returns the number of 4KB pages available in the buddy allocator."]}),"\n",(0,l.jsx)(n.h3,{id:"gettotalpages",children:"getTotalPages()"}),"\n",(0,l.jsx)(n.pre,{children:(0,l.jsx)(n.code,{className:"language-cpp",children:"UInt64 ReservationTHPAllocator::getTotalPages() const;\n"})}),"\n",(0,l.jsxs)(n.p,{children:[(0,l.jsx)(n.strong,{children:"Purpose:"})," Returns the total number of pages (again, in 4KB units) managed by the buddy allocator."]}),"\n",(0,l.jsx)(n.h3,{id:"getlargepageratio",children:"getLargePageRatio()"}),"\n",(0,l.jsx)(n.pre,{children:(0,l.jsx)(n.code,{className:"language-cpp",children:"double ReservationTHPAllocator::getLargePageRatio();\n"})}),"\n",(0,l.jsxs)(n.p,{children:[(0,l.jsx)(n.strong,{children:"Purpose:"})," Retrieves the ratio of large pages (e.g., 2MB blocks) to total pages from the buddy allocator."]}),"\n",(0,l.jsx)(n.h3,{id:"getaveragesizeratio",children:"getAverageSizeRatio()"}),"\n",(0,l.jsx)(n.pre,{children:(0,l.jsx)(n.code,{className:"language-cpp",children:"double ReservationTHPAllocator::getAverageSizeRatio();\n"})}),"\n",(0,l.jsxs)(n.p,{children:[(0,l.jsx)(n.strong,{children:"Purpose:"})," Reports the average size ratio of allocated blocks within the buddy allocator."]}),"\n",(0,l.jsxs)(n.p,{children:[(0,l.jsx)(n.strong,{children:"Details:"})," Implementation depends on the Buddy class\u2019s internal logic."]}),"\n",(0,l.jsx)(n.h3,{id:"fragment_memory",children:"fragment_memory()"}),"\n",(0,l.jsx)(n.pre,{children:(0,l.jsx)(n.code,{className:"language-cpp",children:"void ReservationTHPAllocator::fragment_memory(double fragmentation);\n"})}),"\n",(0,l.jsxs)(n.p,{children:[(0,l.jsx)(n.strong,{children:"Purpose:"})," Artificially induce fragmentation in the buddy allocator for testing or simulation."]}),"\n",(0,l.jsx)(n.hr,{}),"\n",(0,l.jsx)(n.h2,{id:"logging-and-debugging",children:"Logging and Debugging"}),"\n",(0,l.jsxs)(n.ul,{children:["\n",(0,l.jsxs)(n.li,{children:["The ",(0,l.jsx)(n.code,{children:"#define DEBUG_RESERVATION_THP"})," macro toggles detailed debug statements."]}),"\n",(0,l.jsxs)(n.li,{children:["Logs are written to ",(0,l.jsx)(n.code,{children:"reservation_thp.log"}),"."]}),"\n"]}),"\n",(0,l.jsx)(n.p,{children:(0,l.jsx)(n.strong,{children:"Debug output includes:"})}),"\n",(0,l.jsxs)(n.ul,{children:["\n",(0,l.jsx)(n.li,{children:"Allocation requests and outcomes."}),"\n",(0,l.jsx)(n.li,{children:"Region promotions and demotions."}),"\n",(0,l.jsx)(n.li,{children:"Bitset state for reserved 2MB regions."}),"\n"]}),"\n",(0,l.jsx)(n.hr,{}),"\n",(0,l.jsx)(n.h2,{id:"dependencies",children:"Dependencies"}),"\n",(0,l.jsx)(n.p,{children:(0,l.jsxs)(n.strong,{children:["Buddy Allocator (",(0,l.jsx)(n.code,{children:"buddy_allocator"}),")"]})}),"\n",(0,l.jsx)(n.p,{children:"The class relies on Buddy, which must implement:"}),"\n",(0,l.jsxs)(n.ul,{children:["\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.code,{children:"reserve_2mb_page(...)"})}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.code,{children:"allocate(...)"})}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.code,{children:"free(...)"})}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.code,{children:"fragmentMemory(...)"})}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.code,{children:"getFreePages()"})}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.code,{children:"getTotalPages()"})}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.code,{children:"getLargePageRatio()"})}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.code,{children:"getAverageSizeRatio()"})}),"\n"]}),"\n",(0,l.jsx)(n.p,{children:(0,l.jsx)(n.strong,{children:"Standard C++ Libraries:"})}),"\n",(0,l.jsxs)(n.ul,{children:["\n",(0,l.jsxs)(n.li,{children:[(0,l.jsx)(n.code,{children:"<vector>"}),", ",(0,l.jsx)(n.code,{children:"<list>"}),", ",(0,l.jsx)(n.code,{children:"<utility>"}),", ",(0,l.jsx)(n.code,{children:"<bitset>"}),", ",(0,l.jsx)(n.code,{children:"<iostream>"}),", ",(0,l.jsx)(n.code,{children:"<fstream>"}),", ",(0,l.jsx)(n.code,{children:"<algorithm>"}),", etc."]}),"\n"]}),"\n",(0,l.jsx)(n.p,{children:(0,l.jsx)(n.strong,{children:"Other Includes:"})}),"\n",(0,l.jsxs)(n.ul,{children:["\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.code,{children:"physical_memory_allocator.h"})}),"\n",(0,l.jsx)(n.li,{children:(0,l.jsx)(n.code,{children:"reserve_thp.h"})}),"\n"]}),"\n",(0,l.jsx)(n.hr,{}),"\n",(0,l.jsx)(n.h2,{id:"how-to-use",children:"How to Use"}),"\n",(0,l.jsx)(n.p,{children:(0,l.jsx)(n.strong,{children:"Include Headers:"})}),"\n",(0,l.jsx)(n.pre,{children:(0,l.jsx)(n.code,{className:"language-cpp",children:'#include "physical_memory_allocator.h"\n#include "reserve_thp.h"\n#include "buddy_allocator.h"\n// ... other relevant headers\n'})}),"\n",(0,l.jsx)(n.p,{children:(0,l.jsx)(n.strong,{children:"Instantiate the Allocator:"})}),"\n",(0,l.jsx)(n.pre,{children:(0,l.jsx)(n.code,{className:"language-cpp",children:'ReservationTHPAllocator thpAllocator(\n     "THP_Allocator",\n     memory_size_in_4KB_pages,\n     max_order,\n     kernel_size_in_4KB_pages,\n     "some_fragmentation_type",\n     0.7f // threshold_for_promotion\n);\n'})}),"\n",(0,l.jsx)(n.p,{children:(0,l.jsx)(n.strong,{children:"Perform Allocations:"})}),"\n",(0,l.jsx)(n.pre,{children:(0,l.jsx)(n.code,{className:"language-cpp",children:'// Example: Allocate 4KB at virtual address 0x100000 on core 0\nauto result = thpAllocator.allocate(4096, 0x100000, 0, false);\n\n// Check outcome\nif (result.first == -1) {\n     std::cerr << "Allocation failed!\\n";\n} else {\n     std::cout << "Allocated physical address: " \n                  << result.first \n                  << ", pageSizeFlag: " \n                  << result.second \n                  << std::endl;\n}\n'})}),"\n",(0,l.jsx)(n.p,{children:(0,l.jsx)(n.strong,{children:"Check for Promotion:"})}),"\n",(0,l.jsx)(n.p,{children:"If enough 4KB pages in a 2MB region become allocated, the region will be promoted automatically once the threshold is exceeded."}),"\n",(0,l.jsx)(n.p,{children:(0,l.jsx)(n.strong,{children:"Demote Pages (Manually Triggered):"})}),"\n",(0,l.jsx)(n.pre,{children:(0,l.jsx)(n.code,{className:"language-cpp",children:'bool success = thpAllocator.demote_page();\nif (success) {\n     std::cout << "Successfully demoted one 2MB region." << std::endl;\n} else {\n     std::cout << "No 2MB region available for demotion." << std::endl;\n}\n'})}),"\n",(0,l.jsx)(n.hr,{}),"\n",(0,l.jsx)(n.h2,{id:"example-usage",children:"Example Usage"}),"\n",(0,l.jsx)(n.p,{children:"Below is a simplified snippet that demonstrates how one might use the ReservationTHPAllocator in a hypothetical system:"}),"\n",(0,l.jsx)(n.pre,{children:(0,l.jsx)(n.code,{className:"language-cpp",children:'#include <iostream>\n#include "reserve_thp.h"\n#include "physical_memory_allocator.h"\n\nint main() {\n     // Basic parameters\n     int memory_size = 16384;   // 64 MB if each page is 4KB\n     int max_order   = 11;      // For buddy system\n     int kernel_size = 1024;    // 4 MB reserved for kernel\n     float promotion_threshold = 0.75f; // 75% usage threshold\n\n     // Instantiate the THP Allocator\n     ReservationTHPAllocator thpAllocator(\n          "THP_Allocator",\n          memory_size,\n          max_order,\n          kernel_size,\n          "default_fragmentation",\n          promotion_threshold\n     );\n\n     // Request an allocation\n     UInt64 virtualAddress = 0x100000; // Example virtual address\n     auto [physicalAddress, pageFlag] = thpAllocator.allocate(4096, virtualAddress, 0, false);\n\n     if (physicalAddress == static_cast<UInt64>(-1)) {\n          std::cerr << "Allocation failed!" << std::endl;\n     } else {\n          std::cout << "Allocated 4KB at physical address 0x"\n                        << std::hex << physicalAddress\n                        << " with pageFlag="\n                        << pageFlag\n                        << std::endl;\n     }\n\n     // ... additional logic ...\n\n     return 0;\n}\n'})}),"\n",(0,l.jsx)(n.hr,{})]})}function h(e={}){const{wrapper:n}={...(0,i.R)(),...e.components};return n?(0,l.jsx)(n,{...e,children:(0,l.jsx)(d,{...e})}):d(e)}},8453:(e,n,s)=>{s.d(n,{R:()=>o,x:()=>a});var r=s(6540);const l={},i=r.createContext(l);function o(e){const n=r.useContext(i);return r.useMemo((function(){return"function"==typeof e?e(n):{...n,...e}}),[n,e])}function a(e){let n;return n=e.disableParentContext?"function"==typeof e.components?e.components(l):e.components||l:o(e.components),r.createElement(i.Provider,{value:n},e.children)}}}]);