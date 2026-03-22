#ifndef CACHE_SET_NMRU_H
#define CACHE_SET_NMRU_H

#include "cache_set.h"

class CacheSetNMRU : public CacheSet
{
   public:
      CacheSetNMRU(CacheBase::cache_t cache_type,
            UInt32 associativity, UInt32 blocksize, bool is_tlb_set);
      ~CacheSetNMRU();

      UInt32 getReplacementIndex(CacheCntlr *cntlr);
      void updateReplacementIndex(UInt32 accessed_index);
      
      // Override to return LRU bits (higher = older)
      virtual UInt8 getRecencyBits(UInt32 way) const override { return m_lru_bits[way]; }

   private:
      UInt8* m_lru_bits;
      UInt8  m_replacement_pointer;
};

#endif /* CACHE_SET_NMRU_H */
