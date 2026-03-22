#pragma once

#include "memory_manager_base.h"
#include "fast_detailed_cache.h"
#include "mmu_base.h"
#include "shmem_perf.h"
#include "lock.h"
#include "sim_log.h"

#include <unordered_set>

// Forward declarations
namespace PrL1PrL2DramDirectoryMSI
{
   class DramCntlr;
}
class DramCntlrInterface;
class AddressHomeLookup;

namespace FastDetailed
{

class MemoryManager : public MemoryManagerBase
{
private:
   // Per-core private caches
   FastDetailedCache *m_icache;
   FastDetailedCache *m_dcache;
   FastDetailedCache *m_l2cache;

   // Shared across all cores (static)
   static FastDetailedCacheLocked *s_llc;
   static FastDetailedDram *s_dram_wrapper;
   static DramCntlrInterface *s_dram_cntlr;
   static AddressHomeLookup *s_dram_controller_home_lookup;
   static Lock s_shared_lock;      // Protects LLC and DRAM
   static ShmemPerf s_dummy_perf;
   static UInt32 s_num_instances;   // Reference counting for cleanup

   // MMU adapter for L1D (registered via getCacheCntlrAt for MMU to find)
   FastDetailedCacheCntlrAdapter *m_l1d_adapter;
   // Also register L2 adapter for prefetch requests
   FastDetailedCacheCntlrAdapter *m_l2_adapter;

   // Logger
   SimLog *m_log;

   // MMU
   ParametricDramDirectoryMSI::MemoryManagementUnitBase *m_mmu;
   String mmu_type;
   bool m_native_environment;
   bool m_virtualized_environment;
   bool m_translation_enabled;

   // Config
   UInt32 m_cache_block_size;
   bool m_enabled;

   // L1 hit latency (for fast path)
   SubsecondTime m_l1_hit_latency;

   // Statistics
   struct
   {
      SubsecondTime m_memory_access_latency;
      SubsecondTime m_translation_latency;
      UInt64 m_memory_accesses;
      UInt64 translation_dram_memory_dram;
      UInt64 translation_dram_memory_cache;
      UInt64 translation_cache_memory_dram;
      UInt64 translation_cache_memory_cache;
      SubsecondTime latency_translation_dram_memory_dram;
      SubsecondTime latency_translation_dram_memory_cache;
      SubsecondTime latency_translation_cache_memory_dram;
      SubsecondTime latency_translation_cache_memory_cache;
      UInt64 translation_slower_than_memory_access;
      UInt64 translation_faster_than_memory_access;
      UInt64 unique_data_cache_lines;
   } memory_access_stats;

   std::unordered_set<IntPtr> m_unique_data_cache_lines;

public:
   MemoryManager(Core *core, Network *network, ShmemPerfModel *shmem_perf_model);
   ~MemoryManager();

   // MemoryManagerBase interface
   HitWhere::where_t coreInitiateMemoryAccess(
       IntPtr eip, MemComponent::component_t mem_component,
       Core::lock_signal_t lock_signal, Core::mem_op_t mem_op_type,
       IntPtr address, UInt32 offset,
       Byte *data_buf, UInt32 data_length,
       Core::MemModeled modeled) override;

   SubsecondTime coreInitiateMemoryAccessFast(
       bool icache, Core::mem_op_t mem_op_type, IntPtr address) override
   {
      IntPtr tag_addr = address & ~((IntPtr)m_cache_block_size - 1);
      CacheAccessResult result = (icache ? m_icache : m_dcache)->access(
          mem_op_type, tag_addr, getCore()->getId(), getShmemPerfModel());
      return result.latency;
   }

   void handleMsgFromNetwork(NetPacket &packet) override
   {
      LOG_PRINT_ERROR("fast_detailed does not use network messages");
   }

   UInt64 getCacheBlockSize() const override { return m_cache_block_size; }

   SubsecondTime getL1HitLatency(void) override { return m_l1_hit_latency; }
   void addL1Hits(bool icache, Core::mem_op_t mem_op_type, UInt64 hits) override {}

   core_id_t getShmemRequester(const void *pkt_data) override { return getCore()->getId(); }
   UInt32 getModeledLength(const void *pkt_data) override { return m_cache_block_size; }

   void enableModels() override;
   void disableModels() override;

   void measureNucaStats() override {}
   NucaCache *getNucaCache() override { return NULL; }
   Cache *getCache(MemComponent::component_t mem_component) override { return NULL; }

   // MMU cache interface — allow MMU to access our cache hierarchy
   MMUCacheInterface *getCacheCntlrAt(core_id_t core_id, MemComponent::component_t mem_component) override;
   void tagCachesBlockType(IntPtr address, CacheBlockInfo::block_type_t btype) override {}
   ParametricDramDirectoryMSI::MemoryManagementUnitBase *getMMU() override { return m_mmu; }

   void sendMsg(PrL1PrL2DramDirectoryMSI::ShmemMsg::msg_t msg_type,
                MemComponent::component_t sender_mem_component,
                MemComponent::component_t receiver_mem_component,
                core_id_t requester, core_id_t receiver, IntPtr address,
                Byte *data_buf = NULL, UInt32 data_length = 0,
                HitWhere::where_t where = HitWhere::UNKNOWN,
                ShmemPerf *perf = NULL,
                ShmemPerfModel::Thread_t thread_num = ShmemPerfModel::NUM_CORE_THREADS,
                CacheBlockInfo::block_type_t block_type = CacheBlockInfo::block_type_t::DATA) override
   {
      LOG_PRINT_ERROR("fast_detailed does not use network messages");
   }

   void broadcastMsg(PrL1PrL2DramDirectoryMSI::ShmemMsg::msg_t msg_type,
                     MemComponent::component_t sender_mem_component,
                     MemComponent::component_t receiver_mem_component,
                     core_id_t requester, IntPtr address,
                     Byte *data_buf = NULL, UInt32 data_length = 0,
                     ShmemPerf *perf = NULL,
                     ShmemPerfModel::Thread_t thread_num = ShmemPerfModel::NUM_CORE_THREADS) override
   {
      LOG_PRINT_ERROR("fast_detailed does not use network messages");
   }
};

} // namespace FastDetailed
