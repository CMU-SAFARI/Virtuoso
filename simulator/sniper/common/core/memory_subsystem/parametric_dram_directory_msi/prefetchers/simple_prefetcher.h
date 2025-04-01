#ifndef __SIMPLE_PREFETCHER_H
#define __SIMPLE_PREFETCHER_H

#include "prefetcher.h"

class SimplePrefetcher : public Prefetcher
{
   public:
      SimplePrefetcher(String configName, core_id_t core_id, UInt32 shared_cores);
      std::vector<IntPtr> getNextAddress(IntPtr current_address, core_id_t core_id,Core::mem_op_t mem_op_type, bool cache_hit, bool prefetch_hit, IntPtr eip);

   private:
      const core_id_t core_id;
      const UInt32 shared_cores;
      const UInt32 n_flows;
      const bool flows_per_core;
      const UInt32 num_prefetches;
      const bool stop_at_page;
      UInt32 n_flow_next;
      std::vector<std::vector<IntPtr> > m_prev_address;
};

#endif // __SIMPLE_PREFETCHER_H
