// #include "cache_cntlr.h"
// #include "subsecond_time.h"
// #include "utopia_cache_template.h"
// #include "memory_manager.h"
// #include <math.h> 
// #include <fstream>
// #include <stdlib.h>
// #include <time.h> 
// namespace ParametricDramDirectoryMSI{
//     XMemManager::XMemManager(int size,int granularity,int cache_size,int cache_associativity,int cache_hit_latency,int cache_miss_latency,Core* _core,ShmemPerfModel* _m_shmem_perf_model,PWC* _pwc, bool _page_walk_cache_enabled)
//     {
//         this->granularity=granularity;
//         if(_core!=NULL){
//             this->atom_table=(char*)malloc(sizeof(char)*1024*((size*1024*1024)/granularity));
//             this->m_shmem_perf_model=_m_shmem_perf_model;
//             this->buffer=new UtopiaCache("xmem_cache", "xmem_cache", _core->getId(),(int)log2(granularity), cache_size, cache_associativity, ComponentLatency(_core->getDvfsDomain(), cache_hit_latency), ComponentLatency(_core->getDvfsDomain(),cache_miss_latency));
//         }       

        
//     }
//     SubsecondTime XMemManager::init_walk(IntPtr eip, IntPtr address,
//     CacheCntlr *_cache,
//     Core::lock_signal_t lock_signal,
//     Byte* data_buf, UInt32 data_length,
//      bool modeled, bool count){
//         SubsecondTime s1=m_shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
//         UtopiaCache::where_t result  =buffer->lookup(address,s1,true,false);
//         if(result == UtopiaCache::HIT){

//             SubsecondTime e1=m_shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
//             m_shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD,s1);
//             return e1-s1;
//         }
//         else{
            
//             int index=address>>((int)log2(granularity));
//             SubsecondTime now=m_shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
//             IntPtr cache_address = ((IntPtr)(&atom_table[index])) & (~((64 - 1))); 
//             _cache->processMemOpFromCore(eip, lock_signal,Core::mem_op_t::READ,cache_address,0,data_buf,data_length,modeled,false,CacheBlockInfo::block_type_t::EXPRESSIVE,SubsecondTime::Zero());
//             SubsecondTime end=m_shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
//             SubsecondTime latency=end-now;
//             m_shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD,s1);

//             if(nuca){
                
//                 nuca->markTranslationMetadata(cache_address,CacheBlockInfo::block_type_t::EXPRESSIVE);
//                 mem_manager->getCache(MemComponent::component_t::L1_DCACHE)->markMetadata(cache_address,CacheBlockInfo::block_type_t::EXPRESSIVE);
//                 mem_manager->getCache(MemComponent::component_t::L2_CACHE)->markMetadata(cache_address,CacheBlockInfo::block_type_t::EXPRESSIVE);
//             }
//             return latency;
//         }
        
//     }
// }