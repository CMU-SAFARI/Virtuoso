#ifndef UTOPIA_PERM_H
#define UTOPIA_PERM_H

#include "fixed_types.h"
#include "cache.h"
#include <util.h>
#include <unordered_map>
#include <unordered_set>
#include "lock.h"
#include <vector>

namespace ParametricDramDirectoryMSI
{
  class UtopiaCache
  {
  private:


    UInt64 m_access, m_miss;
    UInt64 m_unique_cache_lines;  // Track unique cache lines accessed
    core_id_t m_core_id;
    ComponentLatency access_latency;
    ComponentLatency miss_latency;

    Cache m_cache;
    CacheCntlr* m_next_level;
    
    std::unordered_set<IntPtr> m_unique_lines_set;  // Set of unique cache line addresses

  public:

    enum where_t
      {
        HIT = 0,
        MISS
      };

    UtopiaCache(String name, String cfgname, core_id_t core_id,UInt32 page_size, UInt32 num_entries, UInt32 associativity, ComponentLatency access_latency, ComponentLatency miss_latency);
    UtopiaCache::where_t lookup(IntPtr address, SubsecondTime now, bool allocate_on_miss , bool count);
    void allocate(IntPtr address, SubsecondTime now, Cache *Utopia_cache);
    void setNextLevel(CacheCntlr* next_level){ m_next_level = next_level;};
    
    /// @brief Invalidate an entry from the cache
    /// @param address The address to invalidate (FPA address or TAR tag)
    /// @return true if the entry was found and invalidated, false otherwise
    bool invalidate(IntPtr address);
 
  };
}

#endif // ULB_H
