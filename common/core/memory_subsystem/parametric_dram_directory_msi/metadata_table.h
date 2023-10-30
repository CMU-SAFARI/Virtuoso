#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "utopia_cache_template.h"
#include "pagetable_walker.h"
#include <stdint.h>
namespace ParametricDramDirectoryMSI{
    class XMemManager{
        public:
            int size;
            int granularity;
            char* atom_table;
            UtopiaCache *buffer;
            CacheCntlr *cache;
            ShmemPerfModel* m_shmem_perf_model;
            CacheCntlr *cache_ctrl;
            NucaCache* nuca;
            MemoryManager* mem_manager;
            SubsecondTime init_walk(IntPtr eip, IntPtr address, CacheCntlr *cache, Core::lock_signal_t lock_signal, Byte* _data_buf, UInt32 _data_length, bool modeled, bool count);
            XMemManager(int size,int granularity,int cache_size,int cache_associativity,int cache_hit_latency,int cache_miss_latency,Core* core,ShmemPerfModel* m_shmem_perf_model,PWC* _pwc, bool _page_walk_cache_enabled);
            void setNucaCache(NucaCache* nuca2){ nuca = nuca2;}
		    void setMemoryManager(MemoryManager* _mem_manager){ mem_manager = _mem_manager;}
    };

}