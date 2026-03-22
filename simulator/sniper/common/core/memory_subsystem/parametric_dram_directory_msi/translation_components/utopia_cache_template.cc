#include "utopia_cache_template.h"
#include "stats.h"
#include "config.hpp"
#include <cmath>
#include <iostream>
#include <utility>
#include "core_manager.h"
#include "cache_set.h"

namespace ParametricDramDirectoryMSI
{
  
         
  UtopiaCache::UtopiaCache(String name, String cfgname, core_id_t core_id, UInt32 block_size, UInt32 size, UInt32 associativity,ComponentLatency _access_latency, ComponentLatency _miss_latency)
    : m_core_id(core_id)
    , access_latency(_access_latency)
    , miss_latency(_miss_latency)
    , m_cache(name , cfgname, core_id, k_KILO * size / (associativity * block_size), associativity, block_size, "lru", CacheBase::PR_L1_CACHE) // Assuming 8B granularity
 
  {
    std::cout << "[MMU:UTOPIA]" << "Instantiating " << name << " with sets: " <<  k_KILO * size / (associativity * 8) << std::endl;
    m_unique_cache_lines = 0;
    registerStatsMetric(name, core_id, "access", &m_access);
    registerStatsMetric(name, core_id, "miss", &m_miss);
    registerStatsMetric(name, core_id, "unique_cache_lines", &m_unique_cache_lines);
  }

  UtopiaCache::where_t UtopiaCache::lookup(IntPtr address, SubsecondTime now, bool allocate_on_miss, bool count)
  {
          bool hit;
          hit = m_cache.accessSingleLine(address, Cache::LOAD, NULL, 0, now, true);

          // Track unique cache lines (align to 64-byte cache line boundary)
          IntPtr cache_line_addr = address & ~(IntPtr)63;
          if (count && m_unique_lines_set.find(cache_line_addr) == m_unique_lines_set.end()) {
              m_unique_lines_set.insert(cache_line_addr);
              m_unique_cache_lines = m_unique_lines_set.size();
          }

          //std::cout << "Accessing xmem cache: " << (hit ? "HIT" : "MISS" )<< std::endl;
          if(count) m_access++;
          if (hit) return UtopiaCache::where_t::HIT;
          else{
              if(count) m_miss++;
              if (allocate_on_miss) allocate(address, now, &m_cache);
              return UtopiaCache::where_t::MISS;
          }
  }

  void UtopiaCache::allocate(IntPtr address, SubsecondTime now, Cache *Utopia_cache)
  {
    bool eviction;
    IntPtr evict_addr;
    CacheBlockInfo evict_block_info;

    IntPtr tag;
    UInt32 set_index;
    Utopia_cache->splitAddress(address, tag, set_index);
    Utopia_cache->insertSingleLine(address, NULL, &eviction, &evict_addr, &evict_block_info, NULL, now);
  }

  bool UtopiaCache::invalidate(IntPtr address)
  {
    // Invalidate the cache entry for the given address
    // This is called during migration to ensure stale entries don't cause incorrect translations
    return m_cache.invalidateSingleLine(address);
  }

}
