#ifndef CACHE_SET_NRU_H
#define CACHE_SET_NRU_H

#include "cache_set.h"

class CacheSetNRU : public CacheSet
{
   public:
      CacheSetNRU(CacheBase::cache_t cache_type,
            UInt32 associativity, UInt32 blocksize, bool is_tlb_set);
      ~CacheSetNRU();

      UInt32 getReplacementIndex(CacheCntlr *cntlr);
      void updateReplacementIndex(UInt32 accessed_index);
      
      // Override to return NRU bits (0 = recently used, 1 = not recently used)
      virtual UInt8 getRecencyBits(UInt32 way) const override { return m_lru_bits[way]; }

   private:
      UInt8* m_lru_bits;
      UInt8  m_num_bits_set;
      UInt8  m_replacement_pointer;
};

#endif /* CACHE_SET_NRU_H */
