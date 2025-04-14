"use strict";(self.webpackChunkvirtuoso=self.webpackChunkvirtuoso||[]).push([[59],{8453:(t,e,n)=>{n.d(e,{R:()=>i,x:()=>l});var s=n(6540);const a={},r=s.createContext(a);function i(t){const e=s.useContext(r);return s.useMemo((function(){return"function"==typeof t?t(e):{...e,...t}}),[e,t])}function l(t){let e;return e=t.disableParentContext?"function"==typeof t.components?t.components(a):t.components||a:i(t.components),s.createElement(r.Provider,{value:e},t.children)}},9984:(t,e,n)=>{n.r(e),n.d(e,{assets:()=>d,contentTitle:()=>l,default:()=>g,frontMatter:()=>i,metadata:()=>s,toc:()=>o});const s=JSON.parse('{"id":"MMU Designs/mmu_rmm","title":"Range Mappings MMU Design","description":"In this section, we will discuss the implementation of the Range Mappings MMU design. The Range Mappings MMU design is based on the work by","source":"@site/docs/MMU Designs/mmu_rmm.md","sourceDirName":"MMU Designs","slug":"/MMU Designs/mmu_rmm","permalink":"/Virtuoso/docs/MMU Designs/mmu_rmm","draft":false,"unlisted":false,"editUrl":"https://github.com/facebook/docusaurus/tree/main/packages/create-docusaurus/templates/shared/docs/MMU Designs/mmu_rmm.md","tags":[],"version":"current","frontMatter":{},"sidebar":"tutorialSidebar","previous":{"title":"Part-of-Memory TLB (POM-TLB) MMU Design","permalink":"/Virtuoso/docs/MMU Designs/mmu_pomtlb"},"next":{"title":"Page Table Walker","permalink":"/Virtuoso/docs/MMU Designs/performing_ptw"}}');var a=n(4848),r=n(8453);const i={},l="Range Mappings MMU Design",d={},o=[{value:"New Components",id:"new-components",level:2},{value:"Address Translation Flow",id:"address-translation-flow",level:2},{value:"Range Walk",id:"range-walk",level:3},{value:"Finding my corresponding VMA",id:"finding-my-corresponding-vma",level:3}];function c(t){const e={a:"a",code:"code",h1:"h1",h2:"h2",h3:"h3",header:"header",li:"li",ol:"ol",p:"p",pre:"pre",strong:"strong",ul:"ul",...(0,r.R)(),...t.components};return(0,a.jsxs)(a.Fragment,{children:[(0,a.jsx)(e.header,{children:(0,a.jsx)(e.h1,{id:"range-mappings-mmu-design",children:"Range Mappings MMU Design"})}),"\n",(0,a.jsx)(e.p,{children:"In this section, we will discuss the implementation of the Range Mappings MMU design. The Range Mappings MMU design is based on the work by"}),"\n",(0,a.jsxs)(e.p,{children:["We will describe only the differences between the Range Mappings MMU design and the baseline MMU design. For a detailed description of the baseline MMU design, please refer to the ",(0,a.jsx)(e.a,{href:"./mmu_baseline.md",children:"Baseline MMU Design"})," section."]}),"\n",(0,a.jsx)(e.h2,{id:"new-components",children:"New Components"}),"\n",(0,a.jsxs)(e.ul,{children:["\n",(0,a.jsxs)(e.li,{children:[(0,a.jsx)(e.strong,{children:"Range Lookaside Buffer (RLB):"})," A range-based cache that stores base-bound pairs for fast address translation."]}),"\n",(0,a.jsxs)(e.li,{children:[(0,a.jsx)(e.strong,{children:"Range Table Walker:"})," A component that walks the range table to find the base-bound pairs for address translation and inserts them into the RLB."]}),"\n"]}),"\n",(0,a.jsx)(e.h2,{id:"address-translation-flow",children:"Address Translation Flow"}),"\n",(0,a.jsxs)(e.ol,{children:["\n",(0,a.jsxs)(e.li,{children:[(0,a.jsx)(e.strong,{children:"TLB Lookup:"})," The MMU first checks the TLB hierarchy for a translation. If a TLB hit occurs, the physical address is returned."]}),"\n",(0,a.jsxs)(e.li,{children:[(0,a.jsx)(e.strong,{children:"Range Walk:"})," If a TLB miss occurs, the MMU performs a range walk to check for a range mapping."]}),"\n"]}),"\n",(0,a.jsx)(e.pre,{children:(0,a.jsx)(e.code,{className:"language-cpp",children:'    auto range_walk_result = performRangeWalk(address, eip, lock, modeled, count);\n    range_latency = range_lb->get_latency().getLatency();\t\n\n\tif (get<1>(range_walk_result) != static_cast<IntPtr>(-1))\n\t\t\t{\n\t\t\t\trange_hit = true;\n\t\t\t\tIntPtr vpn_start = get<1>(range_walk_result)/4096;\n\t\t\t\tIntPtr ppn_offset = get<2>(range_walk_result);\n\n\t\t\t\tIntPtr current_vpn = address >> 12;\n\t\t\t\t\n\t\t\t\tppn_result = (current_vpn - vpn_start) + ppn_offset;\n\t\t\t\tpage_size = 12;\n#ifdef DEBUG_MMU\n\t\t\t\tlog_file << "[MMU] Range Hit: " << range_hit << " at time: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;\n\t\t\t\tlog_file << "[MMU] VPN Start: " << vpn_start << std::endl;\n\t\t\t\tlog_file << "[MMU] PPN Offset: " << ppn_offset << std::endl;\n\t\t\t\tlog_file << "[MMU] Current VPN: " << current_vpn << std::endl;\n\t\t\t\tlog_file << "[MMU] Final PPN: " << (current_vpn - vpn_start) + ppn_offset << std::endl;\n#endif\n\t\t\t\tif (count)\n\t\t\t\t{\n\t\t\t\t\ttranslation_stats.requests_resolved_by_rlb++;\n\t\t\t\t\ttranslation_stats.requests_resolved_by_rlb_latency += range_latency;\n\t\t\t\t\ttranslation_stats.total_range_walk_latency += range_latency;\n\t\t\t\t\ttranslation_stats.total_translation_latency += charged_tlb_latency;\n\t\t\t\t}\n\t\t\t\t// We progress the time by L1 TLB latency + range latency\n\t\t\t\t// This is done so that the PTW starts after the TLB latency and the range latency\n\n\t\t\t\tshmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time + tlb_latency[0] + range_latency); \n#ifdef DEBUG_MMU\n\t\t\t\tlog_file << "[MMU] New time after charging TLB and Range latency: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;\n#endif\n\t\t\t}\n'})}),"\n",(0,a.jsx)(e.h3,{id:"range-walk",children:"Range Walk"}),"\n",(0,a.jsxs)(e.p,{children:["The range walk is performed by the ",(0,a.jsx)(e.code,{children:"performRangeWalk"})," function and  returns a tuple containing the charged range walk latency, the virtual page number (VPN), and the offset."]}),"\n",(0,a.jsxs)(e.ul,{children:["\n",(0,a.jsx)(e.li,{children:"The function first checks if the address is present in the Range Lookaside Buffer (RLB). If it is a hit, it returns the VPN and offset."}),"\n",(0,a.jsx)(e.li,{children:"If it is a miss, it checks the Range Table for the address. If the address is found in the Range Table, it inserts the entry into the RLB and returns the VPN and offset."}),"\n",(0,a.jsx)(e.li,{children:"If the address is not found in the Range Table, it returns -1 for both VPN and offset."}),"\n",(0,a.jsx)(e.li,{children:"The function also charges the range walk latency based on the number of memory accesses required to traverse the range table."}),"\n",(0,a.jsx)(e.li,{children:"The accesses are serialized since the walk to the range table (e.g., the B+ tree) is serialized."}),"\n"]}),"\n",(0,a.jsx)(e.pre,{children:(0,a.jsx)(e.code,{className:"language-cpp",children:'\tstd::tuple<SubsecondTime, IntPtr, int> RangeMMU::performRangeWalk(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count)\n\t{\n\n\t\tSubsecondTime time = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);\n\n\t\tSubsecondTime charged_range_walk_latency = SubsecondTime::Zero();\n\t\tauto hit_rlb = range_lb->access(Core::mem_op_t::READ, address, count);\n\n\t\tRangeTable *range_table = Sim()->getMimicOS()->getRangeTable(core->getThread()->getAppId());\n\n\t\tif (!hit_rlb.first) // Miss in RLB\n\t\t{\n#ifdef DEBUG_MMU\n\t\t\tlog_file << "Miss in RLB for address: " << address << std::endl;\n#endif\n\t\t\t// Check if the address is in the range table\n\t\t\tauto result = range_table->lookup(address);\n\t\t\tif (get<0>(result) != NULL) // TreeNode* is not NULL\n\t\t\t{\n\t\t\t\t// We found the key inside the range table\n#ifdef DEBUG_MMU\n\t\t\t\tlog_file << "Key found for address: " << address << " in the range table" << std::endl;\n#endif\n\t\t\t\tRange range;\n\t\t\t\trange.vpn = get<0>(result)->keys[get<1>(result)].first;\n\t\t\t\trange.bounds = get<0>(result)->keys[get<1>(result)].second;\n\t\t\t\trange.offset = get<0>(result)->values[get<1>(result)].offset;\n\n#ifdef DEBUG_MMU\n\t\t\t\tlog_file << "VPN: " << range.vpn << " Bounds: " << range.bounds << " Offset: " << range.offset << std::endl;\n#endif\n\t\t\t\t// Insert the entry in the RLB\n\t\t\t\tfor (auto &address : get<2>(result))\n\t\t\t\t{\n\n\t\t\t\t\ttranslationPacket packet;\n\t\t\t\t\tpacket.address = address;\n\t\t\t\t\tpacket.eip = eip;\n\t\t\t\t\tpacket.instruction = false;\n\t\t\t\t\tpacket.lock_signal = lock;\n\t\t\t\t\tpacket.modeled = modeled;\n\t\t\t\t\tpacket.count = count;\n\t\t\t\t\tpacket.type = CacheBlockInfo::block_type_t::RANGE_TABLE;\n\n\t\t\t\t\tcharged_range_walk_latency += accessCache(packet, charged_range_walk_latency);\n\t\t\t\t}\n\t\t\t\trange_lb->insert_entry(range);\n\t\t\t}\n\t\t\telse // We did not find the key inside the range table\n\t\t\t{\n\t\t\n#ifdef DEBUG_MMU\n\t\t\t\tlog_file << "No key found for address: " << address << " in the range table" << std::endl;\n#endif\n\t\t\t\treturn std::make_tuple(charged_range_walk_latency, -1, -1);\n\t\t\t}\n\t\t\treturn std::make_tuple(charged_range_walk_latency, hit_rlb.second.vpn, hit_rlb.second.offset);\n\t\t}\n\t\telse\n\t\t{\n#ifdef DEBUG_MMU\n\t\t\tlog_file << "Hit in RLB for address: " << address << std::endl;\n\t\t\tlog_file << "VPN: " << hit_rlb.second.vpn << " Bounds: " << hit_rlb.second.bounds << " Offset: " << hit_rlb.second.offset << std::endl;\n#endif\n\t\t\treturn std::make_tuple(charged_range_walk_latency, hit_rlb.second.vpn, hit_rlb.second.offset);\n\t\t}\n\t}\n\n'})}),"\n",(0,a.jsx)(e.h3,{id:"finding-my-corresponding-vma",children:"Finding my corresponding VMA"}),"\n",(0,a.jsx)(e.pre,{children:(0,a.jsx)(e.code,{className:"language-cpp",children:'\tvoid RangeMMU::discoverVMAs()\n\t{\n\t\t// We need to discover the VMAs in the application\n\t}\n\n\tVMA RangeMMU::findVMA(IntPtr address)\n\t{\n\t\tint app_id = core->getThread()->getAppId();\n\t\tstd::vector<VMA> vma_list = Sim()->getMimicOS()->getVMA(app_id);\n\n\t\tfor (UInt32 i = 0; i < vma_list.size(); i++)\n\t\t{\n\t\t\tif (address >= vma_list[i].getBase() && address < vma_list[i].getEnd())\n\t\t\t{\n#ifdef DEBUG_MMU\n\t\t\t\tlog_file << "VMA found for address: " << address << " in VMA: " << vma_list[i].getBase() << " - " << vma_list[i].getEnd() << std::endl;\n#endif\n\t\t\t\treturn vma_list[i];\n\t\t\t}\n\t\t}\n\t\tassert(false);\n\t\treturn VMA(-1, -1);\n\t}\n\n'})})]})}function g(t={}){const{wrapper:e}={...(0,r.R)(),...t.components};return e?(0,a.jsx)(e,{...t,children:(0,a.jsx)(c,{...t})}):c(t)}}}]);