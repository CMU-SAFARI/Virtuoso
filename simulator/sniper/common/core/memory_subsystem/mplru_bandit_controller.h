/**
 * @file mplru_bandit_controller.h
 * @brief NUCA MPKI-based multi-armed bandit MPLRU controller
 * 
 * This controller uses NUCA MPKI as the reward signal to select among
 * 12 policy arms. It uses a simple bandit algorithm with:
 * 
 * 1. Q-value learning with EMA (β=0.2)
 * 2. Periodic neighbor probing (probe_period=10 epochs)
 * 3. Probe abort guardrail (abort if MPKI rises >15% vs best Q-value)
 * 
 * 12 Policy Arms (symmetric metadata/data protection):
 * 
 * Metadata-protection arms (M0-M5):
 *   Arm 0 → M0: OFF (vanilla LRU)
 *   Arm 1 → M1: Gentle bias (evict data if in bottom 25% LRU)
 *   Arm 2 → M2: Medium bias (evict data if in bottom 50% LRU)
 *   Arm 3 → M3: Hard bias (always evict data first)
 *   Arm 4 → M4: Partition 25% (reserve 25% ways for metadata)
 *   Arm 5 → M5: Partition 50% (reserve 50% ways for metadata)
 * 
 * Data-protection arms (D0-D5):
 *   Arm 6 → D0: OFF (vanilla LRU, same as M0)
 *   Arm 7 → D1: Gentle bias (evict metadata if in bottom 25% LRU)
 *   Arm 8 → D2: Medium bias (evict metadata if in bottom 50% LRU)
 *   Arm 9 → D3: Hard bias (always evict metadata first)
 *   Arm 10 → D4: Partition 25% (reserve 25% ways for data)
 *   Arm 11 → D5: Partition 50% (reserve 50% ways for data)
 * 
 * Key design principle:
 *   "Minimize overall NUCA MPKI using bandit exploration"
 *   - NUCA MPKI = (nuca_misses / instructions) * 1000
 *   - Reward = -MPKI (maximize negative MPKI = minimize MPKI)
 *   - Supports forced policy override via config
 */

#ifndef MPLRU_BANDIT_CONTROLLER_H
#define MPLRU_BANDIT_CONTROLLER_H

#include "mplru_controller_iface.h"
#include "fixed_types.h"
#include "subsecond_time.h"
#include <vector>
#include <mutex>

// Forward declarations
class SimLog;

/**
 * @brief NUCA MPKI-based bandit controller for MPLRU policy selection
 */
class MPLRUBanditController : public IMPLRUController
{
public:
   static constexpr int NUM_LEVELS = 12;  // Arms 0-5 (M0-M5) + 6-11 (D0-D5)
   
   /**
    * @brief Per-core bandit state
    */
   struct PerCoreState {
      // ========================================
      // Configuration
      // ========================================
      bool enabled;
      UInt64 epoch_instructions;    // Instructions per epoch (default 100K)
      float beta;                   // EMA smoothing for Q-values (default 0.2)
      int probe_period;             // Epochs between probes (default 10)
      int probe_length;             // Epochs per probe (default 5)
      float abort_threshold;        // Abort if MPKI > best * (1 + threshold) (default 0.15)
      float penalty_factor;         // Q penalty on abort (default 0.98)
      int start_level;              // Starting level (default 2 = middle)
      int warmup_epochs;            // Epochs to cycle through all levels (default 120)
      float ucb_c;                  // UCB exploration coefficient (default 1.0)
      bool use_ucb;                 // Use UCB for probe selection (default true)
      
      // ========================================
      // Force Policy Override
      // ========================================
      bool force_policy;            // If true, bypass bandit and use forced_policy_id
      int forced_policy_id;         // Policy arm to force (0-11)
      
      // ========================================
      // Runtime State
      // ========================================
      int current_level;            // Current active level [0..11]
      int best_level;               // Level with highest Q-value
      bool probing;                 // Currently in probe phase
      int probe_level;              // Level being probed
      int epochs_since_probe;       // Epochs since last probe
      int probe_epochs_remaining;   // Epochs left in current probe
      bool engaged;                 // true if level > 0 (any protection active)
      bool in_warmup;               // Currently in warmup phase
      int warmup_level;             // Current level in warmup rotation
      int warmup_epochs_remaining;  // Epochs left in warmup
      int warmup_order[NUM_LEVELS]; // Randomized order of levels for warmup
      bool warmup_order_initialized; // Whether warmup_order has been shuffled
      
      // ========================================
      // Q-values (one per arm)
      // ========================================
      float Q[NUM_LEVELS];          // Q-value estimates (EMA of -MPKI reward)
      
      // ========================================
      // NUCA MPKI tracking (replaces IPC)
      // ========================================
      UInt64 last_epoch_instructions;  // For epoch timing
      UInt64 last_instructions;        // Instructions at last epoch
      UInt64 last_nuca_misses;         // NUCA misses at last epoch
      float mpki_window;               // Current epoch's MPKI
      
      // ========================================
      // Statistics
      // ========================================
      UInt64 total_epochs;
      UInt64 probes_started;
      UInt64 probes_completed;
      UInt64 probes_aborted;
      UInt64 level_switches;
      UInt64 epochs_at_level[NUM_LEVELS];
      UInt64 n_samples[NUM_LEVELS];       // Number of samples per level (for UCB)
      
      // Stats snapshots (for StatsManager)
      UInt64 current_level_stat;
      UInt64 current_best_level_stat;
      UInt64 current_probing_stat;
      UInt64 mpki_x1000;             // MPKI * 1000 for integer stats
      UInt64 Q_x1000[NUM_LEVELS];    // Q[i] * 1000 for integer stats
      UInt64 force_policy_stat;      // 1 if force_policy is active
      UInt64 forced_policy_id_stat;  // Current forced policy ID
      
      PerCoreState();
   };
   
   MPLRUBanditController();
   virtual ~MPLRUBanditController();
   
   // IMPLRUController interface
   void initialize(UInt32 num_cores) override;
   void cleanup() override;
   void loadConfig(core_id_t core_id, const String& cfgname) override;
   void tryProcessEpoch(core_id_t core_id) override;
   int getMetaLevel(core_id_t core_id) const override;
   bool isEngaged(core_id_t core_id) const override;
   bool isInitialized() const override { return m_initialized; }
   const char* getTypeName() const override { return "bandit"; }

private:
   /**
    * @brief Process one epoch for a core
    * @param core_id Core ID
    */
   void processEpoch(core_id_t core_id);
   
   /**
    * @brief Register statistics for a core
    * @param core_id Core ID
    */
   void registerStats(core_id_t core_id);
   
   /**
    * @brief Update stats snapshots after each epoch
    * @param state Per-core state
    */
   void updateStatsSnapshots(PerCoreState& state);
   
   /**
    * @brief Choose next level to probe using UCB or neighbor selection
    * @param state Per-core state
    * @return Level to probe
    */
   int chooseProbeLevel(const PerCoreState& state);
   
   /**
    * @brief Calculate UCB score for a level
    * @param state Per-core state
    * @param level Level to calculate UCB for
    * @return UCB score = Q[level] + c * sqrt(ln(N) / n[level])
    */
   float calculateUCB(const PerCoreState& state, int level) const;
   
   std::vector<PerCoreState> m_per_core_state;
   std::vector<SimLog*> m_per_core_log;
   bool m_initialized;
   std::mutex m_mutex;
};

#endif // MPLRU_BANDIT_CONTROLLER_H
