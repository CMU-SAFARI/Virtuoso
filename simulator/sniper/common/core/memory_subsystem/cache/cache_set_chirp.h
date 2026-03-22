#ifndef CACHE_SET_CHIRP_H
#define CACHE_SET_CHIRP_H

#include "cache_set_lru.h"
#include "cache_block_info.h"

// CHiRP: control-flow-history-based dead-entry predictor for L2 TLB replacement.
//
// Uses a single prediction table of 2-bit saturating counters indexed by a
// 16-bit signature derived from four control-flow features:
//   sig = hash16( (PC >> 2) ^ path_hist ^ cond_hist ^ uncond_ind_hist )
//
// Per-entry metadata stored alongside each TLB way:
//   - chirp_sig:       16-bit signature at fill / last hit
//   - chirp_pred_dead: thresholded dead/live prediction from table
//   - chirp_first_hit: true until first hit after fill (tracks reuse)
//
// Victim selection uses the stored per-entry prediction bit:
//   1. Any invalid way (cold start)
//   2. LRU among entries with chirp_pred_dead == true
//   3. Standard LRU fallback
//
// Training:
//   On fill:  compute sig → read counter → store sig + pred; set first_hit
//   On hit:   if first_hit, decrement counter for old sig (reuse evidence);
//             recompute sig from current context → update stored sig + pred
//   On evict: if first_hit still true (never reused), increment counter
//
// The TLB access path should call the CHiRP-specific overloads that accept
// (pc) to supply real control-flow context.  The standard virtual interface
// falls back to pc=0 until the TLB integration is wired up.

class CacheSetInfoCHiRP : public CacheSetInfoLRU
{
public:
   CacheSetInfoCHiRP(String name, String cfgname, core_id_t core_id,
                     UInt32 associativity, UInt32 table_size, UInt8 threshold);
   ~CacheSetInfoCHiRP();

   // --- Prediction table operations ---
   void   incrementCounter(UInt32 index);
   void   decrementCounter(UInt32 index);
   bool   predictDead(UInt32 index) const { return m_prediction_table[index] >= m_threshold; }
   UInt32 getTableSize() const { return m_table_size; }

   // --- Control-flow history management ---
   // Call once per relevant TLB access to advance the global histories.
   void   updateHistories(UInt64 pc, bool is_cond_branch, bool is_uncond_ind_branch);
   // Compute the 16-bit CHiRP signature from current PC + histories.
   UInt16 computeSignature(UInt64 pc) const;

   // --- Stat helpers ---
   void recordEviction(bool predicted_dead, bool actually_dead, bool bypassed_lru);

private:
   static UInt16 hash16(UInt64 raw);

   UInt32 m_table_size;
   UInt8  m_threshold;
   UInt8* m_prediction_table;   // 2-bit saturating counters

   // Global control-flow histories (shared across all sets in this TLB)
   UInt64 m_path_hist;
   UInt64 m_cond_hist;
   UInt64 m_uncond_ind_hist;

   // Stats
   UInt64 m_stat_dead_correct;    // predicted dead, was dead
   UInt64 m_stat_dead_incorrect;  // predicted dead, was alive
   UInt64 m_stat_alive_correct;   // predicted alive, was alive
   UInt64 m_stat_alive_incorrect; // predicted alive, was dead
   UInt64 m_stat_bypass_lru;      // evictions that bypassed pure-LRU victim
};


class CacheSetCHiRP : public CacheSetLRU
{
public:
   CacheSetCHiRP(CacheBase::cache_t cache_type,
                 UInt32 associativity, UInt32 blocksize,
                 CacheSetInfoCHiRP* set_info, bool is_tlb_set);
   ~CacheSetCHiRP();

   // Standard virtual interface (fallback: uses pc=0)
   UInt32 getReplacementIndex(CacheCntlr *cntlr) override;
   void   updateReplacementIndex(UInt32 accessed_index) override;

   // CHiRP-specific interface with control-flow context.
   // The TLB access path should call these once wired up.
   UInt32 getReplacementIndex(CacheCntlr *cntlr, UInt64 pc);
   void   updateReplacementIndex(UInt32 accessed_index, UInt64 pc);

private:
   CacheSetInfoCHiRP* m_chirp_info;

   // Per-way CHiRP metadata (side arrays, indexed by way)
   UInt16* m_chirp_sig;        // stored signature at fill / last hit
   bool*   m_chirp_pred_dead;  // stored dead prediction bit
   bool*   m_chirp_first_hit;  // true until first hit after fill
};

#endif /* CACHE_SET_CHIRP_H */
