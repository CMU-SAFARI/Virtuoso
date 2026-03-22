#pragma once

#include "fixed_types.h"
#include "subsecond_time.h"
#include "hit_where.h"
#include "core.h"
#include "lock.h"
#include "stats.h"
#include "log.h"
#include "dvfs_manager.h"
#include "cache_block_info.h"
#include "mem_component.h"
#include "shmem_perf_model.h"
#include "mmu_cache_interface.h"
#include "dram_cntlr_interface.h"
#include "sim_log.h"

#include <unordered_map>

// Forward declarations
class ShmemPerf;

namespace FastDetailed
{

/**
 * @brief Result of a cache access: where it hit and the latency.
 */
struct CacheAccessResult
{
   SubsecondTime latency;
   HitWhere::where_t hit_where;
};

// ============================================================================
// CacheSet — LRU tag array set (configurable associativity)
// ============================================================================
class CacheSet
{
private:
   UInt32 m_assoc;
   IntPtr *m_tags;
   bool *m_dirty;
   UInt64 *m_lru;
   UInt64 m_lru_max;

public:
   CacheSet(UInt32 assoc)
       : m_assoc(assoc), m_lru_max(0)
   {
      m_tags = new IntPtr[assoc]();
      m_dirty = new bool[assoc]();
      m_lru = new UInt64[assoc]();
      for (UInt32 i = 0; i < assoc; ++i)
      {
         m_tags[i] = ~(IntPtr)0; // Invalid
         m_dirty[i] = false;
         m_lru[i] = 0;
      }
   }

   ~CacheSet()
   {
      delete[] m_tags;
      delete[] m_dirty;
      delete[] m_lru;
   }

   // Prevent copying
   CacheSet(const CacheSet &) = delete;
   CacheSet &operator=(const CacheSet &) = delete;

   // Move constructors for use in std::vector
   CacheSet(CacheSet &&o) noexcept
       : m_assoc(o.m_assoc), m_tags(o.m_tags), m_dirty(o.m_dirty),
         m_lru(o.m_lru), m_lru_max(o.m_lru_max)
   {
      o.m_tags = nullptr;
      o.m_dirty = nullptr;
      o.m_lru = nullptr;
   }

   CacheSet &operator=(CacheSet &&o) noexcept
   {
      if (this != &o)
      {
         delete[] m_tags;
         delete[] m_dirty;
         delete[] m_lru;
         m_assoc = o.m_assoc;
         m_tags = o.m_tags;
         m_dirty = o.m_dirty;
         m_lru = o.m_lru;
         m_lru_max = o.m_lru_max;
         o.m_tags = nullptr;
         o.m_dirty = nullptr;
         o.m_lru = nullptr;
      }
      return *this;
   }

   /**
    * @brief Look up a tag in this set.
    * @param tag The tag to find.
    * @param is_write Whether this access is a write (marks dirty).
    * @param evicted_tag [out] If miss, the evicted tag.
    * @param evicted_dirty [out] If miss and line was dirty.
    * @return true if hit, false if miss (eviction performed).
    */
   bool access(IntPtr tag, bool is_write, IntPtr &evicted_tag, bool &evicted_dirty)
   {
      // Search for hit
      for (UInt32 idx = 0; idx < m_assoc; ++idx)
      {
         if (m_tags[idx] == tag)
         {
            m_lru[idx] = ++m_lru_max;
            if (is_write)
               m_dirty[idx] = true;
            return true;
         }
      }
      // Miss — find LRU victim
      UInt64 lru_min = UINT64_MAX;
      UInt32 idx_min = 0;
      for (UInt32 idx = 0; idx < m_assoc; ++idx)
      {
         if (m_lru[idx] < lru_min)
         {
            lru_min = m_lru[idx];
            idx_min = idx;
         }
      }
      evicted_tag = m_tags[idx_min];
      evicted_dirty = m_dirty[idx_min];
      m_tags[idx_min] = tag;
      m_dirty[idx_min] = is_write;
      m_lru[idx_min] = ++m_lru_max;
      return false;
   }
};

// ============================================================================
// FastDetailedCacheBase — abstract base for cache levels
// ============================================================================
class FastDetailedCacheBase
{
public:
   virtual ~FastDetailedCacheBase() {}
   virtual CacheAccessResult access(Core::mem_op_t mem_op_type, IntPtr address,
                                     core_id_t requester, ShmemPerfModel *perf) = 0;
};

// ============================================================================
// FastDetailedCache — configurable LRU cache level (private per-core)
// ============================================================================
class FastDetailedCache : public FastDetailedCacheBase
{
private:
   String m_name;
   SimLog *m_log;
   const MemComponent::component_t m_mem_component;
   const HitWhere::where_t m_hit_where;
   const ComponentLatency m_latency;
   FastDetailedCacheBase *const m_next_level;
   const UInt64 m_num_sets;
   const IntPtr m_sets_mask;
   std::vector<CacheSet> m_sets;
   UInt64 m_loads, m_stores, m_load_misses, m_store_misses;

   // ---- MSHR tracking ----
   std::unordered_map<IntPtr, SubsecondTime> m_mshr; // tag → completion_time
   UInt32 m_max_outstanding; // 0 = unlimited
   UInt64 m_mshr_hits;       // hits that waited for a pending fill (prefetch in flight)
   UInt64 m_mshr_stalls;     // misses that waited for a free MSHR slot
   SubsecondTime m_mshr_total_latency;

   UInt32 countActiveMshr(SubsecondTime t_now) const
   {
      UInt32 count = 0;
      for (auto &entry : m_mshr)
         if (entry.second > t_now)
            ++count;
      return count;
   }

   SubsecondTime getEarliestMshrCompletion() const
   {
      SubsecondTime earliest = SubsecondTime::MaxTime();
      for (auto &entry : m_mshr)
         if (entry.second < earliest)
            earliest = entry.second;
      return earliest;
   }

   void cleanExpiredMshr(SubsecondTime t_now)
   {
      for (auto it = m_mshr.begin(); it != m_mshr.end(); )
      {
         if (it->second <= t_now)
            it = m_mshr.erase(it);
         else
            ++it;
      }
   }

public:
   FastDetailedCache(Core *core, String name,
                     MemComponent::component_t mem_component,
                     HitWhere::where_t hit_where,
                     UInt32 size_kb, UInt32 assoc, UInt32 latency_cycles,
                     FastDetailedCacheBase *next_level,
                     UInt32 outstanding_misses = 0)
       : m_name(name), m_mem_component(mem_component), m_hit_where(hit_where),
         m_latency(core->getDvfsDomain(), latency_cycles),
         m_next_level(next_level),
         m_num_sets(size_kb * 1024 / 64 / assoc),
         m_sets_mask(m_num_sets - 1),
         m_max_outstanding(outstanding_misses)
   {
      m_log = new SimLog(std::string(name.c_str()), core->getId(), DEBUG_FAST_DETAILED);
      LOG_ASSERT_ERROR(m_num_sets > 0 && (m_num_sets & (m_num_sets - 1)) == 0,
                       "%s: num_sets=%lu must be power of 2 (size_kb=%u, assoc=%u)",
                       name.c_str(), m_num_sets, size_kb, assoc);
      m_sets.reserve(m_num_sets);
      for (UInt64 i = 0; i < m_num_sets; ++i)
         m_sets.emplace_back(assoc);
      m_loads = m_stores = m_load_misses = m_store_misses = 0;
      m_mshr_hits = m_mshr_stalls = 0;
      m_mshr_total_latency = SubsecondTime::Zero();
      registerStatsMetric(name, core->getId(), "loads", &m_loads);
      registerStatsMetric(name, core->getId(), "stores", &m_stores);
      registerStatsMetric(name, core->getId(), "load-misses", &m_load_misses);
      registerStatsMetric(name, core->getId(), "store-misses", &m_store_misses);
      registerStatsMetric(name, core->getId(), "mshr-hits", &m_mshr_hits);
      registerStatsMetric(name, core->getId(), "mshr-stalls", &m_mshr_stalls);
      registerStatsMetric(name, core->getId(), "mshr-total-latency", &m_mshr_total_latency);

      m_log->info("Created:", name.c_str(), "size=", size_kb, "KB assoc=", assoc,
                  "sets=", m_num_sets, "latency=", latency_cycles, "cyc",
                  "outstanding=", outstanding_misses);
   }

   virtual ~FastDetailedCache() { delete m_log; }

   CacheAccessResult access(Core::mem_op_t mem_op_type, IntPtr address,
                             core_id_t requester, ShmemPerfModel *perf) override
   {
      bool is_write = (mem_op_type == Core::WRITE);
      if (is_write)
         ++m_stores;
      else
         ++m_loads;

      IntPtr tag = address >> 6; // 64-byte cache lines
      SubsecondTime t_now = perf->getElapsedTime(ShmemPerfModel::_USER_THREAD);

      m_log->trace(is_write ? "WRITE" : "READ", "addr=", SimLog::hex(address),
                   "tag=", SimLog::hex(tag), "set=", (tag & m_sets_mask),
                   "t_now=", t_now.getNS(), "ns");

      IntPtr evicted_tag;
      bool evicted_dirty;
      if (m_sets[tag & m_sets_mask].access(tag, is_write, evicted_tag, evicted_dirty))
      {
         // HIT — but check for a pending MSHR (e.g. prefetch still in flight)
         SubsecondTime mshr_wait = SubsecondTime::Zero();
         auto it = m_mshr.find(tag);
         if (it != m_mshr.end())
         {
            if (it->second > t_now)
            {
               mshr_wait = it->second - t_now;
               ++m_mshr_hits;
               m_mshr_total_latency += mshr_wait;
               m_log->debug("HIT + MSHR-WAIT tag=", SimLog::hex(tag),
                            "mshr_completion=", it->second.getNS(), "ns",
                            "wait=", mshr_wait.getNS(), "ns");
            }
            m_mshr.erase(it); // consumed
         }
         SubsecondTime hit_lat = m_latency.getLatency() + mshr_wait;
         m_log->trace("HIT lat=", hit_lat.getNS(), "ns where=", HitWhereString(m_hit_where));
         return {hit_lat, m_hit_where};
      }
      else
      {
         // MISS
         if (is_write)
            ++m_store_misses;
         else
            ++m_load_misses;

         m_log->debug("MISS addr=", SimLog::hex(address), "tag=", SimLog::hex(tag),
                      "evicted_tag=", SimLog::hex(evicted_tag),
                      "evicted_dirty=", evicted_dirty ? "Y" : "N",
                      "loads=", m_loads, "misses=", m_load_misses + m_store_misses);

         // Wait for a free MSHR slot if the table is full
         SubsecondTime mshr_stall = SubsecondTime::Zero();
         if (m_max_outstanding > 0 && countActiveMshr(t_now) >= m_max_outstanding)
         {
            SubsecondTime earliest = getEarliestMshrCompletion();
            if (earliest > t_now)
            {
               mshr_stall = earliest - t_now;
               ++m_mshr_stalls;
               m_mshr_total_latency += mshr_stall;
               m_log->debug("MSHR-STALL active=", countActiveMshr(t_now),
                            "max=", m_max_outstanding,
                            "stall=", mshr_stall.getNS(), "ns");
            }
            cleanExpiredMshr(t_now + mshr_stall);
         }

         // Invalidate evicted tag's MSHR (data no longer in this cache)
         if (evicted_tag != ~(IntPtr)0)
            m_mshr.erase(evicted_tag);

         // Access next level
         m_log->trace("-> next-level access for addr=", SimLog::hex(address));
         CacheAccessResult next_result = m_next_level->access(mem_op_type, address, requester, perf);
         m_log->trace("<- next-level returned lat=", next_result.latency.getNS(), "ns",
                      "where=", HitWhereString(next_result.hit_where));

         // Record MSHR entry: completion = now + stall + this_latency + next_latency
         SubsecondTime total_latency = m_latency.getLatency() + mshr_stall + next_result.latency;
         m_mshr[tag] = t_now + total_latency;

         // Dirty writeback of evicted line (fire-and-forget, latency not charged)
         if (evicted_dirty && evicted_tag != ~(IntPtr)0)
         {
            IntPtr evict_addr = evicted_tag << 6;
            m_log->trace("dirty-writeback evict_addr=", SimLog::hex(evict_addr));
            m_next_level->access(Core::WRITE, evict_addr, requester, perf);
         }
         m_log->debug("MISS total_lat=", total_latency.getNS(), "ns",
                      "where=", HitWhereString(next_result.hit_where));
         return {total_latency, next_result.hit_where};
      }
   }

   SubsecondTime getHitLatency() const { return m_latency.getLatency(); }
};

// ============================================================================
// FastDetailedDram — wraps real DramCntlr for detailed DRAM timing
// ============================================================================
class FastDetailedDram : public FastDetailedCacheBase
{
private:
   SimLog *m_log;
   DramCntlrInterface *m_dram_cntlr;
   Lock &m_lock; // Shared with LLC for atomic LLC→DRAM access
   UInt64 m_reads, m_writes;
   SubsecondTime m_total_read_latency;
   ShmemPerf *m_dummy_perf;

public:
   FastDetailedDram(DramCntlrInterface *dram_cntlr, Lock &lock,
                    Core *core, String name, ShmemPerf *dummy_perf)
       : m_dram_cntlr(dram_cntlr), m_lock(lock), m_dummy_perf(dummy_perf)
   {
      m_log = new SimLog(std::string(name.c_str()), core->getId(), DEBUG_FAST_DETAILED);
      m_reads = m_writes = 0;
      m_total_read_latency = SubsecondTime::Zero();
      registerStatsMetric(name, core->getId(), "reads", &m_reads);
      registerStatsMetric(name, core->getId(), "writes", &m_writes);
      registerStatsMetric(name, core->getId(), "total-read-latency", &m_total_read_latency);
      m_log->info("DRAM wrapper created");
   }

   ~FastDetailedDram() { delete m_log; }

   CacheAccessResult access(Core::mem_op_t mem_op_type, IntPtr address,
                             core_id_t requester, ShmemPerfModel *perf) override
   {
      // Lock is already held by the LLC caller
      SubsecondTime now = perf->getElapsedTime(ShmemPerfModel::_USER_THREAD);

      if (mem_op_type == Core::WRITE)
      {
         ++m_writes;
         m_log->trace("DRAM WRITE addr=", SimLog::hex(address), "t=", now.getNS(), "ns");
         auto result = m_dram_cntlr->putDataToDram(address, requester, NULL, now, false);
         SubsecondTime lat = boost::get<0>(result);
         m_log->trace("DRAM WRITE done lat=", lat.getNS(), "ns",
                      "where=", HitWhereString(boost::get<1>(result)));
         return {lat, boost::get<1>(result)};
      }
      else
      {
         ++m_reads;
         m_log->trace("DRAM READ addr=", SimLog::hex(address), "t=", now.getNS(), "ns");
         auto result = m_dram_cntlr->getDataFromDram(address, requester, NULL, now, m_dummy_perf, false);
         SubsecondTime lat = boost::get<0>(result);
         m_total_read_latency += lat;
         m_log->debug("DRAM READ done addr=", SimLog::hex(address),
                      "lat=", lat.getNS(), "ns",
                      "where=", HitWhereString(boost::get<1>(result)),
                      "total_reads=", m_reads);
         return {lat, boost::get<1>(result)};
      }
   }
};

// ============================================================================
// FastDetailedCacheLocked — shared LLC with a global lock
// ============================================================================
class FastDetailedCacheLocked : public FastDetailedCacheBase
{
private:
   SimLog *m_log;
   const HitWhere::where_t m_hit_where;
   const ComponentLatency m_latency;
   FastDetailedCacheBase *const m_next_level; // DRAM
   const UInt64 m_num_sets;
   const IntPtr m_sets_mask;
   std::vector<CacheSet> m_sets;
   Lock &m_lock;
   UInt64 m_loads, m_stores, m_load_misses, m_store_misses;

   // ---- MSHR tracking (protected by m_lock) ----
   std::unordered_map<IntPtr, SubsecondTime> m_mshr; // tag → completion_time
   UInt64 m_mshr_hits;       // hits that waited for a pending fill
   SubsecondTime m_mshr_total_latency;

public:
   FastDetailedCacheLocked(Core *core, String name,
                           HitWhere::where_t hit_where,
                           UInt32 size_kb, UInt32 assoc, UInt32 latency_cycles,
                           FastDetailedCacheBase *next_level, Lock &lock)
       : m_hit_where(hit_where),
         m_latency(core->getDvfsDomain(), latency_cycles),
         m_next_level(next_level),
         m_num_sets(size_kb * 1024 / 64 / assoc),
         m_sets_mask(m_num_sets - 1),
         m_lock(lock)
   {
      m_log = new SimLog(std::string(name.c_str()), core->getId(), DEBUG_FAST_DETAILED);
      LOG_ASSERT_ERROR(m_num_sets > 0 && (m_num_sets & (m_num_sets - 1)) == 0,
                       "%s: num_sets=%lu must be power of 2 (size_kb=%u, assoc=%u)",
                       name.c_str(), m_num_sets, size_kb, assoc);
      m_sets.reserve(m_num_sets);
      for (UInt64 i = 0; i < m_num_sets; ++i)
         m_sets.emplace_back(assoc);
      m_loads = m_stores = m_load_misses = m_store_misses = 0;
      m_mshr_hits = 0;
      m_mshr_total_latency = SubsecondTime::Zero();
      registerStatsMetric(name, core->getId(), "loads", &m_loads);
      registerStatsMetric(name, core->getId(), "stores", &m_stores);
      registerStatsMetric(name, core->getId(), "load-misses", &m_load_misses);
      registerStatsMetric(name, core->getId(), "store-misses", &m_store_misses);
      registerStatsMetric(name, core->getId(), "mshr-hits", &m_mshr_hits);
      registerStatsMetric(name, core->getId(), "mshr-total-latency", &m_mshr_total_latency);
      m_log->info("Created: LLC", name.c_str(), "size=", size_kb, "KB assoc=", assoc,
                  "sets=", m_num_sets, "latency=", latency_cycles, "cyc");
   }

   ~FastDetailedCacheLocked() { delete m_log; }

   CacheAccessResult access(Core::mem_op_t mem_op_type, IntPtr address,
                             core_id_t requester, ShmemPerfModel *perf) override
   {
      ScopedLock sl(m_lock);

      bool is_write = (mem_op_type == Core::WRITE);
      if (is_write)
         ++m_stores;
      else
         ++m_loads;

      IntPtr tag = address >> 6;
      SubsecondTime t_now = perf->getElapsedTime(ShmemPerfModel::_USER_THREAD);

      m_log->trace(is_write ? "LLC WRITE" : "LLC READ",
                   "addr=", SimLog::hex(address), "tag=", SimLog::hex(tag),
                   "set=", (tag & m_sets_mask), "t_now=", t_now.getNS(), "ns",
                   "requester=", requester);

      IntPtr evicted_tag;
      bool evicted_dirty;
      if (m_sets[tag & m_sets_mask].access(tag, is_write, evicted_tag, evicted_dirty))
      {
         // HIT — check for pending MSHR
         SubsecondTime mshr_wait = SubsecondTime::Zero();
         auto it = m_mshr.find(tag);
         if (it != m_mshr.end())
         {
            if (it->second > t_now)
            {
               mshr_wait = it->second - t_now;
               ++m_mshr_hits;
               m_mshr_total_latency += mshr_wait;
               m_log->debug("LLC HIT + MSHR-WAIT tag=", SimLog::hex(tag),
                            "wait=", mshr_wait.getNS(), "ns");
            }
            m_mshr.erase(it);
         }
         SubsecondTime hit_lat = m_latency.getLatency() + mshr_wait;
         m_log->trace("LLC HIT lat=", hit_lat.getNS(), "ns");
         return {hit_lat, m_hit_where};
      }
      else
      {
         if (is_write)
            ++m_store_misses;
         else
            ++m_load_misses;

         m_log->debug("LLC MISS addr=", SimLog::hex(address),
                      "evicted_tag=", SimLog::hex(evicted_tag),
                      "evicted_dirty=", evicted_dirty ? "Y" : "N",
                      "total_misses=", m_load_misses + m_store_misses);

         // Invalidate evicted tag's MSHR
         if (evicted_tag != ~(IntPtr)0)
            m_mshr.erase(evicted_tag);

         // LLC miss → access DRAM (lock is held, DRAM access is atomic with LLC lookup)
         CacheAccessResult dram_result = m_next_level->access(mem_op_type, address, requester, perf);

         // Record MSHR entry
         SubsecondTime total_latency = m_latency.getLatency() + dram_result.latency;
         m_mshr[tag] = t_now + total_latency;

         m_log->debug("LLC MISS -> DRAM lat=", total_latency.getNS(), "ns",
                      "where=", HitWhereString(dram_result.hit_where));

         // Handle dirty writeback from LLC eviction (fire-and-forget)
         if (evicted_dirty && evicted_tag != ~(IntPtr)0)
         {
            IntPtr evict_addr = evicted_tag << 6;
            m_log->trace("LLC dirty-writeback evict_addr=", SimLog::hex(evict_addr));
            m_next_level->access(Core::WRITE, evict_addr, requester, perf);
         }
         return {total_latency, dram_result.hit_where};
      }
   }
};

// ============================================================================
// FastDetailedCacheCntlrAdapter — MMU-compatible adapter
// ============================================================================
class FastDetailedCacheCntlrAdapter : public MMUCacheInterface
{
private:
   SimLog *m_log;
   FastDetailedCacheBase *m_cache;
   core_id_t m_core_id;
   ShmemPerfModel *m_shmem_perf_model;
   SubsecondTime m_last_prefetch_completion;

public:
   FastDetailedCacheCntlrAdapter(FastDetailedCacheBase *cache, core_id_t core_id,
                                  ShmemPerfModel *perf, const char *adapter_name = "fd-adapter")
       : m_cache(cache), m_core_id(core_id), m_shmem_perf_model(perf),
         m_last_prefetch_completion(SubsecondTime::Zero())
   {
      m_log = new SimLog(std::string(adapter_name), core_id, DEBUG_FAST_DETAILED);
      m_log->info("MMU adapter created for core=", core_id);
   }

   ~FastDetailedCacheCntlrAdapter() { delete m_log; }

   HitWhere::where_t handleMMUCacheAccess(
       IntPtr eip, Core::lock_signal_t lock_signal,
       Core::mem_op_t mem_op_type,
       IntPtr ca_address, UInt32 offset,
       Byte *data_buf, UInt32 data_length,
       bool modeled, bool count,
       CacheBlockInfo::block_type_t block_type,
       SubsecondTime t_start) override
   {
      // NOTE: The perf-model time is already set to the correct access start time
      // by accessCache() in mmu_base.cc *before* calling us. The t_start parameter
      // here is actually host_translation_latency (usually Zero), NOT the real
      // start time. Do NOT call setElapsedTime(t_start) here!

      SubsecondTime t_before = m_shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
      m_log->debug("handleMMUCacheAccess addr=", SimLog::hex(ca_address),
                   "eip=", SimLog::hex(eip),
                   "op=", (mem_op_type == Core::WRITE ? "W" : "R"),
                   "host_trans_lat=", t_start.getNS(), "ns",
                   "perf_time_before=", t_before.getNS(), "ns",
                   "block_type=", (int)block_type);

      // If there's additional host translation latency (nested MMU), add it
      if (t_start > SubsecondTime::Zero())
         m_shmem_perf_model->incrElapsedTime(t_start, ShmemPerfModel::_USER_THREAD);

      // Walk cache hierarchy
      CacheAccessResult result = m_cache->access(mem_op_type, ca_address, m_core_id, m_shmem_perf_model);

      // Advance elapsed time by total latency
      m_shmem_perf_model->incrElapsedTime(result.latency, ShmemPerfModel::_USER_THREAD);

      SubsecondTime t_after = m_shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
      m_log->debug("handleMMUCacheAccess done addr=", SimLog::hex(ca_address),
                   "result=", HitWhereString(result.hit_where),
                   "cache_lat=", result.latency.getNS(), "ns",
                   "perf_time_after=", t_after.getNS(), "ns",
                   "delta=", (t_after - t_before).getNS(), "ns");

      return result.hit_where;
   }

   void handleMMUPrefetch(
       IntPtr eip, IntPtr prefetch_address,
       SubsecondTime t_start,
       CacheBlockInfo::block_type_t block_type) override
   {
      // Prefetch = fire-and-forget READ that warms the cache hierarchy.
      SubsecondTime saved_time = m_shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

      m_log->debug("handleMMUPrefetch addr=", SimLog::hex(prefetch_address),
                   "t_start=", t_start.getNS(), "ns",
                   "saved_time=", saved_time.getNS(), "ns");

      m_shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, t_start);
      m_cache->access(Core::READ, prefetch_address, m_core_id, m_shmem_perf_model);

      // Capture the time at which the prefetch data became available
      m_last_prefetch_completion = m_shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

      // Restore the original time — the prefetch is "free" from the requester's perspective
      m_shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, saved_time);

      m_log->trace("handleMMUPrefetch done, time restored to", saved_time.getNS(), "ns");
   }

   SubsecondTime getLastPrefetchCompletion() const override
   {
      return m_last_prefetch_completion;
   }
};

} // namespace FastDetailed
