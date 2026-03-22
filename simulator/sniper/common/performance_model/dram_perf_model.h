#ifndef __DRAM_PERF_MODEL_H__
#define __DRAM_PERF_MODEL_H__

#include "queue_model.h"
#include "fixed_types.h"
#include "subsecond_time.h"
#include "dram_cntlr_interface.h"
#include "address_home_lookup.h"

class ShmemPerf;

// Note: Each Dram Controller owns a single DramModel object
// Hence, m_dram_bandwidth is the bandwidth for a single DRAM controller
// Total Bandwidth = m_dram_bandwidth * Number of DRAM controllers
// Number of DRAM controllers presently = Number of Cores
// m_dram_bandwidth is expressed in GB/s
// Assuming the frequency of a core is 1GHz,
// m_dram_bandwidth is also expressed in 'Bytes per clock cycle'
// This DRAM model is not entirely correct.
// It sort of increases the queueing delay to a huge value if
// the arrival times of adjacent packets are spread over a large
// simulated time period
class DramPerfModel
{
   protected:
      bool m_enabled;
      UInt64 m_num_accesses;
      String m_suffix;  // Per-instance suffix for unique stat names (e.g., "-numa1")

   public:
      // Factory methods
      static DramPerfModel* createDramPerfModel(core_id_t core_id, UInt32 cache_block_size, 
                                                 AddressHomeLookup* address_home_lookup);
      
      // Factory with explicit type parameter (for tiered/heterogeneous memory)
      static DramPerfModel* createDramPerfModel(core_id_t core_id, UInt32 cache_block_size,
                                                 AddressHomeLookup* address_home_lookup,
                                                 const String& type,
                                                 const String& suffix = "");

      DramPerfModel(core_id_t core_id, UInt64 cache_block_size, const String& suffix = "")
         : m_enabled(false), m_num_accesses(0), m_suffix(suffix) {}
      virtual ~DramPerfModel() {}
      virtual SubsecondTime getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size, core_id_t requester, IntPtr address, DramCntlrInterface::access_t access_type, ShmemPerf *perf,bool is_metadata) = 0;
      void enable() { m_enabled = true; }
      void disable() { m_enabled = false; }
        virtual SubsecondTime getAccessLatencyUnmodelled(SubsecondTime pkt_time,UInt64 pkt_size, core_id_t requester,IntPtr address){return SubsecondTime::Zero();};
      UInt64 getTotalAccesses() { return m_num_accesses; }
};

#endif /* __DRAM_PERF_MODEL_H__ */
