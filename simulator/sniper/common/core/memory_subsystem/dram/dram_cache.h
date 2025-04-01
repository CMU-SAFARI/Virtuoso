#ifndef __DRAM_CACHE
#define __DRAM_CACHE

#include "dram_cntlr_interface.h"
#include "subsecond_time.h"
#include "cache.h"
#include "contention_model.h"

class QueueModel;
class Prefetcher;

class DramCache : public DramCntlrInterface
{
   public:
      DramCache(MemoryManagerBase* memory_manager, ShmemPerfModel* shmem_perf_model, AddressHomeLookup* home_lookup, UInt32 cache_block_size, DramCntlrInterface *dram_cntlr);
      ~DramCache();

      virtual boost::tuple<SubsecondTime, HitWhere::where_t> getDataFromDram(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf,bool is_matadata);
      virtual boost::tuple<SubsecondTime, HitWhere::where_t> putDataToDram(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now,bool is_matadata);

   private:
      core_id_t m_core_id;
      UInt32 m_cache_block_size;
      SubsecondTime m_data_access_time;
      SubsecondTime m_tags_access_time;
      ComponentBandwidth m_data_array_bandwidth;

      AddressHomeLookup* m_home_lookup;
      DramCntlrInterface* m_dram_cntlr;
      Cache* m_cache;
      QueueModel* m_queue_model;
      Prefetcher* m_prefetcher;
      bool m_prefetch_on_prefetch_hit;
      ContentionModel m_prefetch_mshr;

      UInt64 m_reads, m_writes;
      UInt64 m_read_misses, m_write_misses;
      UInt64 m_hits_prefetch, m_prefetches;
      SubsecondTime m_prefetch_mshr_delay;

      std::pair<bool, SubsecondTime> doAccess(Cache::access_t access, IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf);
      void insertLine(Cache::access_t access, IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now);
      SubsecondTime accessDataArray(Cache::access_t access, core_id_t requester, SubsecondTime t_start, ShmemPerf *perf);
      void callPrefetcher(IntPtr address, bool cache_hit, bool prefetch_hit, SubsecondTime t_issue);
};

#endif // __DRAM_CACHE
