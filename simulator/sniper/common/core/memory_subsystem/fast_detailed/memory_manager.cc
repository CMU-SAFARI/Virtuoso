#include "memory_manager.h"
#include "simulator.h"
#include "core.h"
#include "core_manager.h"
#include "log.h"
#include "config.hpp"
#include "stats.h"
#include "dvfs_manager.h"
#include "itostr.h"
#include "thread.h"
#include "trace_thread.h"
#include "trace_manager.h"
#include "mimicos.h"
#include "performance_model.h"
#include "mmu_factory.h"
#include "mmu.h"
#include "metadata_info.h"
#include "address_home_lookup.h"
#include "dram_cntlr.h"
#include "tiered_dram_cntlr.h"
#include "topology_info.h"

#include <algorithm>
#include <iostream>

namespace FastDetailed
{

// Static member definitions
FastDetailedCacheLocked *MemoryManager::s_llc = nullptr;
FastDetailedDram *MemoryManager::s_dram_wrapper = nullptr;
DramCntlrInterface *MemoryManager::s_dram_cntlr = nullptr;
AddressHomeLookup *MemoryManager::s_dram_controller_home_lookup = nullptr;
Lock MemoryManager::s_shared_lock;
ShmemPerf MemoryManager::s_dummy_perf;
UInt32 MemoryManager::s_num_instances = 0;

MemoryManager::MemoryManager(Core *core, Network *network, ShmemPerfModel *shmem_perf_model)
    : MemoryManagerBase(core, network, shmem_perf_model),
      m_icache(nullptr), m_dcache(nullptr), m_l2cache(nullptr),
      m_l1d_adapter(nullptr), m_l2_adapter(nullptr),
      m_mmu(nullptr), m_enabled(false)
{
   m_log = new SimLog("FD-MM", core->getId(), DEBUG_FAST_DETAILED);
   m_log->info("=== Initializing fast_detailed memory hierarchy for core", core->getId(), "===");

   // Read config
   m_cache_block_size = Sim()->getCfg()->getInt("perf_model/l1_icache/cache_block_size");
   m_native_environment = Sim()->getCfg()->getBool("general/native_environment");
   m_virtualized_environment = Sim()->getCfg()->getBool("general/virtualized_environment");
   m_translation_enabled = Sim()->getCfg()->getBool("general/translation_enabled");

   m_log->info("Config: block_size=", m_cache_block_size,
               "native=", m_native_environment,
               "virt=", m_virtualized_environment,
               "translation=", m_translation_enabled);

   // Read cache parameters
   UInt32 l1i_size = Sim()->getCfg()->getIntArray("perf_model/l1_icache/cache_size", core->getId());
   UInt32 l1i_assoc = Sim()->getCfg()->getIntArray("perf_model/l1_icache/associativity", core->getId());
   UInt32 l1i_lat = Sim()->getCfg()->getIntArray("perf_model/l1_icache/data_access_time", core->getId());

   UInt32 l1d_size = Sim()->getCfg()->getIntArray("perf_model/l1_dcache/cache_size", core->getId());
   UInt32 l1d_assoc = Sim()->getCfg()->getIntArray("perf_model/l1_dcache/associativity", core->getId());
   UInt32 l1d_lat = Sim()->getCfg()->getIntArray("perf_model/l1_dcache/data_access_time", core->getId());
   UInt32 l1d_outstanding = Sim()->getCfg()->getIntArray("perf_model/l1_dcache/outstanding_misses", core->getId());

   UInt32 l2_size = Sim()->getCfg()->getIntArray("perf_model/l2_cache/cache_size", core->getId());
   UInt32 l2_assoc = Sim()->getCfg()->getIntArray("perf_model/l2_cache/associativity", core->getId());
   UInt32 l2_lat = Sim()->getCfg()->getIntArray("perf_model/l2_cache/data_access_time", core->getId());

   UInt32 llc_size = Sim()->getCfg()->getIntArray("perf_model/l3_cache/cache_size", core->getId());
   UInt32 llc_assoc = Sim()->getCfg()->getIntArray("perf_model/l3_cache/associativity", core->getId());
   UInt32 llc_lat = Sim()->getCfg()->getIntArray("perf_model/l3_cache/data_access_time", core->getId());

   m_log->info("L1I: size=", l1i_size, "KB assoc=", l1i_assoc, "lat=", l1i_lat);
   m_log->info("L1D: size=", l1d_size, "KB assoc=", l1d_assoc, "lat=", l1d_lat, "outstanding=", l1d_outstanding);
   m_log->info("L2:  size=", l2_size, "KB assoc=", l2_assoc, "lat=", l2_lat);
   m_log->info("L3:  size=", llc_size, "KB assoc=", llc_assoc, "lat=", llc_lat);

   // Create shared static resources (once)
   if (s_num_instances == 0)
   {
      // DRAM controller
      std::vector<core_id_t> core_list_with_dram = getCoreListWithMemoryControllers();
      UInt32 dram_home_lookup_param = 0;
      try
      {
         dram_home_lookup_param = Sim()->getCfg()->getInt("perf_model/dram_directory/home_lookup_param");
      }
      catch (...)
      {
         dram_home_lookup_param = 0;
      }
      s_dram_controller_home_lookup = new AddressHomeLookup(dram_home_lookup_param, core_list_with_dram, m_cache_block_size);

      bool cxl_enabled = false;
      try
      {
         cxl_enabled = Sim()->getCfg()->getBool("perf_model/cxl/enabled");
      }
      catch (...)
      {
         cxl_enabled = false;
      }

      bool numa_enabled = false;
      try
      {
         numa_enabled = Sim()->getCfg()->getBool("perf_model/dram/numa/enabled");
      }
      catch (...)
      {
         numa_enabled = false;
      }

      if (cxl_enabled || numa_enabled)
      {
         s_dram_cntlr = new PrL1PrL2DramDirectoryMSI::TieredDramCntlr(
             this, shmem_perf_model, m_cache_block_size, s_dram_controller_home_lookup);
         m_log->info("Tiered/NUMA DRAM controller created (CXL=", cxl_enabled, ", NUMA=", numa_enabled, ")");
      }
      else
      {
         s_dram_cntlr = new PrL1PrL2DramDirectoryMSI::DramCntlr(
             this, shmem_perf_model, m_cache_block_size, s_dram_controller_home_lookup);
         m_log->info("Standard DRAM controller created");
      }

      // DRAM wrapper
      s_dram_wrapper = new FastDetailedDram(s_dram_cntlr, s_shared_lock, core, "fast-detailed-dram", &s_dummy_perf);

      // Shared LLC
      s_llc = new FastDetailedCacheLocked(core, "fast-detailed-L3",
                                          HitWhere::L3_OWN,
                                          llc_size, llc_assoc, llc_lat,
                                          s_dram_wrapper, s_shared_lock);

      Sim()->getStatsManager()->logTopology("dram-cntlr", core->getId(), core->getId());
   }
   s_num_instances++;

   // Per-core private caches: chain L1→L2→LLC
   m_l2cache = new FastDetailedCache(core, "fast-detailed-L2",
                                      MemComponent::L2_CACHE, HitWhere::L2_OWN,
                                      l2_size, l2_assoc, l2_lat, s_llc);

   m_icache = new FastDetailedCache(core, "fast-detailed-L1-I",
                                     MemComponent::L1_ICACHE, HitWhere::L1I,
                                     l1i_size, l1i_assoc, l1i_lat, m_l2cache);

   m_dcache = new FastDetailedCache(core, "fast-detailed-L1-D",
                                     MemComponent::L1_DCACHE, HitWhere::L1_OWN,
                                     l1d_size, l1d_assoc, l1d_lat, m_l2cache,
                                     l1d_outstanding);

   m_l1_hit_latency = m_dcache->getHitLatency();
   m_log->info("L1D hit latency =", m_l1_hit_latency.getNS(), "ns");

   // Create MMU-compatible adapters
   m_l1d_adapter = new FastDetailedCacheCntlrAdapter(m_dcache, core->getId(), shmem_perf_model, "FD-L1D-Adapter");
   m_l2_adapter = new FastDetailedCacheCntlrAdapter(m_l2cache, core->getId(), shmem_perf_model, "FD-L2-Adapter");

   // Create MMU
   if (m_native_environment)
   {
      mmu_type = Sim()->getCfg()->getString("perf_model/mmu/type");
      m_mmu = ParametricDramDirectoryMSI::MMUFactory::createMemoryManagementUnit(
          mmu_type, core, this, shmem_perf_model, "mmu");
      m_log->info("MMU created type=", mmu_type.c_str());
   }
   else if (m_virtualized_environment)
   {
      mmu_type = "virt";
      String host_mmu_type = Sim()->getCfg()->getString("perf_model/host_mmu/type");
      auto *host_mmu = ParametricDramDirectoryMSI::MMUFactory::createMemoryManagementUnit(
          host_mmu_type, core, this, shmem_perf_model, "host_mmu");
      m_mmu = ParametricDramDirectoryMSI::MMUFactory::createMemoryManagementUnit(
          "virt", core, this, shmem_perf_model, "mmu", host_mmu);
      m_log->info("Virtualized MMU created, host_mmu=", host_mmu_type.c_str());
   }

   if (m_translation_enabled)
   {
      LOG_ASSERT_ERROR(m_mmu != nullptr,
          "translation_enabled is true but no MMU was created (check native_environment/virtualized_environment)");
   }

   // Register stats
   bzero(&memory_access_stats, sizeof(memory_access_stats));
   registerStatsMetric("memory_manager", core->getId(), "memory_access_latency", &memory_access_stats.m_memory_access_latency);
   registerStatsMetric("memory_manager", core->getId(), "translation_latency", &memory_access_stats.m_translation_latency);
   registerStatsMetric("memory_manager", core->getId(), "memory_accesses", &memory_access_stats.m_memory_accesses);
   registerStatsMetric("memory_manager", core->getId(), "translation_dram_memory_dram", &memory_access_stats.translation_dram_memory_dram);
   registerStatsMetric("memory_manager", core->getId(), "translation_dram_memory_cache", &memory_access_stats.translation_dram_memory_cache);
   registerStatsMetric("memory_manager", core->getId(), "translation_cache_memory_dram", &memory_access_stats.translation_cache_memory_dram);
   registerStatsMetric("memory_manager", core->getId(), "translation_cache_memory_cache", &memory_access_stats.translation_cache_memory_cache);
   registerStatsMetric("memory_manager", core->getId(), "latency_translation_dram_memory_dram", &memory_access_stats.latency_translation_dram_memory_dram);
   registerStatsMetric("memory_manager", core->getId(), "latency_translation_dram_memory_cache", &memory_access_stats.latency_translation_dram_memory_cache);
   registerStatsMetric("memory_manager", core->getId(), "latency_translation_cache_memory_dram", &memory_access_stats.latency_translation_cache_memory_dram);
   registerStatsMetric("memory_manager", core->getId(), "latency_translation_cache_memory_cache", &memory_access_stats.latency_translation_cache_memory_cache);
   registerStatsMetric("memory_manager", core->getId(), "translation_slower_than_memory_access", &memory_access_stats.translation_slower_than_memory_access);
   registerStatsMetric("memory_manager", core->getId(), "translation_faster_than_memory_access", &memory_access_stats.translation_faster_than_memory_access);
   registerStatsMetric("memory_manager", core->getId(), "unique_data_cache_lines", &memory_access_stats.unique_data_cache_lines);

   // Setup core topology info
   UInt32 smt_cores = Sim()->getCfg()->getInt("perf_model/core/logical_cpus");
   UInt32 shared_cores = Sim()->getCfg()->getIntArray("perf_model/l3_cache/shared_cores", core->getId());
   getCore()->getTopologyInfo()->setup(smt_cores, shared_cores * smt_cores);

   m_log->info("=== Memory hierarchy initialized for core", core->getId(), "===");
}

MemoryManager::~MemoryManager()
{
   delete m_icache;
   delete m_dcache;
   delete m_l2cache;
   delete m_l1d_adapter;
   delete m_l2_adapter;

   if (m_mmu)
   {
      m_log->info("Destroying MMU for core", getCore()->getId());
      delete m_mmu;
   }

   // Shared resources: clean up on last instance
   s_num_instances--;
   if (s_num_instances == 0)
   {
      delete s_llc;
      s_llc = nullptr;
      delete s_dram_wrapper;
      s_dram_wrapper = nullptr;
      delete s_dram_cntlr;
      s_dram_cntlr = nullptr;
      delete s_dram_controller_home_lookup;
      s_dram_controller_home_lookup = nullptr;
      m_log->info("Shared resources cleaned up");
   }

   m_log->info("Core", getCore()->getId(), "accessed",
               (UInt64)m_unique_data_cache_lines.size(), "unique data cache lines");
   delete m_log;
}

MMUCacheInterface *MemoryManager::getCacheCntlrAt(core_id_t core_id, MemComponent::component_t mem_component)
{
   if (core_id != getCore()->getId())
      return nullptr; // Only return adapters for our own core

   switch (mem_component)
   {
   case MemComponent::L1_DCACHE:
      return m_l1d_adapter;
   case MemComponent::L2_CACHE:
      return m_l2_adapter;
   default:
      return nullptr;
   }
}

HitWhere::where_t
MemoryManager::coreInitiateMemoryAccess(
    IntPtr eip,
    MemComponent::component_t mem_component,
    Core::lock_signal_t lock_signal,
    Core::mem_op_t mem_op_type,
    IntPtr address, UInt32 offset,
    Byte *data_buf, UInt32 data_length,
    Core::MemModeled modeled)
{
   bool count = (modeled != Core::MEM_MODELED_NONE);
   bool should_model = (modeled != Core::MEM_MODELED_NONE && modeled != Core::MEM_MODELED_COUNT);

   bool skip_translation = !m_translation_enabled;

   // Get core/thread info
   Core *core = getCore();
   int thread_id = core->getThread()->getId();
   int app_id = core->getThread()->getAppId();

   m_log->trace("coreInitiateMemoryAccess eip=", SimLog::hex(eip),
                "addr=", SimLog::hex(address),
                "op=", (mem_op_type == Core::WRITE ? "W" : "R"),
                "comp=", (mem_component == MemComponent::L1_ICACHE ? "I" : "D"),
                "modeled=", (int)modeled,
                "thread=", thread_id, "app=", app_id);

   // Update MimicOS per-core instruction/cycle stats
   if (count)
   {
      MimicOS *mimicos = Sim()->getMimicOS();
      if (mimicos && mimicos->isPerCoreStatsInitialized())
      {
         UInt64 instructions = core->getPerformanceModel()->getInstructionCount();
         UInt64 cycles = core->getPerformanceModel()->getElapsedTime().getNS();
         mimicos->updateInstructionStats(core->getId(), instructions, cycles);
      }
   }

   // Check kernel thread — skip translation for kernel accesses
   TraceThread *trace_thread = Sim()->getTraceManager()->getTraceThread(app_id, thread_id);
   if ((trace_thread->getCurrentSiftReader() == trace_thread->getKernelSiftReader()) && this->getIsUserspaceMimicosEnabled())
   {
      skip_translation = true;
   }

   bool is_instruction = (mem_component == MemComponent::L1_ICACHE);

   // ===== ADDRESS TRANSLATION =====
   IntPtr translation_result = address;
   SubsecondTime t_start_translation = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_USER_THREAD);

   m_log->trace("Translation start t=", t_start_translation.getNS(), "ns",
                "skip=", skip_translation ? "Y" : "N",
                "mmu_type=", mmu_type.c_str());

   if (!skip_translation)
   {
      translation_result = m_mmu->performAddressTranslation(
          eip, address, is_instruction, lock_signal, should_model, count);
      if (translation_result == static_cast<IntPtr>(-1))
      {
         return HitWhere::where_t::PAGE_FAULT;
      }
   }

   int dram_accesses_during_translation = m_mmu ? m_mmu->getDramAccessesDuringLastWalk() : 0;
   bool performed_dram_access_during_translation = dram_accesses_during_translation > 0;

   SubsecondTime t_end_translation = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_USER_THREAD);

   m_log->debug("Translation done: virt=", SimLog::hex(address),
                "-> phys_raw=", SimLog::hex(translation_result),
                "trans_lat=", (t_end_translation - t_start_translation).getNS(), "ns",
                "dram_walks=", dram_accesses_during_translation);

   if (count)
   {
      memory_access_stats.m_translation_latency += (t_end_translation - t_start_translation);
   }

   // ===== PHYSICAL ADDRESS =====
   IntPtr physical_address;
   if (!skip_translation)
   {
      physical_address = translation_result & ~((IntPtr)m_cache_block_size - 1);
   }
   else
   {
      physical_address = address;
   }

   // Track unique data cache lines
   if (!is_instruction)
   {
      m_unique_data_cache_lines.insert(physical_address);
      memory_access_stats.unique_data_cache_lines = m_unique_data_cache_lines.size();
   }

   // ===== CACHE ACCESS =====
   SubsecondTime t_cache_issue = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_USER_THREAD);

   m_log->trace("Cache access start phys=", SimLog::hex(physical_address),
                "t_issue=", t_cache_issue.getNS(), "ns",
                is_instruction ? "I$" : "D$");

   // Set MetadataContext for DRAM model correlation
   if (!skip_translation && performed_dram_access_during_translation && m_mmu)
   {
      UInt64 ptw_id = m_mmu->getLastPtwId();
      MetadataContext::set(getCore()->getId(), MetadataInfo::dataAfterPtw(
                                                   ptw_id, address, physical_address,
                                                   performed_dram_access_during_translation,
                                                   dram_accesses_during_translation));
   }

   // Choose I-cache or D-cache
   FastDetailedCacheBase *cache = is_instruction ? (FastDetailedCacheBase *)m_icache : (FastDetailedCacheBase *)m_dcache;
   CacheAccessResult result = cache->access(mem_op_type, physical_address, core->getId(), getShmemPerfModel());

   // Advance time
   getShmemPerfModel()->incrElapsedTime(result.latency, ShmemPerfModel::_USER_THREAD);

   // Clear MetadataContext
   MetadataContext::clear(getCore()->getId());

   SubsecondTime t_cache_done = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_USER_THREAD);

   m_log->debug("Cache access done phys=", SimLog::hex(physical_address),
                "where=", HitWhereString(result.hit_where),
                "cache_lat=", result.latency.getNS(), "ns",
                "t_done=", t_cache_done.getNS(), "ns",
                "total_accesses=", memory_access_stats.m_memory_accesses + 1);

   // ===== STATISTICS =====
   bool dram_access = (result.hit_where == HitWhere::DRAM ||
                       result.hit_where == HitWhere::DRAM_CACHE ||
                       result.hit_where == HitWhere::DRAM_LOCAL ||
                       result.hit_where == HitWhere::DRAM_REMOTE);

   if (count)
   {
      memory_access_stats.m_memory_access_latency += (t_cache_done - t_cache_issue);
      memory_access_stats.m_memory_accesses++;

      MimicOS *mimicos = Sim()->getMimicOS();
      if (mimicos && mimicos->isPerCoreStatsInitialized())
      {
         bool is_cache_hit = !dram_access;
         mimicos->updateDataAccessStats(core->getId(), t_cache_done - t_cache_issue, is_cache_hit);
      }
   }

   if (count)
   {
      SubsecondTime trans_lat = t_end_translation - t_start_translation;
      SubsecondTime cache_lat = t_cache_done - t_cache_issue;
      SubsecondTime total_lat = trans_lat + cache_lat;

      if (dram_access && performed_dram_access_during_translation)
      {
         memory_access_stats.latency_translation_dram_memory_dram += total_lat;
         memory_access_stats.translation_dram_memory_dram++;
      }
      else if (dram_access && !performed_dram_access_during_translation)
      {
         memory_access_stats.latency_translation_cache_memory_dram += total_lat;
         memory_access_stats.translation_cache_memory_dram++;
      }
      else if (!dram_access && performed_dram_access_during_translation)
      {
         memory_access_stats.latency_translation_dram_memory_cache += total_lat;
         memory_access_stats.translation_dram_memory_cache++;
      }
      else
      {
         memory_access_stats.latency_translation_cache_memory_cache += total_lat;
         memory_access_stats.translation_cache_memory_cache++;
      }

      if (cache_lat < trans_lat)
         memory_access_stats.translation_slower_than_memory_access++;
      if (cache_lat > trans_lat)
         memory_access_stats.translation_faster_than_memory_access++;
   }

   return result.hit_where;
}

void MemoryManager::enableModels()
{
   m_log->info("enableModels()");
   m_enabled = true;
   if (s_dram_cntlr)
   {
      auto *dram_cntlr = dynamic_cast<PrL1PrL2DramDirectoryMSI::DramCntlr *>(s_dram_cntlr);
      if (dram_cntlr)
         dram_cntlr->getDramPerfModel()->enable();
   }
}

void MemoryManager::disableModels()
{
   m_log->info("disableModels()");
   m_enabled = false;
   if (s_dram_cntlr)
   {
      auto *dram_cntlr = dynamic_cast<PrL1PrL2DramDirectoryMSI::DramCntlr *>(s_dram_cntlr);
      if (dram_cntlr)
         dram_cntlr->getDramPerfModel()->disable();
   }
}

} // namespace FastDetailed
