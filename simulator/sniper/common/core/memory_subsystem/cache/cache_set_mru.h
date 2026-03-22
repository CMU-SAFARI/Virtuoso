#ifndef CACHE_SET_MRU_H
#define CACHE_SET_MRU_H

#include "cache_set.h"

class CacheSetMRU : public CacheSet
{
   public:
      CacheSetMRU(CacheBase::cache_t cache_type,
            UInt32 associativity, UInt32 blocksize, bool is_tlb_set);
      ~CacheSetMRU();

      UInt32 getReplacementIndex(CacheCntlr *cntlr);
      void updateReplacementIndex(UInt32 accessed_index);
      
      // Override to return LRU bits (higher = older)
      virtual UInt8 getRecencyBits(UInt32 way) const override { return m_lru_bits[way]; }

   private:
      UInt8* m_lru_bits;
};

#endif /* CACHE_SET_MRU_H */
