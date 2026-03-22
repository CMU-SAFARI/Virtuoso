// #include "subsecond_time.h"
// #include "memory_manager.h"
// #include <math.h> 
// #include <fstream>
// #include <stdlib.h>
// #include <time.h> 
// namespace ParametricDramDirectoryMSI{
//     ModrianMemoryManager::ModrianMemoryManager(int number_of_levels,Core* _core,ShmemPerfModel* _m_shmem_perf_model,int *level_percentages,int *level_bit_indices,int segments,PWC* pwc, bool pwc_enabled){
//         if(level_percentages!=NULL){
//             this->core =_core;
//             this->m_shmem_perf_model=_m_shmem_perf_model;
//             this->stats.number_of_levels=number_of_levels;
//             this->stats.hit_percentages=(int *)malloc(number_of_levels*sizeof(int));
//             this->stats.address_bit_indices=(int *)malloc((number_of_levels+1)*sizeof(int));
//             this->stats.segments=segments;
//             for (int i = 0; i < number_of_levels+1; i++)
//             {
//                 if(i<number_of_levels){
//                     this->stats.hit_percentages[i]=level_percentages[i];
//                 }
//                 this->stats.address_bit_indices[i]=level_bit_indices[i];
//             }
//             this->starting_table=InitiateTable((int)pow(2.0,(float)level_bit_indices[0]));
//         }
//         srand(time(NULL));
        
        
//     }
//     SubsecondTime ModrianMemoryManager::init_walk(
//         IntPtr eip,
//         IntPtr address,
//         CacheCntlr *_cache,
//         Core::lock_signal_t lock_signal,
//         Byte* data_buf, UInt32 data_length,
//         bool modeled,bool count)
//     {
//         cache=_cache;

//         uint64_t a1;
//         int shift_bits=0;
//         for (int i = stats.number_of_levels; i >= 1; i--)
//         {
//             shift_bits+=stats.address_bit_indices[i];
//         }
//         a1=((address>>shift_bits))&0x1ff;


//         SubsecondTime now=m_shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
//         IntPtr cache_address = ((IntPtr)(&starting_table->entries[a1])) & (~((64 - 1))); 
//         cache->processMemOpFromCore(eip, lock_signal,Core::mem_op_t::READ,cache_address,0,data_buf,data_length,modeled,false,CacheBlockInfo::block_type_t::SECURITY,SubsecondTime::Zero());
//         SubsecondTime end=m_shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
//         SubsecondTime latency=end-now;
//         m_shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD,now);
        
//         if(nuca){
//             nuca->markTranslationMetadata(cache_address,CacheBlockInfo::block_type_t::SECURITY);
//             mem_manager->getCache(MemComponent::component_t::L1_DCACHE)->markMetadata(cache_address,CacheBlockInfo::block_type_t::SECURITY);
//             mem_manager->getCache(MemComponent::component_t::L2_CACHE)->markMetadata(cache_address,CacheBlockInfo::block_type_t::SECURITY);
//         }
//         UInt64 vpn = address >> init_walk_functional(address);

//         if(starting_table->entries[a1].entry_type==table_entry_type::NONE){
//             starting_table->entries[a1]=*CreateNewEntryAtLevel(1,stats.number_of_levels,stats.hit_percentages,stats.address_bit_indices);
//         }
//         //Not Essential
//         if(starting_table->entries[a1].entry_type==table_entry_type::BITVECTOR){
//             return SubsecondTime::Zero();
//         }
//         else if(starting_table->entries[a1].entry_type==table_entry_type::RLE){
//             ComponentLatency range_latency=ComponentLatency(core->getDvfsDomain(),log(stats.segments));
//             return range_latency.getLatency();
//         }
//         //Not Essential
        
        
//         return latency+FindPermissionsRecursive(eip, address,2,starting_table->entries[a1].next_level_table,lock_signal,data_buf,data_length,modeled);
//     }
//     SubsecondTime ModrianMemoryManager::FindPermissionsRecursive(
//         IntPtr eip,
//         uint64_t address,
//         int level,table* new_table,
//         Core::lock_signal_t lock_signal,
//         Byte* data_buf, UInt32 data_length,
//         bool modeled)
//     {
//         uint64_t a1;
//         int shift_bits=0;
        
//         for (int i = stats.number_of_levels; i >= level; i--)
//         {
//             shift_bits+=stats.address_bit_indices[i];
//         }
//         a1=((address>>shift_bits))&0x1ff;
        

//         SubsecondTime now=m_shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
        
//         IntPtr cache_address = ((IntPtr)(&new_table->entries[a1])) & (~((64 - 1))); 
//         cache->processMemOpFromCore(eip,lock_signal,Core::mem_op_t::READ,cache_address,0,data_buf,data_length,modeled,false,CacheBlockInfo::block_type_t::SECURITY,SubsecondTime::Zero());
        
//         SubsecondTime end=m_shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
//         SubsecondTime latency=end-now;
//         m_shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD,now);
       
//         if(nuca){
//             nuca->markTranslationMetadata(cache_address,CacheBlockInfo::block_type_t::SECURITY);
//             mem_manager->getCache(MemComponent::component_t::L1_DCACHE)->markMetadata(cache_address,CacheBlockInfo::block_type_t::SECURITY);
//             mem_manager->getCache(MemComponent::component_t::L2_CACHE)->markMetadata(cache_address,CacheBlockInfo::block_type_t::SECURITY);
//         }
//         UInt64 vpn = address >> init_walk_functional(address);

//         if(new_table->entries[a1].entry_type==table_entry_type::NONE){
            
//             new_table->entries[a1]=*CreateNewEntryAtLevel(level,stats.number_of_levels,stats.hit_percentages,stats.address_bit_indices);
//         }
//         if(new_table->entries[a1].entry_type==table_entry_type::BITVECTOR){
//             //std::cout<<std::hex<<address<<" - "<<std::hex<<a1<<" - "<<level<<" BitVector\n";
//             SubsecondTime now_bv=m_shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
        
//             IntPtr cache_address_bv = ((IntPtr)(&new_table->entries[a1].bitvector[(address>>stats.address_bit_indices[level+1])&0x1ff])) & (~((64 - 1))); 
//             cache->processMemOpFromCore(eip, lock_signal,Core::mem_op_t::READ,cache_address_bv,0,data_buf,data_length,modeled,false,CacheBlockInfo::block_type_t::SECURITY,SubsecondTime::Zero());
            
//             SubsecondTime end_bv=m_shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
//             SubsecondTime latency_bv=end_bv-now_bv;
//             m_shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD,now_bv);
//             if(nuca){
//                 nuca->markTranslationMetadata(cache_address,CacheBlockInfo::block_type_t::SECURITY);
//                 mem_manager->getCache(MemComponent::component_t::L1_DCACHE)->markMetadata(cache_address,CacheBlockInfo::block_type_t::SECURITY);
//                 mem_manager->getCache(MemComponent::component_t::L2_CACHE)->markMetadata(cache_address,CacheBlockInfo::block_type_t::SECURITY);
//             }
//             UInt64 vpn = address >> init_walk_functional(address);
//             return latency_bv;
//         }
//         else if(new_table->entries[a1].entry_type==table_entry_type::RLE){
//             //std::cout<<std::hex<<address<<" - "<<std::hex<<a1<<" - "<<level<<" RLE\n";
//             ComponentLatency range_latency=ComponentLatency(core->getDvfsDomain(),(int)log(stats.segments));
            
//             SubsecondTime latency_rle=range_latency.getLatency();
//             return latency_rle;
//         }
        
//         return latency+FindPermissionsRecursive(eip, address,level+1,new_table->entries[a1].next_level_table,lock_signal,data_buf,data_length,modeled);
        
//     }
//     int ModrianMemoryManager::init_walk_functional(IntPtr address){

//         uint64_t a1;
//         int shift_bits=0;
//         for (int i = stats.number_of_levels; i >= 1; i--)
//         {
//             shift_bits+=stats.address_bit_indices[i];
//         }
//         a1=((address>>shift_bits))&0x1ff;


//         if(starting_table->entries[a1].entry_type==table_entry_type::NONE){
//             return 0;
//         }
//         //Not Essential
//         if(starting_table->entries[a1].entry_type==table_entry_type::BITVECTOR){
//             return shift_bits;
//         }
//         else if(starting_table->entries[a1].entry_type==table_entry_type::RLE){
            
//             return shift_bits;
//         }
//         //Not Essential
        
        
//         return init_walk_functional_rec(address,2,starting_table->entries[a1].next_level_table);
//     }
//     int ModrianMemoryManager::init_walk_functional_rec(uint64_t address,
//     int level,table* new_table){
//         uint64_t a1;
//         int shift_bits=0;
        
//         for (int i = stats.number_of_levels; i >= level; i--)
//         {
//             shift_bits+=stats.address_bit_indices[i];
//         }
//         a1=((address>>shift_bits))&0x1ff;
//         if(new_table->entries[a1].entry_type==table_entry_type::NONE){
//             return 0;
            
//         }
//         if(new_table->entries[a1].entry_type==table_entry_type::BITVECTOR){
//             return shift_bits;
//         }
//         else if(new_table->entries[a1].entry_type==table_entry_type::RLE){
//             return shift_bits;
//         }
        
//         return init_walk_functional_rec(address,level+1,new_table->entries[a1].next_level_table);
        
//     }
//     bool ModrianMemoryManager::isPageFault(IntPtr address){

//         uint64_t a1;
//         int shift_bits=0;
//         for (int i = stats.number_of_levels; i >= 1; i--)
//         {
//             shift_bits+=stats.address_bit_indices[i];
//         }
//         a1=((address>>shift_bits))&0x1ff;


//         if(starting_table->entries[a1].entry_type==table_entry_type::NONE){
//             return true;
//         }
//         //Not Essential
//         if(starting_table->entries[a1].entry_type==table_entry_type::BITVECTOR){
//             return false;
//         }
//         else if(starting_table->entries[a1].entry_type==table_entry_type::RLE){
            
//             return false;
//         }
//         //Not Essential
        
        
//         return isPageFault_rec(address,2,starting_table->entries[a1].next_level_table);
//     }
//     bool ModrianMemoryManager::isPageFault_rec(uint64_t address,
//     int level,table* new_table){
//         uint64_t a1;
//         int shift_bits=0;
        
//         for (int i = stats.number_of_levels; i >= level; i--)
//         {
//             shift_bits+=stats.address_bit_indices[i];
//         }
//         a1=((address>>shift_bits))&0x1ff;
//         if(new_table->entries[a1].entry_type==table_entry_type::NONE){
            
//             return true;
//         }
//         if(new_table->entries[a1].entry_type==table_entry_type::BITVECTOR){
//             return false;
//         }
//         else if(new_table->entries[a1].entry_type==table_entry_type::RLE){
//             return false;
//         }
        
//         return isPageFault_rec(address,level+1,new_table->entries[a1].next_level_table);
        
//     }
//     /*int main(int argc, char *argv[]){
//         int per[]={0,2,28,100};
//         int ind[]={9,9,9,9,12};
//         ModrianMemoryManager mmm(4,per,ind,8);
//         srand (time(NULL));
//         uint64_t n;
//         std::ifstream in_file;
//         in_file.open (argv[1]);
        
//         while (!in_file.eof())
//         {
//             in_file>>n;
//             int r=mmm.FindPermissions(n);
//         }
        
        
//     }*/
// }