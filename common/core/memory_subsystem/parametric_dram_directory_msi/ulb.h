#ifndef ULB_H
#define ULB_H

#include "fixed_types.h"
#include "cache.h"
#include <util.h>
#include <unordered_map>
#include "lock.h"
#include <vector>
#include "utopia.h"

namespace ParametricDramDirectoryMSI
{
  class ULB
  {
  private:
    static UInt32 SIM_PAGE_SHIFT; // 4KB
    static IntPtr SIM_PAGE_SIZE;
    static IntPtr SIM_PAGE_MASK;

    UInt64 m_access, m_miss, m_allocations, m_miss_inRestSeg;
    core_id_t m_core_id;

    Cache m_cache;

    Utopia *m_utopia;
    RestSeg *m_RestSeg_4KB;
    RestSeg *m_RestSeg_2MB;

  public:
    enum where_t
    {
      HIT = 0,
      MISS
    };

    ComponentLatency access_latency;
    ComponentLatency miss_latency;

    ULB(String name, String cfgname, core_id_t core_id, UInt32 page_size, UInt32 num_entries, UInt32 associativity, ComponentLatency access_latency, ComponentLatency miss_latency, int *page_size_list, int page_sizes);
    ULB::where_t lookup(IntPtr address, SubsecondTime now, bool allocate_on_miss, bool count);
    void allocate(IntPtr address, SubsecondTime now, Cache *ulb_cache, int pagesize);
  };
}

#endif // ULB_H
