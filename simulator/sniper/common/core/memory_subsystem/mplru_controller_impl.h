/**
 * @file mplru_controller.h
 * @brief Adaptive MPLRU controller with Δ-based push-then-backoff policy
 * 
 * This controller determines the metadata priority level (meta_level) for MPLRU
 * based on translation vs data stall analysis per epoch.
 * 
 * Key features:
 * 1. Engage gate with hysteresis: Only prioritize translation when it's worth optimizing
 *    - tr_share >= tr_share_min (translation stalls ≥ 1% of cycles)
 *    - OR rho >= rho_min (translation not ultra-low relative to data)
 *    - Uses EMA-smoothed values to avoid thrash
 *    - Hysteresis: engage after 2 epochs above threshold, disengage after 4 below
 * 
 * 2. Exploration phase: When first engaged, explore aggressively
 *    - For first N epochs after engage, push meta_level unless severe data pain
 *    - Ensures we "try prioritizing translation" before evaluating
 * 
 * 3. Δ-based controller: When engaged (post-exploration), use marginal-effect control
 *    - Back off when data pain rises AND translation gains saturate
 *    - Cooldown prevents oscillations
 * 
 * 4. meta_level mapping (6 levels):
 *    - 0: vanilla LRU
 *    - 1: protect metadata if data victim in bottom 50% LRU
 *    - 2: always evict data if any exists
 *    - 3: 25% way reservation (reserve 4 ways in 16-way cache)
 *    - 4: 50% way reservation (reserve 8 ways in 16-way cache)
 *    - 5: 75% way reservation (reserve 12 ways in 16-way cache)
 */

#ifndef MPLRU_CONTROLLER_IMPL_H
#define MPLRU_CONTROLLER_IMPL_H

#include "fixed_types.h"
#include "subsecond_time.h"
#include <vector>
#include <mutex>

class MPLRUController
{
public:
   /**
    * @brief Per-core controller state and configuration
    */
   struct PerCoreState {
      // ========================================
      // Configuration (from config file)
      // ========================================
      bool enabled;
      UInt64 epoch_instructions;     // Instructions between epochs
      float tr_share_min;            // Min translation share of cycles to engage (default 0.01)
      float rho_min;                 // Min translation/data ratio to engage (default 0.02)
      int meta_level_start;          // Starting level when engaged (default 2)
      int meta_level_max;            // Maximum meta_level (default 5)
      float data_pain_frac;          // Fractional increase in data stalls triggering pain (default 0.05)
      float tr_gain_frac;            // Fractional decrease in translation stalls needed (default 0.03)
      int cooldown_epochs;           // Epochs between level changes (default 2)
      int max_step_per_epoch;        // Max level change per epoch (default 1)
      float ema_alpha;               // EMA smoothing factor (default 0.2)
      int explore_epochs;            // Exploration epochs after engage (default 3)
      float severe_data_pain_mult;   // Multiplier for "severe" data pain during exploration (default 2.0)
      
      // ========================================
      // Absolute Guardrail Configuration
      // ========================================
      bool abs_guard_enabled;        // Enable absolute data share guardrail (default true)
      float data_share_max;          // Max allowed data share before backing off (default 0.995)
      int abs_guard_epochs;          // Consecutive epochs above threshold to trigger (default 2)
      int abs_guard_backoff_step;    // How many levels to back off when triggered (default 2)
      
      // ========================================
      // Net Benefit Configuration
      // ========================================
      bool net_benefit_enabled;      // Enable net benefit test (default true)
      float harm_to_benefit_ratio;   // Max harm/benefit ratio to continue pushing (default 1.5)
      float min_benefit_cycles;      // Ignore tiny benefits below this threshold (default 1000)
      bool level_aware_guard;        // Stricter guardrails at high levels (default true)
      float high_level_threshold;    // Level above which to apply stricter rules (default 10)
      float high_level_ratio_mult;   // Multiply harm_to_benefit_ratio at high levels (default 0.75)
      
      // ========================================
      // Hysteresis state
      // ========================================
      int engage_above_count;        // Consecutive epochs above engage threshold
      int engage_below_count;        // Consecutive epochs below engage threshold
      int engage_threshold_epochs;   // Epochs above threshold needed to engage (default 2)
      int disengage_threshold_epochs; // Epochs below threshold needed to disengage (default 4)
      
      // ========================================
      // Runtime state
      // ========================================
      int meta_level;                // Current metadata priority level [0..meta_level_max]
      bool engaged;                  // Whether translation-priority is currently engaged
      int cooldown_remaining;        // Epochs remaining before next level change
      int exploration_remaining;     // Epochs remaining in exploration phase
      int abs_guard_violation_count; // Consecutive epochs with data_share > data_share_max
      float data_share_ema;          // EMA of data share (S_data_ema / C_ema)
      
      // ========================================
      // Epoch timing (instructions)
      // ========================================
      UInt64 last_epoch_instruction_marker;  // Instruction count at last epoch boundary
      
      // ========================================
      // Stall tracking (cumulative, in cycles)
      // ========================================
      UInt64 cumulative_translation_cycles;
      UInt64 cumulative_data_cycles;
      UInt64 cumulative_total_cycles;
      
      // Last epoch baselines (for delta calculation)
      UInt64 last_translation_cycles;
      UInt64 last_data_cycles;
      UInt64 last_total_cycles;
      
      // ========================================
      // EMA values (smoothed per-epoch stalls)
      // ========================================
      float S_tr_ema;            // EMA of translation stalls per epoch
      float S_data_ema;          // EMA of data stalls per epoch
      float C_ema;               // EMA of total cycles per epoch
      float prev_S_tr_ema;       // Previous EMA for delta calculation
      float prev_S_data_ema;     // Previous EMA for delta calculation
      
      // ========================================
      // Debug/stats counters (registered as metrics)
      // ========================================
      UInt64 engaged_epochs;
      UInt64 disengaged_epochs;
      UInt64 exploration_epochs;
      UInt64 level_ups;
      UInt64 level_downs;
      UInt64 data_pain_events;
      UInt64 tr_gain_events;
      UInt64 total_epochs;           // Total epochs processed
      UInt64 cooldown_epochs_spent;  // Epochs spent in cooldown
      UInt64 hold_decisions;         // Epochs where level was held (no change)
      
      // New guardrail/net-benefit stats
      UInt64 abs_guard_triggers;     // Times absolute guardrail triggered backoff
      UInt64 net_benefit_backoffs;   // Times net benefit test caused backoff
      UInt64 net_benefit_pushes;     // Times net benefit test allowed push
      UInt64 net_benefit_holds;      // Times net benefit test caused hold
      
      // Per-level epoch counters (time spent at each level)
      UInt64 epochs_at_level[16];    // Epochs spent at meta_level 0-15
      
      // Cumulative stall tracking (for total stall reporting)
      UInt64 total_translation_stalls;  // Total translation stalls observed
      UInt64 total_data_stalls;         // Total data stalls observed
      
      // Current state (snapshot for stats)
      UInt64 current_meta_level;        // Current meta_level (as UInt64 for stats)
      UInt64 current_engaged;           // Currently engaged (0 or 1)
      
      // EMA snapshots (scaled to integers for stats, *1000)
      UInt64 tr_share_x1000;            // tr_share * 1000 (for integer stats)
      UInt64 rho_x1000;                 // rho * 1000 (for integer stats)
      UInt64 S_tr_ema_scaled;           // S_tr_ema (raw cycles)
      UInt64 S_data_ema_scaled;         // S_data_ema (raw cycles)
      
      // Delta tracking (magnitude scaled *1000, sign indicates direction)
      // Note: Using unsigned with "improving" variants for stats compatibility
      UInt64 delta_tr_magnitude_x1000;    // |Δ_tr / S_tr| * 1000
      UInt64 delta_data_magnitude_x1000;  // |Δ_data / S_data| * 1000
      UInt64 tr_improving;                // 1 if translation improved (Δ_tr < 0), else 0
      UInt64 data_worsening;              // 1 if data got worse (Δ_data > 0), else 0
      
      // Legacy float stats (for internal use)
      float last_tr_share;       // For stats reporting
      float last_rho;            // For stats reporting
      
      PerCoreState();
   };
   
   /**
    * @brief Initialize the controller for a given number of cores
    * @param num_cores Number of cores in the system
    */
   static void initialize(UInt32 num_cores);
   
   /**
    * @brief Clean up the controller
    */
   static void cleanup();
   
   /**
    * @brief Check if the controller is initialized
    */
   static bool isInitialized() { return s_initialized; }
   
   /**
    * @brief Load configuration for a core
    * @param core_id Core ID
    * @param cfgname Config path prefix (e.g., "perf_model/nuca")
    */
   static void loadConfig(core_id_t core_id, const String& cfgname);
   
   /**
    * @brief Register statistics for a core with the stats manager
    * @param core_id Core ID
    */
   static void registerStats(core_id_t core_id);
   
   /**
    * @brief Run epoch processing for a core (called periodically)
    * @param core_id Core ID
    * 
    * This computes deltas, updates EMAs, checks engage condition,
    * and adjusts meta_level according to the Δ-controller.
    */
   static void processEpoch(core_id_t core_id);
   
   /**
    * @brief Try to process epoch if enough cycles have passed
    * @param core_id Core ID
    * 
    * This method checks internally if it's time for a new epoch
    * (based on cycle count from MimicOS) and calls processEpoch if needed.
    * Should be called by the replacement policy on each access.
    */
   static void tryProcessEpoch(core_id_t core_id);
   
   /**
    * @brief Get the current meta_level for a core
    * @param core_id Core ID
    * @return Current metadata priority level [0..meta_level_max]
    */
   static int getMetaLevel(core_id_t core_id);
   
   /**
    * @brief Check if translation-priority is engaged for a core
    * @param core_id Core ID
    * @return true if engaged
    */
   static bool isEngaged(core_id_t core_id);
   
   /**
    * @brief Get the per-core state (for stats/debugging)
    * @param core_id Core ID
    * @return Pointer to per-core state, or nullptr if invalid
    */
   static const PerCoreState* getState(core_id_t core_id);

private:
   static std::vector<PerCoreState> s_per_core_state;
   static std::vector<class SimLog*> s_per_core_log;  // Per-core loggers
   static bool s_initialized;
   static std::mutex s_mutex;
};

#endif // MPLRU_CONTROLLER_IMPL_H
