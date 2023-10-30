
// #include "page_table_virtualized.h"
// #include <vector>
// #include "config.hpp"
// #include "tlb.h"
// #include "memory_manager.h"
// PageTableVirtualized::PageTableVirtualized(int number_of_levels,
//                         Core* _core,
//                         ShmemPerfModel* _m_shmem_perf_model,
//                         int *level_bit_indices,
//                         int *level_percentages,
//                         PWC* pwc,
//                         bool pwc_enabled,
//                         UtopiaCache* _shadow_cache):

//                         PageTableWalker(_core->getId(), 0, _m_shmem_perf_model, pwc, pwc_enabled),
//                         m_nested_tlb_access_latency(NULL, 0),
//                         m_tlb_l1_cache_access(NULL, 0),
//                         m_tlb_l2_cache_access(NULL, 0),
//                         m_tlb_nuca_cache_access(NULL, 0)
// {

//    ptw_radix_guest = new PageTableWalkerRadix(number_of_levels, _core, _m_shmem_perf_model, level_bit_indices, level_percentages, pwc, pwc_enabled, _shadow_cache);
//    ptw_radix_host  = new PageTableWalkerRadix(number_of_levels, _core, _m_shmem_perf_model, level_bit_indices, level_percentages, NULL, false, _shadow_cache);

//    bool m_utopia_enabled = Sim()->getCfg()->getBool("perf_model/utopia/enabled");
//    bool track_misses =  Sim()->getCfg()->getBool("perf_model/tlb/track_misses");
//    bool track_accesses = Sim()->getCfg()->getBool("perf_model/tlb/track_accesses");
//    int m_system_page_size = Sim()->getCfg()->getInt("perf_model/tlb/pagesize");
//    int number_of_page_sizes = Sim()->getCfg()->getInt("perf_model/tlb/page_sizes");
//    int* page_size_list;
//    page_size_list = (int *) malloc (sizeof(int)*(number_of_page_sizes));
//    int nested_size = Sim()->getCfg()->getInt("perf_model/nested_tlb/size");
//    m_tlb_l1_cache_access =  ComponentLatency(_core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/l1_dcache/data_access_time"));;
//    m_tlb_l2_cache_access = ComponentLatency(_core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/l2_cache/data_access_time"));
//    m_tlb_nuca_cache_access = ComponentLatency(_core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/nuca/data_access_time"));

//    m_nested_tlb_access_latency = ComponentLatency(_core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/nested_tlb/latency"));

//    nested_tlb = new TLB("nested_tlb", "perf_model/nested_tlb", _core->getId(),m_shmem_perf_model, nested_size,m_system_page_size, Sim()->getCfg()->getInt("perf_model/nested_tlb/associativity"), NULL, m_utopia_enabled,  track_misses, track_accesses,page_size_list,  number_of_page_sizes, ptw_radix_host);

// }

// SubsecondTime PageTableVirtualized::init_walk(IntPtr eip, IntPtr address,
//         UtopiaCache* shadow_cache,
//         CacheCntlr *_cache,
//         Core::lock_signal_t lock_signal,
//         Byte* data_buf, UInt32 data_length,
//         bool modeled, bool count){

//         // if the address is in the guest address space, use the guest page table walker

//         std::vector<UInt64> guest_pt_addresses;
//         SubsecondTime guest_latency = SubsecondTime::Zero();
//         SubsecondTime host_latency = SubsecondTime::Zero();
//         guest_latency = ptw_radix_guest->init_walk(eip, address, shadow_cache, _cache, lock_signal, data_buf, data_length, modeled, count);
//         guest_pt_addresses = ptw_radix_guest->getAddresses();

//         //iterate over guest_pt_addresses and execute an init_walk using the host page table walker for every address

//         for (int i = 0; i < guest_pt_addresses.size(); i++){
//             TLB::where_t hit = nested_tlb->lookup(address, getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_USER_THREAD), true, 1, count, lock_signal);

//             if (hit == TLB::where_t::MISS && modeled){

//                 host_latency +=  m_nested_tlb_access_latency.getLatency() + ptw_radix_host->init_walk(eip, guest_pt_addresses[i], shadow_cache, _cache, lock_signal, data_buf, data_length, modeled, count);

//             }
//             else if(hit == TLB::where_t::L1_CACHE){ //
//                 host_latency +=  m_nested_tlb_access_latency.getLatency()+m_tlb_l1_cache_access.getLatency();
//             }
//             else if(hit == TLB::where_t::L2_CACHE){
//                 host_latency +=  m_nested_tlb_access_latency.getLatency()+m_tlb_l2_cache_access.getLatency();

//             }
//             else if(hit == TLB::where_t::NUCA_CACHE){
//                 host_latency +=  m_nested_tlb_access_latency.getLatency()+m_tlb_nuca_cache_access.getLatency();
//             }
//             else if(hit == TLB::where_t::L1){
//                 host_latency +=  m_nested_tlb_access_latency.getLatency();

//             }

//         }

//     return guest_latency + host_latency;

// }

// int PageTableVirtualized::init_walk_functional(IntPtr address)
// {

//     return  ptw_radix_guest->init_walk_functional(address);
// }

// bool PageTableVirtualized::isPageFault(IntPtr address)
// {
//     return ptw_radix_guest->isPageFault(address);
// }
