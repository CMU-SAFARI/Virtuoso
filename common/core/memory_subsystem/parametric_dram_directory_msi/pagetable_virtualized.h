// //create class that inherits from page table walker
// #include "pagetable_walker_radix.h"
// #include "pagetable_walker.h"
// #include "tlb.h"

// namespace ParametricDramDirectoryMSI
// {
//    class PageTableVirtualized : public PageTableWalker
//    {

//    private:
//          PageTableWalkerRadix *ptw_radix_host;
//          PageTableWalkerRadix *ptw_radix_guest;
//          TLB* nested_tlb;
//          ComponentLatency m_nested_tlb_access_latency;

//          ComponentLatency m_tlb_l1_cache_access;
//          ComponentLatency m_tlb_l2_cache_access; 
//          ComponentLatency m_tlb_nuca_cache_access;
   
//    public:

//       PageTableVirtualized(int number_of_levels,
//                            Core* _core, 
//                            ShmemPerfModel* _m_shmem_perf_model, 
//                            int *level_bit_indices,
//                            int *level_percentages, 
//                            PWC* pwc, 
//                            bool pwc_enabled,
//                            UtopiaCache* _shadow_cache);
      


//       ~PageTableVirtualized(){
//          delete ptw_radix_guest;
//          delete ptw_radix_host;
//       }


//          SubsecondTime init_walk(IntPtr eip, IntPtr address,
//                                  UtopiaCache* shadow_cache,
//                                  CacheCntlr *_cache,
//                                  Core::lock_signal_t lock_signal,
//                                  Byte* data_buf, UInt32 data_length,
//                                  bool modeled, bool count);

//          int init_walk_functional(IntPtr address);
//          bool isPageFault(IntPtr address);
//          void setMemoryManager(ParametricDramDirectoryMSI::MemoryManager* _memory_manager) override {
//          ptw_radix_guest->setMemoryManager(_memory_manager);
//          ptw_radix_host->setMemoryManager(_memory_manager);
//       }
//    };

// };