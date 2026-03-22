// #include "entry_types.h"
// //#include "cache_cntlr.h"
// #include "subsecond_time.h"
// #include "fixed_types.h"
// #include "nuca_cache.h"

// #include <stdint.h>
// #define CACHE_ACCESS_LATENCY 1;
// namespace ParametricDramDirectoryMSI{
//     class ModrianMemoryManager{
//         public:
//             struct{
//                 int number_of_levels;
//                 int *address_bit_indices;
//                 int *hit_percentages;
//                 int segments;
//             }stats;
//             NucaCache* nuca;
//             MemoryManager* mem_manager;
//             Core* core;
//             CacheCntlr *cache;
//             ModrianMemoryManager(int number_of_levels,Core* _core,ShmemPerfModel* _m_shmem_perf_model,int *level_percentages,int *level_bit_indices,int segments,PWC* pwc, bool pwc_enabled);
//             table* starting_table;
//             ShmemPerfModel* m_shmem_perf_model;
//             SubsecondTime init_walk(IntPtr eip, IntPtr address,CacheCntlr *_cache,Core::lock_signal_t lock_signal,Byte* data_buf, UInt32 data_length,bool modeled,bool count);
//             SubsecondTime FindPermissionsRecursive(IntPtr eip, IntPtr address,int level,table* new_table,Core::lock_signal_t lock_signal,Byte* data_buf, UInt32 data_length,bool modeled);
//             int init_walk_functional(IntPtr address);
//             int init_walk_functional_rec(uint64_t address,int level,table* new_table);
//             bool isPageFault(IntPtr address);
//             bool isPageFault_rec(uint64_t address,int level,table* new_table);
//             void setNucaCache(NucaCache* nuca2){ nuca = nuca2;}
// 		    void setMemoryManager(MemoryManager* _mem_manager){ mem_manager = _mem_manager;}
//     };
// }