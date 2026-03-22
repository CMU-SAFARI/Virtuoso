#pragma once

#include "fixed_types.h"
#include "hit_where.h"
#include "subsecond_time.h"
#include "core.h"
#include "cache_block_info.h"

/**
 * @brief Abstract interface for cache controllers accessed by the MMU subsystem.
 *
 * This interface decouples the MMU from any specific cache controller implementation,
 * allowing both the parametric (coherent) and fast_detailed (non-coherent) memory
 * managers to provide cache access for page table walks.
 */
class MMUCacheInterface
{
public:
   /**
    * @brief Process a memory operation from the MMU (page table walk cache access).
    *
    * @return HitWhere::where_t indicating where the access hit (L1, L2, LLC, DRAM, etc.)
    */
   virtual HitWhere::where_t handleMMUCacheAccess(
       IntPtr eip,
       Core::lock_signal_t lock_signal,
       Core::mem_op_t mem_op_type,
       IntPtr ca_address, UInt32 offset,
       Byte *data_buf, UInt32 data_length,
       bool modeled, bool count,
       CacheBlockInfo::block_type_t block_type,
       SubsecondTime t_start) = 0;

   /**
    * @brief Issue a prefetch from the MMU.
    */
   virtual void handleMMUPrefetch(
       IntPtr eip, IntPtr prefetch_address,
       SubsecondTime t_start,
       CacheBlockInfo::block_type_t block_type = CacheBlockInfo::block_type_t::DATA)
   {
      // Default: no-op (prefetching not supported in all memory managers)
   }

   /**
    * @brief Return the completion time of the most recent prefetch.
    *
    * Used by the speculative engine to compute timing deltas.
    * Default returns Zero for implementations that don't track prefetch timing.
    */
   virtual SubsecondTime getLastPrefetchCompletion() const
   {
      return SubsecondTime::Zero();
   }

   virtual ~MMUCacheInterface() = default;
};
