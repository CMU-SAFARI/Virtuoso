#include "ulb.h"
#include "stats.h"
#include "config.hpp"
#include <cmath>
#include <iostream>
#include <utility>
#include "core_manager.h"
#include "cache_set.h"
#include "virtuos.h"

namespace ParametricDramDirectoryMSI
{

  UInt32 ULB::SIM_PAGE_SHIFT;
  IntPtr ULB::SIM_PAGE_SIZE;
  IntPtr ULB::SIM_PAGE_MASK;

  ULB::ULB(String name, String cfgname, core_id_t core_id, UInt32 page_size, UInt32 num_entries, UInt32 associativity, ComponentLatency _access_latency, ComponentLatency _miss_latency, int *page_size_list, int page_sizes)
      : m_core_id(core_id), access_latency(_access_latency), miss_latency(_miss_latency), m_cache(name, cfgname, core_id, num_entries / associativity, associativity, (1L << page_size), "lru", CacheBase::PR_L1_CACHE, CacheBase::HASH_MASK,
                                                                                                  NULL,
                                                                                                  NULL, true, page_size_list, page_sizes) // Assuming 8B granularity

  {

    registerStatsMetric(name, core_id, "access", &m_access);
    registerStatsMetric(name, core_id, "miss", &m_miss);
    registerStatsMetric(name, core_id, "miss_inRestSeg", &m_miss_inRestSeg);
    registerStatsMetric(name, core_id, "allocations", &m_allocations);

    m_utopia = Sim()->getVirtuOS()->getUtopia();
    m_RestSeg_4KB = m_utopia->getRestSeg(0);
    m_RestSeg_2MB = m_utopia->getRestSeg(1);
  }

  ULB::where_t ULB::lookup(IntPtr address, SubsecondTime now, bool allocate_on_miss, bool count)
  {

    bool hit;
    hit = m_cache.accessSingleLineTLB(address, Cache::LOAD, NULL, 0, now, true);
    if (count)
      m_access++;

    bool inRestSeg = false;
    bool inRestSeg_4KB = false;
    bool inRestSeg_2MB = false;

    if (m_RestSeg_4KB->inRestSegnostats(address, count, now, m_core_id))
    {
      inRestSeg = true;
      inRestSeg_4KB = true;
    }
    else if (m_RestSeg_2MB->inRestSegnostats(address, count, now, m_core_id))
    {
      inRestSeg = true;
      inRestSeg_2MB = true;
    }
    else
      inRestSeg = false;

    if (hit)
      return ULB::HIT;
    else
    {
      // std::cout << " Miss in ULB but data is in RestSeg of 4KB: " <<  inRestSeg_4KB << " Miss in ULB but data is in RestSeg of 2MB: " << inRestSeg_2MB << std::endl;
      if (count)
      {

        if (inRestSeg)
          m_miss_inRestSeg++;
        else
          m_miss++;
      }
      if (allocate_on_miss && inRestSeg)
      {
        if (inRestSeg_4KB)
          allocate(address, now, &m_cache, 12); // if inRestSeg  == false, virtual-to-physical mapping will probably be stored in L1 TLB, so we dont need to allocate
        else if (inRestSeg_2MB)
          allocate(address, now, &m_cache, 21); // if inRestSeg  == false, virtual-to-physical mapping will probably be stored in L1 TLB, so we dont need to allocate
        m_allocations++;
      }
      return ULB::MISS;
    }
  }

  void ULB::allocate(IntPtr address, SubsecondTime now, Cache *ulb_cache, int page_size)
  {
    bool eviction;
    IntPtr evict_addr;
    CacheBlockInfo evict_block_info;

    IntPtr tag;
    UInt32 set_index;
    ulb_cache->splitAddressTLB(address, tag, set_index, page_size);
    ulb_cache->insertSingleLine(address, NULL, &eviction, &evict_addr, &evict_block_info, NULL, now);
  }

}
