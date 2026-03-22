#include "cache_set_mplru.h"
#include "simulator.h"
#include "config.hpp"
#include "stats.h"
#include "log.h"
#include "mplru_controller_impl.h"
#include "mplru_controller_factory.h"
#include "sim_log.h"
#include "debug_config.h"
#include <algorithm>

// CacheSetInfoMPLRU implementation

CacheSetInfoMPLRU::CacheSetInfoMPLRU(String name, String cfgname, core_id_t core_id, 
                                     UInt32 associativity, UInt8 num_attempts)
   : CacheSetInfoLRU(name, cfgname, core_id, associativity, num_attempts)
   , m_core_id(core_id)
   , m_associativity(associativity)
   , m_metadata_protected(0)
   , m_data_evicted(0)
   , m_metadata_evicted(0)
   , m_data_protected(0)
   , m_metadata_evicted_for_data(0)
{
   // Read configuration
   String mode_str = Sim()->getCfg()->getStringArray(cfgname + "/mplru/mode", core_id);
   
   if (mode_str == "always") {
      m_mode = ALWAYS;
   } else if (mode_str == "adaptive") {
      m_mode = ADAPTIVE;
   } else {
      m_mode = DISABLED;
   }
   
   m_mpki_threshold = Sim()->getCfg()->getFloatArray(cfgname + "/mplru/mpki_threshold", core_id);
   
   // Initialize MPLRU controller for adaptive mode
   if (m_mode == ADAPTIVE) {
      UInt32 num_cores = Sim()->getConfig()->getApplicationCores();
      // Use factory to create appropriate controller type
      MPLRUControllerFactory::initialize(num_cores, cfgname);
      IMPLRUController* controller = MPLRUControllerFactory::getController();
      if (controller) {
         controller->loadConfig(core_id, cfgname);
      }
   }
   
   // Register statistics
   registerStatsMetric(name, core_id, "mplru-metadata-protected", &m_metadata_protected);
   registerStatsMetric(name, core_id, "mplru-data-evicted", &m_data_evicted);
   registerStatsMetric(name, core_id, "mplru-metadata-evicted", &m_metadata_evicted);
   registerStatsMetric(name, core_id, "mplru-data-protected", &m_data_protected);
   registerStatsMetric(name, core_id, "mplru-metadata-evicted-for-data", &m_metadata_evicted_for_data);
   
   // Create per-core logger (SimLog handles debug level internally)
   m_log = new SimLog("MPLRU-Cache", core_id, DEBUG_MPLRU);
   m_log->info("Initialized for ", name, " mode=", mode_str, " mpki_threshold=", m_mpki_threshold);
}

CacheSetInfoMPLRU::~CacheSetInfoMPLRU()
{
   // Print final MPLRU stats for this core on destruction
   if (m_log) {
      m_log->info("Final stats: metadata_protected=", m_metadata_protected,
                  " data_evicted=", m_data_evicted,
                  " metadata_evicted=", m_metadata_evicted,
                  " data_protected=", m_data_protected,
                  " metadata_evicted_for_data=", m_metadata_evicted_for_data);
      delete m_log;
   }
}

// CacheSetMPLRU implementation

CacheSetMPLRU::CacheSetMPLRU(CacheBase::cache_t cache_type,
      UInt32 associativity, UInt32 blocksize, 
      CacheSetInfoMPLRU* set_info, UInt8 num_attempts, bool is_tlb_set)
   : CacheSetLRU(cache_type, associativity, blocksize, set_info, num_attempts, is_tlb_set)
   , m_mplru_set_info(set_info)
   , m_associativity(associativity)
{
}

CacheSetMPLRU::~CacheSetMPLRU()
{
}

UInt32
CacheSetMPLRU::findLRUDataBlock()
{
   UInt32 lru_data_index = m_associativity; // Invalid
   UInt8 max_bits = 0;
   
   for (UInt32 i = 0; i < m_associativity; i++)
   {
      if (!m_cache_block_info_array[i]->isValid())
         continue;
      
      // Only consider data blocks (not PAGE_TABLE)
      if (!m_cache_block_info_array[i]->isPageTableBlock() &&
          m_lru_bits[i] > max_bits && 
          isValidReplacement(i))
      {
         lru_data_index = i;
         max_bits = m_lru_bits[i];
      }
   }
   
   return lru_data_index;
}

UInt32
CacheSetMPLRU::findLRUMetadataBlock()
{
   UInt32 lru_meta_index = m_associativity; // Invalid
   UInt8 max_bits = 0;
   
   for (UInt32 i = 0; i < m_associativity; i++)
   {
      if (!m_cache_block_info_array[i]->isValid())
         continue;
      
      // Only consider metadata blocks (PAGE_TABLE)
      if (m_cache_block_info_array[i]->isPageTableBlock() &&
          m_lru_bits[i] > max_bits && 
          isValidReplacement(i))
      {
         lru_meta_index = i;
         max_bits = m_lru_bits[i];
      }
   }
   
   return lru_meta_index;
}

UInt32
CacheSetMPLRU::findLRUDataBlockInBottomHalf()
{
   // Find LRU data block among the bottom 50% of LRU stack
   // (i.e., blocks with lru_bits > median)
   return findLRUDataBlockInPercentile(50);
}

UInt32
CacheSetMPLRU::findLRUDataBlockInPercentile(int percentile)
{
   // Find LRU data block among the bottom N% of LRU stack
   // percentile=25 means bottom 25%, percentile=50 means bottom 50%
   UInt32 lru_data_index = m_associativity; // Invalid
   UInt8 max_bits = 0;
   // threshold_bits = lru position that marks the top of the "bottom N%"
   // For 16-way: 25% → threshold = 12 (indices 12-15 are bottom 25%)
   //             50% → threshold = 8  (indices 8-15 are bottom 50%)
   UInt8 threshold_bits = (UInt8)(m_associativity * (100 - percentile) / 100);
   
   for (UInt32 i = 0; i < m_associativity; i++)
   {
      if (!m_cache_block_info_array[i]->isValid())
         continue;
      
      // Only consider data blocks in bottom N% of LRU stack
      if (!m_cache_block_info_array[i]->isPageTableBlock() &&
          m_lru_bits[i] >= threshold_bits &&
          m_lru_bits[i] > max_bits && 
          isValidReplacement(i))
      {
         lru_data_index = i;
         max_bits = m_lru_bits[i];
      }
   }
   
   return lru_data_index;
}

UInt32
CacheSetMPLRU::findLRUMetadataBlockInPercentile(int percentile)
{
   // Find LRU metadata block among the bottom N% of LRU stack
   // percentile=25 means bottom 25%, percentile=50 means bottom 50%
   // This is the symmetric operation to findLRUDataBlockInPercentile
   UInt32 lru_meta_index = m_associativity; // Invalid
   UInt8 max_bits = 0;
   // threshold_bits = lru position that marks the top of the "bottom N%"
   // For 16-way: 25% → threshold = 12 (indices 12-15 are bottom 25%)
   //             50% → threshold = 8  (indices 8-15 are bottom 50%)
   UInt8 threshold_bits = (UInt8)(m_associativity * (100 - percentile) / 100);
   
   for (UInt32 i = 0; i < m_associativity; i++)
   {
      if (!m_cache_block_info_array[i]->isValid())
         continue;
      
      // Only consider metadata blocks (PAGE_TABLE) in bottom N% of LRU stack
      if (m_cache_block_info_array[i]->isPageTableBlock() &&
          m_lru_bits[i] >= threshold_bits &&
          m_lru_bits[i] > max_bits && 
          isValidReplacement(i))
      {
         lru_meta_index = i;
         max_bits = m_lru_bits[i];
      }
   }
   
   return lru_meta_index;
}

UInt32
CacheSetMPLRU::getReplacementIndexWithDataReservation(UInt32 reserved_ways, SimLog* log)
{
   // This is symmetric to getReplacementIndexWithReservation, but reserves ways for DATA
   // instead of metadata. We evict metadata preferentially to maintain data capacity.
   
   // Count current data blocks (non-PAGE_TABLE blocks)
   UInt32 data_count = 0;
   for (UInt32 i = 0; i < m_associativity; i++)
   {
      if (m_cache_block_info_array[i]->isValid() && 
          !m_cache_block_info_array[i]->isPageTableBlock())
      {
         data_count++;
      }
   }
   
   if (log) log->trace("Data reservation: data_count=", data_count, 
                       ", reserved_ways=", reserved_ways);
   
   // If data usage is below reservation threshold, we MUST evict metadata
   // to make room for the incoming block (which might be data)
   if (data_count < reserved_ways)
   {
      UInt32 meta_index = findLRUMetadataBlock();
      if (meta_index < m_associativity)
      {
         if (log) log->trace("Below data reservation threshold, evicting metadata at index ", meta_index);
         m_mplru_set_info->incrementDataProtected();
         m_mplru_set_info->incrementMetadataEvictedForData();
         moveToMRU(meta_index);
         return meta_index;
      }
   }
   
   // Above threshold or no metadata: use metadata-first policy (like D3 semantics)
   UInt32 meta_index = findLRUMetadataBlock();
   if (meta_index < m_associativity)
   {
      if (log) log->trace("Metadata-first eviction at index ", meta_index);
      m_mplru_set_info->incrementDataProtected();
      m_mplru_set_info->incrementMetadataEvictedForData();
      moveToMRU(meta_index);
      return meta_index;
   }
   
   // No metadata blocks available, must evict data
   UInt32 data_index = findLRUDataBlock();
   if (data_index < m_associativity)
   {
      if (log) log->trace("No metadata, evicting data at index ", data_index);
      m_mplru_set_info->incrementDataEvicted();
      moveToMRU(data_index);
      return data_index;
   }
   
   // Fallback to standard LRU
   return CacheSetLRU::getReplacementIndex(nullptr);
}

int
CacheSetMPLRU::getPolicyId()
{
   CacheSetInfoMPLRU::MetadataPriorityMode mode = m_mplru_set_info->getMode();
   
   if (mode == CacheSetInfoMPLRU::DISABLED)
      return 0;  // M0 = vanilla LRU
   
   if (mode == CacheSetInfoMPLRU::ALWAYS)
      return 3;  // M3 = hard metadata bias (always evict data first)
   
   SimLog* log = m_mplru_set_info->getLog();
   if (log) log->trace("Checking controller for policy_id");

   // ADAPTIVE mode: use factory to get controller and current policy_id
   IMPLRUController* controller = MPLRUControllerFactory::getController();
   if (controller && controller->isInitialized())
   {
      // Check if we need to run epoch processing (based on instructions elapsed)
      controller->tryProcessEpoch(m_mplru_set_info->getCoreId());
      int policy_id = controller->getMetaLevel(m_mplru_set_info->getCoreId());
      // Clamp to 0-11 for 12-arm policy
      policy_id = std::clamp(policy_id, 0, 11);
      if (log) log->trace("Current policy_id=", policy_id);
      return policy_id;
   }
   
   // Controller not initialized, default to policy 0 (OFF / vanilla LRU)
   return 0;
}

// Helper function for way-reservation levels (3 and 4)
// Reserves a minimum number of ways for metadata
UInt32
CacheSetMPLRU::getReplacementIndexWithReservation(UInt32 reserved_ways, SimLog* log)
{
   // Count current metadata blocks (PAGE_TABLE blocks)
   UInt32 meta_count = 0;
   for (UInt32 i = 0; i < m_associativity; i++)
   {
      if (m_cache_block_info_array[i]->isValid() && 
          m_cache_block_info_array[i]->isPageTableBlock())
      {
         meta_count++;
      }
   }
   
   if (log) log->trace("Way reservation: meta_count=", meta_count, 
                       ", reserved_ways=", reserved_ways);
   
   // If metadata usage is below reservation threshold, we MUST evict data
   // to make room for the incoming block (which might be metadata)
   if (meta_count < reserved_ways)
   {
      UInt32 data_index = findLRUDataBlock();
      if (data_index < m_associativity)
      {
         if (log) log->trace("Below reservation threshold, evicting data at index ", data_index);
         m_mplru_set_info->incrementMetadataProtected();
         m_mplru_set_info->incrementDataEvicted();
         moveToMRU(data_index);
         return data_index;
      }
   }
   
   // Above threshold or no data: use data-first policy (like level 2)
   UInt32 data_index = findLRUDataBlock();
   if (data_index < m_associativity)
   {
      if (log) log->trace("Data-first eviction at index ", data_index);
      m_mplru_set_info->incrementMetadataProtected();
      m_mplru_set_info->incrementDataEvicted();
      moveToMRU(data_index);
      return data_index;
   }
   
   // No data blocks available, must evict metadata
   UInt32 meta_index = findLRUMetadataBlock();
   if (meta_index < m_associativity)
   {
      if (log) log->trace("No data, evicting metadata at index ", meta_index);
      m_mplru_set_info->incrementMetadataEvicted();
      moveToMRU(meta_index);
      return meta_index;
   }
   
   // Fallback to standard LRU
   return CacheSetLRU::getReplacementIndex(nullptr);
}

UInt32
CacheSetMPLRU::getReplacementIndex(CacheCntlr *cntlr)
{
   SimLog* log = m_mplru_set_info->getLog();
   
   // First try to find an invalid block
   for (UInt32 i = 0; i < m_associativity; i++)
   {
      if (!m_cache_block_info_array[i]->isValid())
      {
         if (log) log->trace("Found invalid block at index ", i);
         moveToMRU(i);
         return i;
      }
   }
   
   int policy_id = getPolicyId();
   if (log) log->trace("getReplacementIndex: policy_id=", policy_id);
   
   // ========================================
   // 12-Arm Policy (for NUCA MPKI bandit controller):
   // 
   // Arms 0-5: Metadata Protection (M0-M5) - evict data preferentially
   // Arms 6-11: Data Protection (D0-D5) - evict metadata preferentially
   //
   // M0 (0): OFF (vanilla LRU) - data and metadata compete equally
   // M1 (1): Gentle bias 25% - evict data only if in bottom 25%
   // M2 (2): Medium bias 50% - evict data only if in bottom 50%
   // M3 (3): Hard bias - always evict data first
   // M4 (4): Partition 25% - reserve 25% ways for metadata
   // M5 (5): Partition 50% - reserve 50% ways for metadata
   //
   // D0 (6): OFF (vanilla LRU) - same as M0
   // D1 (7): Gentle bias 25% - evict metadata only if in bottom 25%
   // D2 (8): Medium bias 50% - evict metadata only if in bottom 50%
   // D3 (9): Hard bias - always evict metadata first
   // D4 (10): Partition 25% - reserve 25% ways for data
   // D5 (11): Partition 50% - reserve 50% ways for data
   // ========================================
   
   // Decode policy_id into protection mode and level
   bool protect_metadata = (policy_id <= 5);
   int level = protect_metadata ? policy_id : (policy_id - 6);
   
   if (protect_metadata)
   {
      // ========================================
      // METADATA PROTECTION (M0-M5): Evict data preferentially
      // ========================================
      
      if (level == 0)  // M0
      {
         if (log) log->trace("M0: Vanilla LRU replacement");
         return CacheSetLRU::getReplacementIndex(cntlr);
      }
      else if (level == 1)  // M1
      {
         if (log) log->trace("M1: Gentle metadata protection (25%)");
         UInt32 data_index = findLRUDataBlockInPercentile(25);
         
         if (data_index < m_associativity)
         {
            m_mplru_set_info->incrementMetadataProtected();
            m_mplru_set_info->incrementDataEvicted();
            moveToMRU(data_index);
            return data_index;
         }
         return CacheSetLRU::getReplacementIndex(cntlr);
      }
      else if (level == 2)  // M2
      {
         if (log) log->trace("M2: Medium metadata protection (50%)");
         UInt32 data_index = findLRUDataBlockInPercentile(50);
         
         if (data_index < m_associativity)
         {
            m_mplru_set_info->incrementMetadataProtected();
            m_mplru_set_info->incrementDataEvicted();
            moveToMRU(data_index);
            return data_index;
         }
         return CacheSetLRU::getReplacementIndex(cntlr);
      }
      else if (level == 3)  // M3
      {
         if (log) log->trace("M3: Hard metadata protection");
         UInt32 data_index = findLRUDataBlock();
         
         if (data_index < m_associativity)
         {
            m_mplru_set_info->incrementMetadataProtected();
            m_mplru_set_info->incrementDataEvicted();
            moveToMRU(data_index);
            return data_index;
         }
         
         UInt32 meta_index = findLRUMetadataBlock();
         if (meta_index < m_associativity)
         {
            m_mplru_set_info->incrementMetadataEvicted();
            moveToMRU(meta_index);
            return meta_index;
         }
      }
      else if (level == 4)  // M4
      {
         if (log) log->trace("M4: 25% way reservation for metadata");
         UInt32 assoc = m_mplru_set_info->getAssociativity();
         UInt32 reserved_ways = assoc / 4;
         return getReplacementIndexWithReservation(reserved_ways, log);
      }
      else if (level == 5)  // M5
      {
         if (log) log->trace("M5: 50% way reservation for metadata");
         UInt32 assoc = m_mplru_set_info->getAssociativity();
         UInt32 reserved_ways = assoc / 2;
         return getReplacementIndexWithReservation(reserved_ways, log);
      }
   }
   else
   {
      // ========================================
      // DATA PROTECTION (D0-D5): Evict metadata preferentially
      // ========================================
      
      if (level == 0)  // D0
      {
         if (log) log->trace("D0: Vanilla LRU replacement");
         return CacheSetLRU::getReplacementIndex(cntlr);
      }
      else if (level == 1)  // D1
      {
         if (log) log->trace("D1: Gentle data protection (25%)");
         UInt32 meta_index = findLRUMetadataBlockInPercentile(25);
         
         if (meta_index < m_associativity)
         {
            m_mplru_set_info->incrementDataProtected();
            m_mplru_set_info->incrementMetadataEvictedForData();
            moveToMRU(meta_index);
            return meta_index;
         }
         return CacheSetLRU::getReplacementIndex(cntlr);
      }
      else if (level == 2)  // D2
      {
         if (log) log->trace("D2: Medium data protection (50%)");
         UInt32 meta_index = findLRUMetadataBlockInPercentile(50);
         
         if (meta_index < m_associativity)
         {
            m_mplru_set_info->incrementDataProtected();
            m_mplru_set_info->incrementMetadataEvictedForData();
            moveToMRU(meta_index);
            return meta_index;
         }
         return CacheSetLRU::getReplacementIndex(cntlr);
      }
      else if (level == 3)  // D3
      {
         if (log) log->trace("D3: Hard data protection");
         UInt32 meta_index = findLRUMetadataBlock();
         
         if (meta_index < m_associativity)
         {
            m_mplru_set_info->incrementDataProtected();
            m_mplru_set_info->incrementMetadataEvictedForData();
            moveToMRU(meta_index);
            return meta_index;
         }
         
         // No metadata blocks, fall back to evicting data
         UInt32 data_index = findLRUDataBlock();
         if (data_index < m_associativity)
         {
            m_mplru_set_info->incrementDataEvicted();
            moveToMRU(data_index);
            return data_index;
         }
      }
      else if (level == 4)  // D4
      {
         if (log) log->trace("D4: 25% way reservation for data");
         UInt32 assoc = m_mplru_set_info->getAssociativity();
         UInt32 reserved_ways = assoc / 4;
         return getReplacementIndexWithDataReservation(reserved_ways, log);
      }
      else if (level == 5)  // D5
      {
         if (log) log->trace("D5: 50% way reservation for data");
         UInt32 assoc = m_mplru_set_info->getAssociativity();
         UInt32 reserved_ways = assoc / 2;
         return getReplacementIndexWithDataReservation(reserved_ways, log);
      }
   }
   
   // Fall back to standard LRU
   return CacheSetLRU::getReplacementIndex(cntlr);
}
