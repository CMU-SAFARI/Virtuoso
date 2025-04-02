"use strict";(self.webpackChunkvirtuoso=self.webpackChunkvirtuoso||[]).push([[456],{8453:(e,t,a)=>{a.d(t,{R:()=>r,x:()=>i});var n=a(6540);const s={},l=n.createContext(s);function r(e){const t=n.useContext(l);return n.useMemo((function(){return"function"==typeof e?e(t):{...t,...e}}),[t,e])}function i(e){let t;return t=e.disableParentContext?"function"==typeof e.components?e.components(s):e.components||s:r(e.components),n.createElement(l.Provider,{value:t},e.children)}},9395:(e,t,a)=>{a.r(t),a.d(t,{assets:()=>c,contentTitle:()=>i,default:()=>h,frontMatter:()=>r,metadata:()=>n,toc:()=>o});const n=JSON.parse('{"id":"MMU Designs/performing_ptw","title":"Page Table Walker","description":"The page table walker (PTW) is a critical component of the memory management unit (MMU) that translates virtual addresses to physical addresses. The PTW is responsible for walking the page table hierarchy to find the physical address corresponding to a given virtual address. This process involves multiple memory accesses and can be time-consuming, especially in systems with large page tables.","source":"@site/docs/MMU Designs/performing_ptw.md","sourceDirName":"MMU Designs","slug":"/MMU Designs/performing_ptw","permalink":"/Virtuoso/website/docs/MMU Designs/performing_ptw","draft":false,"unlisted":false,"editUrl":"https://github.com/facebook/docusaurus/tree/main/packages/create-docusaurus/templates/shared/docs/MMU Designs/performing_ptw.md","tags":[],"version":"current","frontMatter":{},"sidebar":"tutorialSidebar","previous":{"title":"Range Mappings MMU Design","permalink":"/Virtuoso/website/docs/MMU Designs/mmu_rmm"},"next":{"title":"Physical Memory Allocators","permalink":"/Virtuoso/website/docs/category/physical-memory-allocators"}}');var s=a(4848),l=a(8453);const r={},i="Page Table Walker",c={},o=[{value:"Overview",id:"overview",level:2}];function d(e){const t={code:"code",h1:"h1",h2:"h2",header:"header",li:"li",ol:"ol",p:"p",pre:"pre",ul:"ul",...(0,l.R)(),...e.components};return(0,s.jsxs)(s.Fragment,{children:[(0,s.jsx)(t.header,{children:(0,s.jsx)(t.h1,{id:"page-table-walker",children:"Page Table Walker"})}),"\n",(0,s.jsx)(t.p,{children:"The page table walker (PTW) is a critical component of the memory management unit (MMU) that translates virtual addresses to physical addresses. The PTW is responsible for walking the page table hierarchy to find the physical address corresponding to a given virtual address. This process involves multiple memory accesses and can be time-consuming, especially in systems with large page tables."}),"\n",(0,s.jsx)(t.h2,{id:"overview",children:"Overview"}),"\n",(0,s.jsx)(t.p,{children:"The mmu_base.cc is responsible for calculating the latency of the page table walk operation. The page table walk latency is calculated based on the number of memory accesses required to traverse the page table hierarchy and the latency of each memory access."}),"\n",(0,s.jsxs)(t.ol,{children:["\n",(0,s.jsxs)(t.li,{children:["The function ",(0,s.jsx)(t.code,{children:"MemoryManagementUnitBase::performPTW"})," is called to perform the page table walk for a given virtual address. This function triggers the page table walk operation and returns the physical address, the page size, the time taken for the page table walk, and whether a page fault occurred."]}),"\n"]}),"\n",(0,s.jsx)(t.pre,{children:(0,s.jsx)(t.code,{className:"language-cpp",children:'/**\n * @brief Perform a Page Table Walk (PTW) for a given address.\n *\n *\n * @param address The virtual address for which the PTW is performed.\n * @param modeled A boolean indicating whether the PTW should be modeled.\n * @param count A boolean indicating whether the PTW should be counted.\n * @param is_prefetch A boolean indicating whether the PTW is for a prefetch operation.\n * @param eip The instruction pointer (EIP) at the time of the PTW.\n * @param lock The lock signal for the core.\n * @return A tuple containing:\n *         - The time taken for the PTW (SubsecondTime).\n *         - Whether a page fault occurred (bool).\n *         - The physical page number (IntPtr) resulting from the PTW (at the 4KB granularity).\n *         - The page size (int).\n */\n\ntuple<SubsecondTime, bool, IntPtr, int> MemoryManagementUnitBase::performPTW(IntPtr address, bool modeled, bool count, bool is_prefetch, IntPtr eip, Core::lock_signal_t lock, PageTable *page_table)\n{\n#ifdef DEBUG_MMU\n    log_file_mmu << std::endl;\n    log_file_mmu << "-------------------------------------" << std::endl;\n    log_file_mmu << "[MMU_BASE] Starting PTW for address: " << address << std::endl;\n#endif\n  auto ptw_result = page_table->initializeWalk(address, count, is_prefetch);\n'})}),"\n",(0,s.jsxs)(t.ul,{children:["\n",(0,s.jsxs)(t.li,{children:["\n",(0,s.jsxs)(t.p,{children:["The ",(0,s.jsx)(t.code,{children:"MemoryManagementUnitBase::performPTW"})," function initializes the page table walk and filters out redundant memory accesses during the walk. What do we consider as redundant memory accesses? When a page table walk is performed and a page fault\noccurs, the page table walker will restart the walk from the root of the page table hierarchy and access the same memory locations again. These redundant memory accesses can be filtered out for simulation speed, as they do not affect accuracy significantly."]}),"\n"]}),"\n",(0,s.jsxs)(t.li,{children:["\n",(0,s.jsx)(t.p,{children:"Why do we filter if the page table is radix?\nThe page walk caches (PWC) are used to cache the results of page table walks to reduce the latency of subsequent walks.\nWe filter out the page table entries that hit in the PWC to avoid redundant memory accesses."}),"\n"]}),"\n"]}),"\n",(0,s.jsx)(t.pre,{children:(0,s.jsx)(t.code,{className:"language-cpp",children:'ptw_result = make_tuple(get<0>(ptw_result), visited_pts, get<2>(ptw_result), get<3>(ptw_result), get<4>(ptw_result));\n\n// Filter the PTW result based on the page table type\n// This filtering is necessary to remove any redundant accesses that may hit in the PWC\n\nif (page_table->getType() == "radix")\n{\n  filterPTWResult(ptw_result, page_table, count);\n}\n'})}),"\n",(0,s.jsxs)(t.ul,{children:["\n",(0,s.jsxs)(t.li,{children:["Now the core: ",(0,s.jsx)(t.code,{children:"MemoryManagementUnitBase::calculatePTWCycles"})," function is called to calculate the latency of the page table walk operation. This function calculates the time taken for the page table walk based on the number of memory accesses required to traverse the page table hierarchy and the latency of each memory access."]}),"\n"]}),"\n",(0,s.jsx)(t.pre,{children:(0,s.jsx)(t.code,{className:"language-cpp",children:'int page_size = get<0>(ptw_result);\nIntPtr ppn_result = get<2>(ptw_result);\nbool is_pagefault = get<4>(ptw_result);\n\n\nSubsecondTime ptw_cycles = calculatePTWCycles(ptw_result, count, modeled, eip, lock);\n\n#ifdef DEBUG_MMU\n  log_file_mmu << "[MMU_BASE] Finished PTW for address: " << address << std::endl;\n  log_file_mmu << "[MMU_BASE] PTW latency: " << ptw_cycles << std::endl;\n  log_file_mmu << "[MMU_BASE] Physical Page Number: " << ppn_result << std::endl;\n  log_file_mmu << "[MMU_BASE] Page Size: " << page_size << std::endl;\n  log_file_mmu << "[MMU_BASE] -------------------------------------" << std::endl;\n#endif\n\nreturn make_tuple(ptw_cycles, is_pagefault, ppn_result, page_size);\n'})}),"\n",(0,s.jsxs)(t.ol,{start:"2",children:["\n",(0,s.jsxs)(t.li,{children:["The ",(0,s.jsx)(t.code,{children:"MemoryManagementUnitBase::calculatePTWCycles"})," may seem overly complex, but it is designed this way so\nthat it can be used in a generic manner for every potential page table walk operation (e.g., page table walks for\nhash-based PTs)."]}),"\n"]}),"\n",(0,s.jsxs)(t.ul,{children:["\n",(0,s.jsxs)(t.li,{children:["If there is a page fault, the memory requests generated by the PTW should be sent to the\ncache hierarchy after the page fault handling latency. The ",(0,s.jsx)(t.code,{children:"Sim()->getMimicOS()->getPageFaultLatency()"})," function is used to get the page fault handling latency."]}),"\n"]}),"\n",(0,s.jsx)(t.pre,{children:(0,s.jsx)(t.code,{className:"language-cpp",children:"if (is_pagefault)\n{\n  initial_delay = Sim()->getMimicOS()->getPageFaultLatency();\n}\n"})}),"\n",(0,s.jsxs)(t.ul,{children:["\n",(0,s.jsx)(t.li,{children:"We iterate over every table and level in the page table hierarchy and calculate the latency of each memory access."}),"\n"]}),"\n",(0,s.jsxs)(t.ul,{children:["\n",(0,s.jsxs)(t.li,{children:["\n",(0,s.jsx)(t.p,{children:"What is table? In the case of radix-based page tables, we only have a single table (the root table). However, in hash-based page tables we may have different tables for different page sizes."}),"\n"]}),"\n",(0,s.jsxs)(t.li,{children:["\n",(0,s.jsx)(t.p,{children:'What is level? The level corresponds to the "depth" that the memory request has been issued at. For example, in the case of radix-based page tables, the root request is at level 0, the next level is level 1, and so on. The final level\'s depth is 4.\nWe use the concept of "level" so that we "serialize" the memory requests being sent to the memory hierarchy.'}),"\n"]}),"\n"]}),"\n",(0,s.jsx)(t.pre,{children:(0,s.jsx)(t.code,{className:"language-cpp",children:'if (is_pagefault)\n{\n  initial_delay = Sim()->getMimicOS()->getPageFaultLatency();\n}\n\nfor (int tab = 0; tab < (tables + 1); tab++)\n{\n  SubsecondTime fetch_delay = initial_delay;\n\n  for (int level = 0; level < (levels + 1); level++)\n  {\n\n    for (UInt32 req = 0; req < accesses.size(); req++)\n    {\n      if (get<1>(accesses[req]) == level && get<0>(accesses[req]) == tab)\n      {\n#ifdef DEBUG_MMU\n  log_file_mmu << "[MMU_BASE] We are accessing address: " << get<2>(accesses[req]) << " from level: " << level << " in table: " << tab << " at time: " << t_now+fetch_delay << std::endl;\n#endif\t\t\t\t\t\n\n\n          \n        packet.address = get<2>(accesses[req]);\n        latency = accessCache(packet, t_now+fetch_delay);\n\n#ifdef DEBUG_MMU\n  log_file_mmu << "[MMU_BASE] We accessed address: " << get<2>(accesses[req]) << " from level: " << level << " in table: " << tab << " at time: " << t_now+fetch_delay << " with latency: " << latency << std::endl;\n#endif\n      \n        if (get<3>(accesses[req]) == true)\n        {\n          correct_table = get<0>(accesses[req]);\n          correct_level = get<1>(accesses[req]);\n          latency_per_table_per_level[get<0>(accesses[req])][get<1>(accesses[req])] = latency;\n          break;\n        }\n        else if (latency_per_table_per_level[get<0>(accesses[req])][get<1>(accesses[req])] < latency && level != correct_level)\n          latency_per_table_per_level[get<0>(accesses[req])][get<1>(accesses[req])] = latency;\n      }\n    }\n    fetch_delay += latency_per_table_per_level[tab][level];\n#ifdef DEBUG_MMU\n    log_file_mmu << "[MMU_BASE] Finished PTW for level: " << level << " at time: " << t_now << " with max latency: " << fetch_delay << std::endl;\n#endif\n  }\n}\n'})})]})}function h(e={}){const{wrapper:t}={...(0,l.R)(),...e.components};return t?(0,s.jsx)(t,{...e,children:(0,s.jsx)(d,{...e})}):d(e)}}}]);