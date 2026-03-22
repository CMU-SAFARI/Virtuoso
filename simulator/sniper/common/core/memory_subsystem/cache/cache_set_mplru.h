#ifndef CACHE_SET_MPLRU_H
#define CACHE_SET_MPLRU_H

#include "cache_set_lru.h"

// Forward declarations
class SimLog;
class IMPLRUController;

/**
 * Metadata-Priority LRU (MPLRU) Cache Replacement Policy
 * 
 * When TLB pressure is high (measured by TLB MPKI), this policy protects
 * metadata (PAGE_TABLE blocks) from eviction by preferring to evict data blocks.
 * 
 * This is designed for NUCA caches in Utopia-like systems where FPA/TAR
 * metadata competes with regular data for cache space.
 * 
 * Modes:
 *   - ALWAYS:    Always prefer evicting data over metadata
 *   - ADAPTIVE:  Only protect metadata when TLB MPKI exceeds threshold
 *   - DISABLED:  Standard LRU (equivalent to CacheSetLRU)
 */

class CacheSetInfoMPLRU : public CacheSetInfoLRU
{
   public:
      enum MetadataPriorityMode {
         ALWAYS,     // Always protect metadata
         ADAPTIVE,   // Protect when TLB MPKI is high
         DISABLED    // Standard LRU
      };
      
      CacheSetInfoMPLRU(String name, String cfgname, core_id_t core_id, 
                        UInt32 associativity, UInt8 num_attempts);
      virtual ~CacheSetInfoMPLRU();
      
      MetadataPriorityMode getMode() const { return m_mode; }
      float getMPKIThreshold() const { return m_mpki_threshold; }
      core_id_t getCoreId() const { return m_core_id; }
      UInt32 getAssociativity() const { return m_associativity; }
      
      // Statistics for metadata protection
      void incrementMetadataProtected() { ++m_metadata_protected; }
      void incrementDataEvicted() { ++m_data_evicted; }
      void incrementMetadataEvicted() { ++m_metadata_evicted; }
      
      // Statistics for data protection (new)
      void incrementDataProtected() { ++m_data_protected; }
      void incrementMetadataEvictedForData() { ++m_metadata_evicted_for_data; }
      
      // Logger access
      SimLog* getLog() const { return m_log; }
      
   private:
      core_id_t m_core_id;
      UInt32 m_associativity;
      MetadataPriorityMode m_mode;
      float m_mpki_threshold;
      SimLog* m_log;
      
      // Metadata protection stats
      UInt64 m_metadata_protected;
      UInt64 m_data_evicted;
      UInt64 m_metadata_evicted;
      
      // Data protection stats (new)
      UInt64 m_data_protected;
      UInt64 m_metadata_evicted_for_data;
};

class CacheSetMPLRU : public CacheSetLRU
{
   public:
      CacheSetMPLRU(CacheBase::cache_t cache_type,
            UInt32 associativity, UInt32 blocksize, 
            CacheSetInfoMPLRU* set_info, UInt8 num_attempts, bool is_tlb_set);
      virtual ~CacheSetMPLRU();

      virtual UInt32 getReplacementIndex(CacheCntlr *cntlr) override;
      
      UInt32 getAssociativity() const { return m_associativity; }

   private:
      CacheSetInfoMPLRU* m_mplru_set_info;
      UInt32 m_associativity;
      
      // Get current policy_id from controller (0-11)
      // 0-5: M0-M5 (metadata protection)
      // 6-11: D0-D5 (data protection)
      int getPolicyId();
      
      // === Metadata protection helpers (M0-M5) ===
      // Find LRU data block (returns associativity if none found)
      UInt32 findLRUDataBlock();
      // Find LRU data block in bottom N% of LRU (for bias levels)
      UInt32 findLRUDataBlockInPercentile(int percentile);
      // Find LRU data block in bottom half of LRU (legacy, for meta_level 1)
      UInt32 findLRUDataBlockInBottomHalf();
      // Find LRU metadata block (returns associativity if none found)  
      UInt32 findLRUMetadataBlock();
      // Helper for metadata way-reservation levels (M4 and M5)
      UInt32 getReplacementIndexWithReservation(UInt32 reserved_ways, SimLog* log);
      
      // === Data protection helpers (D0-D5) ===
      // Find LRU metadata block in bottom N% of LRU (for bias levels)
      UInt32 findLRUMetadataBlockInPercentile(int percentile);
      // Helper for data way-reservation levels (D4 and D5)
      UInt32 getReplacementIndexWithDataReservation(UInt32 reserved_ways, SimLog* log);
      
      // Check if metadata should be protected based on current TLB pressure
      bool shouldProtectMetadata();
};

#endif /* CACHE_SET_MPLRU_H */
