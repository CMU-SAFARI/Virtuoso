"use strict";(self.webpackChunkvirtuoso=self.webpackChunkvirtuoso||[]).push([[397],{3022:(e,t,n)=>{n.r(t),n.d(t,{assets:()=>o,contentTitle:()=>r,default:()=>d,frontMatter:()=>i,metadata:()=>a,toc:()=>c});const a=JSON.parse('{"id":"MMU Designs/baseline_mmu","title":"Baseline MMU","description":"This document provides an in-depth walkthrough of the Memory Management Unit (MMU) implementation.","source":"@site/docs/MMU Designs/baseline_mmu.md","sourceDirName":"MMU Designs","slug":"/MMU Designs/baseline_mmu","permalink":"/docs/MMU Designs/baseline_mmu","draft":false,"unlisted":false,"editUrl":"https://github.com/facebook/docusaurus/tree/main/packages/create-docusaurus/templates/shared/docs/MMU Designs/baseline_mmu.md","tags":[],"version":"current","sidebarPosition":2,"frontMatter":{"sidebar_position":2},"sidebar":"tutorialSidebar","previous":{"title":"MMU Designs","permalink":"/docs/category/mmu-designs"},"next":{"title":"Part-of-Memory TLB (POM-TLB) MMU Design","permalink":"/docs/MMU Designs/mmu_pomtlb"}}');var s=n(4848),l=n(8453);const i={sidebar_position:2},r="Baseline MMU",o={},c=[{value:"Constructor and Destructor",id:"constructor-and-destructor",level:3},{value:"Address Translation",id:"address-translation",level:3},{value:"Debugging",id:"debugging",level:2}];function h(e){const t={code:"code",h1:"h1",h2:"h2",h3:"h3",header:"header",li:"li",ol:"ol",p:"p",pre:"pre",strong:"strong",ul:"ul",...(0,l.R)(),...e.components};return(0,s.jsxs)(s.Fragment,{children:[(0,s.jsx)(t.header,{children:(0,s.jsx)(t.h1,{id:"baseline-mmu",children:"Baseline MMU"})}),"\n",(0,s.jsx)(t.p,{children:"This document provides an in-depth walkthrough of the Memory Management Unit (MMU) implementation."}),"\n",(0,s.jsx)(t.p,{children:"We will explore:"}),"\n",(0,s.jsxs)(t.ul,{children:["\n",(0,s.jsx)(t.li,{children:"The different components of the MMU"}),"\n",(0,s.jsx)(t.li,{children:"How the MMU handles address translation"}),"\n"]}),"\n",(0,s.jsx)(t.h3,{id:"constructor-and-destructor",children:"Constructor and Destructor"}),"\n",(0,s.jsx)(t.p,{children:"The MMU needs backward pointers to the core, the memory manager and the time model."}),"\n",(0,s.jsx)(t.pre,{children:(0,s.jsx)(t.code,{className:"language-cpp",children:"MemoryManagementUnit::MemoryManagementUnit(Core *_core, MemoryManager *_memory_manager, ShmemPerfModel *_shmem_perf_model, String _name, MemoryManagementUnitBase *_nested_mmu)\n"})}),"\n",(0,s.jsxs)(t.ul,{children:["\n",(0,s.jsx)(t.li,{children:(0,s.jsx)(t.strong,{children:"instantiatePageTableWalker()"})}),"\n"]}),"\n",(0,s.jsx)(t.p,{children:"The MMU uses two components to accelerate page table walks:"}),"\n",(0,s.jsx)(t.p,{children:"(i) The number of page table walkers, which can be configured to be more than one to handle multiple page table walks in parallel.\n(ii) The page walk caches to store the results of page table walks to avoid redundant walks."}),"\n",(0,s.jsx)(t.pre,{children:(0,s.jsx)(t.code,{className:"language-cpp",children:"void MemoryManagementUnit::instantiatePageTableWalker()\n"})}),"\n",(0,s.jsxs)(t.ul,{children:["\n",(0,s.jsxs)(t.li,{children:[(0,s.jsx)(t.strong,{children:"instantiateTLBSubsystem()"}),"\nSets up the TLB (Translation Lookaside Buffer) hierarchy which is crucial for speeding up address translation by storing recent translations. The TLB hierarchy can be configured to have multiple levels of TLBs and different number of TLBs per level."]}),"\n"]}),"\n",(0,s.jsx)(t.pre,{children:(0,s.jsx)(t.code,{className:"language-cpp",children:"void MemoryManagementUnit::instantiateTLBSubsystem()\n"})}),"\n",(0,s.jsxs)(t.ul,{children:["\n",(0,s.jsxs)(t.li,{children:[(0,s.jsx)(t.strong,{children:"registerMMUStats()"}),"\nRegisters various statistics like page faults and translation latencies, which are useful for performance monitoring and debugging."]}),"\n"]}),"\n",(0,s.jsx)(t.pre,{children:(0,s.jsx)(t.code,{className:"language-cpp",children:"void MemoryManagementUnit::registerMMUStats()\n"})}),"\n",(0,s.jsx)(t.h3,{id:"address-translation",children:"Address Translation"}),"\n",(0,s.jsxs)(t.ul,{children:["\n",(0,s.jsxs)(t.li,{children:["\n",(0,s.jsx)(t.p,{children:(0,s.jsx)(t.strong,{children:"performAddressTranslation()"})}),"\n",(0,s.jsx)(t.p,{children:"The function is called by the memory manager and offloads the address translation to the MMU."}),"\n",(0,s.jsx)(t.p,{children:"Conducts the translation of a virtual address to a physical address, considering whether the address is for data or instructions, and updates performance metrics accordingly. The function returns the physical address and the time taken for translation."}),"\n"]}),"\n"]}),"\n",(0,s.jsx)(t.pre,{children:(0,s.jsx)(t.code,{className:"language-cpp",children:"IntPtr MemoryManagementUnit::performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count)\n"})}),"\n",(0,s.jsxs)(t.ul,{children:["\n",(0,s.jsxs)(t.li,{children:["\n",(0,s.jsx)(t.p,{children:(0,s.jsx)(t.strong,{children:"Accessing the TLB Subsystem"})}),"\n",(0,s.jsx)(t.p,{children:"The TLB subsystem is accessed through the MMU. The TLB subsystem is responsible for storing recent translations to speed up address translation. We perform a lookup in the TLB hierarchy to find the translation for the virtual address."}),"\n",(0,s.jsxs)(t.ol,{children:["\n",(0,s.jsx)(t.li,{children:"We iterate through the TLB hierarchy across all levels and all TLBs at each level."}),"\n"]}),"\n"]}),"\n"]}),"\n",(0,s.jsx)(t.pre,{children:(0,s.jsx)(t.code,{className:"language-cpp",children:"// We iterate through the TLB hierarchy to find if there is a TLB hit\nfor (UInt32 i = 0; i < tlbs.size(); i++){\n  for (UInt32 j = 0; j < tlbs[i].size(); j++){\n    bool tlb_stores_instructions = (tlbs[i][j]->getType() == TLBtype::Instruction) || (tlbs[i][j]->getType() == TLBtype::Unified);\n\n    // If the TLB stores instructions, we need to check if the address is an instruction address\n    if (tlb_stores_instructions && instruction){\n      // @kanellok: Passing the page table to the TLB lookup function is a legacy from the old TLB implementation. \n      // It is not used in the current implementation.\n\n      tlb_block_info = tlbs[i][j]->lookup(address, time, count, lock, eip, modeled, count, NULL);\n\n      if (tlb_block_info != NULL){\n        hit_tlb = tlbs[i][j]; // Keep track of the TLB that hit\n        hit_level = i; // Keep track of the level of the TLB that hit\n        hit = true; // We have a hit\n        goto HIT; // @kanellok: This is ultra bad practice, but it works\n      }\n    }\n  }\n}\n"})}),"\n",(0,s.jsxs)(t.ol,{start:"3",children:["\n",(0,s.jsx)(t.li,{children:"If there is a TLB hit, we keep track of the TLB that hit and the block info."}),"\n"]}),"\n",(0,s.jsx)(t.pre,{children:(0,s.jsx)(t.code,{className:"language-cpp",children:"CacheBlockInfo *tlb_block_info // This variable will store the translation information if there is a TLB hit\n"})}),"\n",(0,s.jsxs)(t.ul,{children:["\n",(0,s.jsxs)(t.li,{children:["\n",(0,s.jsx)(t.p,{children:(0,s.jsx)(t.strong,{children:"Accessing the Page Table after a miss in the TLB subsystem"})}),"\n",(0,s.jsx)(t.p,{children:"The page table is accessed through the MMU. The page table is a crucial data structure that maps virtual addresses to physical addresses. The MMU requires a pointer to the page table to perform address translation. The page table is looked up after every L2 TLB miss."}),"\n"]}),"\n"]}),"\n",(0,s.jsxs)(t.ol,{children:["\n",(0,s.jsxs)(t.li,{children:["We need to keep track of the total walk latency and the total fault latency (if there was a fault).\nThe physical page number that we will get from the PTW is stored in ",(0,s.jsx)(t.code,{children:"ppn_result"}),"."]}),"\n"]}),"\n",(0,s.jsx)(t.pre,{children:(0,s.jsx)(t.code,{className:"language-cpp",children:"// We need to keep track of the total walk latency and the total fault latency (if there was a fault)\nSubsecondTime total_walk_latency = SubsecondTime::Zero();\nSubsecondTime total_fault_latency = SubsecondTime::Zero();\n    // This is the physical page number that we will get from the PTW\nIntPtr ppn_result;\n"})}),"\n",(0,s.jsxs)(t.ol,{start:"2",children:["\n",(0,s.jsx)(t.li,{children:"We only trigger the PTW if there was a TLB miss. We keep track of the time before the PTW starts so\nthat we search for a free slot in the MSHRs for the PT walker."}),"\n"]}),"\n",(0,s.jsxs)(t.ul,{children:["\n",(0,s.jsx)(t.li,{children:"First, we find if there is any delay because of all the walkers being busy."}),"\n",(0,s.jsx)(t.li,{children:"We switch the time to the time when the PT walker is allocated so that we start the PTW at that time."}),"\n"]}),"\n",(0,s.jsx)(t.pre,{children:(0,s.jsx)(t.code,{className:"language-cpp",children:'// We only trigger the PTW if there was a TLB miss\nif (!hit)\n{\t\n  // Keep track of the time before the PTW starts\n  SubsecondTime time_for_pt = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);\n\n  // We will occupy an entry in the MSHRs for the PT walker\n  struct MSHREntry pt_walker_entry; \n  pt_walker_entry.request_time = time_for_pt;\n\n\n  // The system has N walkers that can be used to perform page table walks in parallel\n  // We need to find if there is any delay because of all the walkers being busy\n  SubsecondTime delay = pt_walkers->getSlotAllocationDelay(pt_walker_entry.request_time);\t\n\n  // We switch the time to the time when the PT walker is allocated so that we start the PTW at that time\n  shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time_for_pt + delay);\n  #ifdef DEBUG_MMU\n    log_file << "[MMU] New time after charging the PT walker allocation delay: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;\n  #endif\n'})}),"\n",(0,s.jsxs)(t.ol,{start:"3",children:["\n",(0,s.jsxs)(t.li,{children:["We retrieve the page table for the application that is currently running on the core. We then perform the PTW and get the PTW latency, PF latency, Physical Address, and Page Size as a tuple. The ",(0,s.jsx)(t.code,{children:"restart_ptw"})," variable is used to indicate whether the PTW should be automatically restarted in cases of a page fault."]}),"\n"]}),"\n",(0,s.jsx)(t.pre,{children:(0,s.jsx)(t.code,{className:"language-cpp",children:"// returns PTW latency, PF latency, Physical Address, Page Size as a tuple\nint app_id = core->getThread()->getAppId();\nPageTable* page_table = Sim()->getMimicOS()->getPageTable(app_id);\n\nbool restart_ptw = true;\nauto ptw_result = performPTW(address, modeled, count, false, eip, lock, page_table, restart_ptw);\n"})}),"\n",(0,s.jsxs)(t.ol,{start:"4",children:["\n",(0,s.jsx)(t.li,{children:"We need to calculate the total walk latency and the total fault latency."}),"\n"]}),"\n",(0,s.jsxs)(t.ul,{children:["\n",(0,s.jsx)(t.li,{children:"If the walk caused a page fault, we need to charge the page fault latency."}),"\n",(0,s.jsx)(t.li,{children:"In the baseline Virtuoso+Sniper, we charge a static page fault latency for all page faults (e.g., 1000 cycles)."}),"\n",(0,s.jsx)(t.li,{children:"We update the translation statistics with the total walk latency and the total fault latency."}),"\n"]}),"\n",(0,s.jsx)(t.pre,{children:(0,s.jsx)(t.code,{className:"language-cpp",children:"total_walk_latency = get<0>(ptw_result); // Total walk latency is only the time it takes to walk the page table (excluding page faults)\t\nif (count)\n{\n  translation_stats.total_walk_latency += total_walk_latency;\n  translation_stats.page_table_walks++;\n}\n\n// If the walk caused a page fault, we need to charge the page fault latency\nbool caused_page_fault = get<1>(ptw_result);\n\n\nif (caused_page_fault)\n{\n  SubsecondTime m_page_fault_latency = Sim()->getMimicOS()->getPageFaultLatency();\t\n  if (count)\n  {\n    translation_stats.page_faults++;\n    translation_stats.total_fault_latency += m_page_fault_latency;\n  }\n  total_fault_latency = m_page_fault_latency;\n}\n"})}),"\n",(0,s.jsxs)(t.ol,{start:"5",children:["\n",(0,s.jsx)(t.li,{children:"We need to calculate when the PTW will be completed to update the completion time of the PT walker entry."}),"\n"]}),"\n",(0,s.jsxs)(t.ul,{children:["\n",(0,s.jsx)(t.li,{children:"We set the completion time to the time before the PTW starts + delay + total walk latency + total fault latency. We then allocate the PT walker entry. The completion time of each PT walker is used to track the time when the PTW is completed so that\nwe charge the corresponding latencies."}),"\n",(0,s.jsx)(t.li,{children:"If the PTW caused a page fault, we need to set the time to the time after the PTW is completed.\nWe treat the fault as a pseudo-instruction and queue it in the performance model. The pseudo-instruction serializes the page fault routine and charges the page fault latency as if the ROB was stalled for that time (which would cause a full stall in the pipeline).\nIn this case, we also update the time so that the memory manager sends the request to the cache hierarchy after the Page Fault Routine is completed."}),"\n",(0,s.jsx)(t.li,{children:"If there was no page fault, we set the time to the time after the PTW is completed. Again, we update the time so that the memory manager sends the request to the cache hierarchy after the PTW is completed."}),"\n"]}),"\n",(0,s.jsx)(t.pre,{children:(0,s.jsx)(t.code,{className:"language-cpp",children:'/*\nWe need to set the completion time:\n1) Time before PTW starts\n2) Delay because of all the walkers being busy\n3) Total walk latency\n4) Total fault latency\n*/\n\npt_walker_entry.completion_time = time_for_pt + delay + total_walk_latency + total_fault_latency;\npt_walkers->allocate(pt_walker_entry);\n\n\nppn_result = get<2>(ptw_result);\npage_size = get<3>(ptw_result);\n\n/* \nWe need to set the time to the time after the PTW is completed. \nThis is done so that the memory manager sends the request to the cache hierarchy after the PTW is completed\n*/\nif (caused_page_fault){\n  PseudoInstruction *i = new PageFaultRoutineInstruction(total_fault_latency);\n  getCore()->getPerformanceModel()->queuePseudoInstruction(i);\n  shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, pt_walker_entry.completion_time);\n}\nelse{\n  shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, pt_walker_entry.completion_time);\n}\n\n#ifdef DEBUG_MMU\n  log_file << "[MMU] New time after charging the PT walker completion time: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;\n#endif\n\n}\n'})}),"\n",(0,s.jsx)(t.p,{children:(0,s.jsx)(t.strong,{children:"Allocating a new entry in the TLB subsystem"})}),"\n",(0,s.jsx)(t.p,{children:"After the page table walk or a TLB hit at a higher level (e.g., L2 TLB), we need to allocate the translation in the TLB that missed. We iterate through the TLB hierarchy to find the TLB that missed and allocate the translation in that TLB."}),"\n",(0,s.jsxs)(t.ul,{children:["\n",(0,s.jsx)(t.li,{children:'We only allocate the translation if the TLB supports the page size of the translation and the TLB is an "allocate on miss" TLB.'}),"\n",(0,s.jsx)(t.li,{children:"We also check if there are any evicted translations from the previous level and allocate them in the current TLB.\nFor example, if the L1 TLB misses, gets filled up and evicts some translations, we need to allocate these evicted translations in the L2 TLB which acts as a victim."}),"\n"]}),"\n",(0,s.jsx)(t.pre,{children:(0,s.jsx)(t.code,{className:"language-cpp",children:'\nfor (int i = 0; i < tlb_levels; i++)\n{\n  // We will check where we need to allocate the page\n\n  for (UInt32 j = 0; j < tlbs[i].size(); j++)\n  {\n    // We need to check if there are any evicted translations from the previous level and allocate them\n    if ((i > 0) && (evicted_translations[i - 1].size() != 0))\n    {\n      tuple<bool, IntPtr, int> result;\n\n#ifdef DEBUG_MMU\n      log_file << "[MMU] There are evicted translations from level: " << i - 1 << std::endl;\n#endif\n      // iterate through the evicted translations and allocate them in the current TLB\n      for (UInt32 k = 0; k < evicted_translations[i - 1].size(); k++)\n      {\n#ifdef DEBUG_MMU\n        log_file << "[MMU] Evicted Translation: " << get<0>(evicted_translations[i - 1][k]) << std::endl;\n#endif\n        // We need to check if the TLB supports the page size of the evicted translation\n        if (tlbs[i][j]->supportsPageSize(page_size))\n        {\n#ifdef DEBUG_MMU\n          log_file << "[MMU] Allocating evicted entry in TLB: Level = " << i << " Index =  " << j << std::endl;\n#endif\n\n          result = tlbs[i][j]->allocate(get<0>(evicted_translations[i - 1][k]), time, count, lock, get<1>(evicted_translations[i - 1][k]), ppn_result);\n\n          // If the allocation was successful and we have an evicted translation, \n          // we need to add it to the evicted translations vector for\n\n          if (get<0>(result) == true)\n          {\n            evicted_translations[i].push_back(make_tuple(get<1>(result), get<2>(result)));\n          }\n        }\n      }\n    }\n\n    // We need to allocate the current translation in the TLB if:\n    // 1) The TLB supports the page size of the translation\n    // 2) The TLB is an "allocate on miss" TLB\n    // 3) There was a TLB miss or the TLB hit was at a higher level and you need to allocate the translation in the current level\n    \n    if (tlbs[i][j]->supportsPageSize(page_size) && tlbs[i][j]->getAllocateOnMiss() && (!hit || (hit && hit_level > i)))\n    {\n#ifdef DEBUG_MMU\n      log_file << "[MMU] Allocating in TLB: Level = " << i << " Index = " << j << " with page size: " << page_size << " and VPN: " << (address >> page_size) << std::endl;\n#endif\n      tuple<bool, IntPtr, int> result;\n\n      result = tlbs[i][j]->allocate(address, time, count, lock, page_size, ppn_result);\n      if (get<0>(result) == true)\n      {\n        evicted_translations[i].push_back(make_tuple(get<1>(result), get<2>(result)));\n      }\n    }\n  }\n}\n'})}),"\n",(0,s.jsx)(t.h2,{id:"debugging",children:"Debugging"}),"\n",(0,s.jsxs)(t.p,{children:["The ",(0,s.jsx)(t.code,{children:"DEBUG_MMU"})," macro can be enabled to log detailed debug messages at various points in the address translation process to help understand and trace the steps involved. DO NOT ENABLE THIS MACRO WHEN YOU ARE RUNNING A SIMULATION. IT WILL SLOW DOWN THE SIMULATION AND GENERATE TONS OF DATA."]}),"\n",(0,s.jsx)(t.pre,{children:(0,s.jsx)(t.code,{className:"language-cpp",children:"\n\n"})})]})}function d(e={}){const{wrapper:t}={...(0,l.R)(),...e.components};return t?(0,s.jsx)(t,{...e,children:(0,s.jsx)(h,{...e})}):h(e)}},8453:(e,t,n)=>{n.d(t,{R:()=>i,x:()=>r});var a=n(6540);const s={},l=a.createContext(s);function i(e){const t=a.useContext(l);return a.useMemo((function(){return"function"==typeof e?e(t):{...t,...e}}),[t,e])}function r(e){let t;return t=e.disableParentContext?"function"==typeof e.components?e.components(s):e.components||s:i(e.components),a.createElement(l.Provider,{value:t},e.children)}}}]);