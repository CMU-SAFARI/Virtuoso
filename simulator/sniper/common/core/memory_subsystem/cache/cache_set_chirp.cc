#include "cache_set_chirp.h"
#include "log.h"
#include "stats.h"

// ---------------------------------------------------------------------------
// CacheSetInfoCHiRP — shared prediction table, histories, and stats
// ---------------------------------------------------------------------------

CacheSetInfoCHiRP::CacheSetInfoCHiRP(
      String name, String cfgname, core_id_t core_id,
      UInt32 associativity, UInt32 table_size, UInt8 threshold)
   : CacheSetInfoLRU(name, cfgname, core_id, associativity, /*num_attempts=*/1)
   , m_table_size(table_size)
   , m_threshold(threshold)
   , m_path_hist(0)
   , m_cond_hist(0)
   , m_uncond_ind_hist(0)
   , m_stat_dead_correct(0)
   , m_stat_dead_incorrect(0)
   , m_stat_alive_correct(0)
   , m_stat_alive_incorrect(0)
   , m_stat_bypass_lru(0)
{
   m_prediction_table = new UInt8[m_table_size];
   for (UInt32 i = 0; i < m_table_size; i++)
      m_prediction_table[i] = 0;

   registerStatsMetric(name, core_id, "chirp-dead-correct",    &m_stat_dead_correct);
   registerStatsMetric(name, core_id, "chirp-dead-incorrect",  &m_stat_dead_incorrect);
   registerStatsMetric(name, core_id, "chirp-alive-correct",   &m_stat_alive_correct);
   registerStatsMetric(name, core_id, "chirp-alive-incorrect", &m_stat_alive_incorrect);
   registerStatsMetric(name, core_id, "chirp-bypass-lru",      &m_stat_bypass_lru);
}

CacheSetInfoCHiRP::~CacheSetInfoCHiRP()
{
   delete [] m_prediction_table;
}

void CacheSetInfoCHiRP::incrementCounter(UInt32 index)
{
   if (m_prediction_table[index] < 3)
      m_prediction_table[index]++;
}

void CacheSetInfoCHiRP::decrementCounter(UInt32 index)
{
   if (m_prediction_table[index] > 0)
      m_prediction_table[index]--;
}

void CacheSetInfoCHiRP::recordEviction(bool predicted_dead, bool actually_dead, bool bypassed_lru)
{
   if (predicted_dead && actually_dead)
      m_stat_dead_correct++;
   else if (predicted_dead && !actually_dead)
      m_stat_dead_incorrect++;
   else if (!predicted_dead && !actually_dead)
      m_stat_alive_correct++;
   else // !predicted_dead && actually_dead
      m_stat_alive_incorrect++;

   if (bypassed_lru)
      m_stat_bypass_lru++;
}

UInt16 CacheSetInfoCHiRP::hash16(UInt64 raw)
{
   raw ^= (raw >> 32);
   raw ^= (raw >> 16);
   return static_cast<UInt16>(raw & 0xFFFF);
}

void CacheSetInfoCHiRP::updateHistories(UInt64 pc, bool is_cond_branch, bool is_uncond_ind_branch)
{
   m_path_hist = (m_path_hist << 1) ^ (pc >> 2);

   if (is_cond_branch)
      m_cond_hist = (m_cond_hist << 1) | 1;

   if (is_uncond_ind_branch)
      m_uncond_ind_hist = (m_uncond_ind_hist << 1) ^ (pc >> 2);
}

UInt16 CacheSetInfoCHiRP::computeSignature(UInt64 pc) const
{
   UInt64 raw = (pc >> 2) ^ m_path_hist ^ m_cond_hist ^ m_uncond_ind_hist;
   return hash16(raw);
}

// ---------------------------------------------------------------------------
// CacheSetCHiRP — per-set replacement logic
// ---------------------------------------------------------------------------

CacheSetCHiRP::CacheSetCHiRP(
      CacheBase::cache_t cache_type,
      UInt32 associativity, UInt32 blocksize,
      CacheSetInfoCHiRP* set_info, bool is_tlb_set)
   : CacheSetLRU(cache_type, associativity, blocksize, set_info, /*num_attempts=*/1, is_tlb_set)
   , m_chirp_info(set_info)
{
   m_chirp_sig       = new UInt16[m_associativity];
   m_chirp_pred_dead = new bool[m_associativity];
   m_chirp_first_hit = new bool[m_associativity];
   for (UInt32 i = 0; i < m_associativity; i++)
   {
      m_chirp_sig[i]       = 0;
      m_chirp_pred_dead[i] = false;
      m_chirp_first_hit[i] = true;
   }
}

CacheSetCHiRP::~CacheSetCHiRP()
{
   delete [] m_chirp_sig;
   delete [] m_chirp_pred_dead;
   delete [] m_chirp_first_hit;
}

// ---------------------------------------------------------------------------
// Standard virtual interface — fallback with pc=0
// ---------------------------------------------------------------------------

UInt32
CacheSetCHiRP::getReplacementIndex(CacheCntlr *cntlr)
{
   return getReplacementIndex(cntlr, /*pc=*/0);
}

void
CacheSetCHiRP::updateReplacementIndex(UInt32 accessed_index)
{
   updateReplacementIndex(accessed_index, /*pc=*/0);
}

// ---------------------------------------------------------------------------
// CHiRP-specific interface with control-flow context
// ---------------------------------------------------------------------------

UInt32
CacheSetCHiRP::getReplacementIndex(CacheCntlr *cntlr, UInt64 pc)
{
   const UInt32 table_size = m_chirp_info->getTableSize();

   // 1. Cold start: pick any invalid way
   for (UInt32 i = 0; i < m_associativity; i++)
   {
      if (!m_cache_block_info_array[i]->isValid())
      {
         // Initialize metadata for the incoming entry
         UInt16 sig = m_chirp_info->computeSignature(pc);
         UInt32 tbl_idx = sig % table_size;
         m_chirp_sig[i]       = sig;
         m_chirp_pred_dead[i] = m_chirp_info->predictDead(tbl_idx);
         m_chirp_first_hit[i] = true;

         moveToMRU(i);
         return i;
      }
   }

   // 2. Scan all ways — find LRU among dead-predicted entries AND overall LRU
   //    Uses the *stored* per-entry prediction bit, not re-querying the table.
   UInt32 dead_victim   = m_associativity; // sentinel: no candidate yet
   UInt8  dead_max_bits = 0;
   UInt32 lru_victim    = 0;
   UInt8  lru_max_bits  = 0;

   for (UInt32 i = 0; i < m_associativity; i++)
   {
      if (!isValidReplacement(i))
         continue;

      if (m_chirp_pred_dead[i] && m_lru_bits[i] >= dead_max_bits)
      {
         dead_victim   = i;
         dead_max_bits = m_lru_bits[i];
      }
      if (m_lru_bits[i] >= lru_max_bits)
      {
         lru_victim   = i;
         lru_max_bits = m_lru_bits[i];
      }
   }

   // Prefer dead-predicted victim; fall back to LRU
   UInt32 victim;
   bool bypassed_lru = false;
   if (dead_victim < m_associativity)
   {
      victim = dead_victim;
      if (victim != lru_victim)
         bypassed_lru = true;
   }
   else
   {
      victim = lru_victim;
   }

   // 3. Train the prediction table on the evicted entry using its stored signature
   UInt32 evict_tbl_idx   = m_chirp_sig[victim] % table_size;
   bool   was_never_reused = m_chirp_first_hit[victim]; // still true → never hit
   bool   was_predicted_dead = m_chirp_pred_dead[victim];

   if (was_never_reused)
      m_chirp_info->incrementCounter(evict_tbl_idx);  // dead evidence
   // If reused, the counter was already decremented on the first hit

   // 4. Record accuracy stats
   m_chirp_info->recordEviction(was_predicted_dead, was_never_reused, bypassed_lru);

   // 5. Initialize metadata for the incoming entry
   UInt16 new_sig = m_chirp_info->computeSignature(pc);
   UInt32 new_tbl_idx = new_sig % table_size;
   m_chirp_sig[victim]       = new_sig;
   m_chirp_pred_dead[victim] = m_chirp_info->predictDead(new_tbl_idx);
   m_chirp_first_hit[victim] = true;

   moveToMRU(victim);
   return victim;
}

void
CacheSetCHiRP::updateReplacementIndex(UInt32 accessed_index, UInt64 pc)
{
   const UInt32 table_size = m_chirp_info->getTableSize();

   // On first hit after fill: decrement counter for old stored signature (reuse evidence)
   if (m_chirp_first_hit[accessed_index])
   {
      UInt32 old_tbl_idx = m_chirp_sig[accessed_index] % table_size;
      m_chirp_info->decrementCounter(old_tbl_idx);
      m_chirp_first_hit[accessed_index] = false;
   }

   // Update stored signature and prediction from current control-flow context
   UInt16 new_sig = m_chirp_info->computeSignature(pc);
   UInt32 new_tbl_idx = new_sig % table_size;
   m_chirp_sig[accessed_index]       = new_sig;
   m_chirp_pred_dead[accessed_index] = m_chirp_info->predictDead(new_tbl_idx);

   // Delegate LRU update + per-position stats to parent
   CacheSetLRU::updateReplacementIndex(accessed_index);
}
