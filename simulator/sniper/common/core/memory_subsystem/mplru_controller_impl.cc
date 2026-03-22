/**
 * @file mplru_controller.cc
 * @brief Implementation of the adaptive MPLRU controller
 * 
 * ============================================================================
 * WHAT THIS CONTROLLER ACHIEVES
 * ============================================================================
 * 
 * 1. CONDITIONAL TRANSLATION PRIORITY
 *    The engage gate ensures we don't starve data to protect metadata when
 *    translation is basically irrelevant (e.g., translation stalls ~0%).
 *    This prevents the common failure mode of always protecting metadata
 *    even when it doesn't matter.
 * 
 * 2. MARGINAL BENEFIT TEST
 *    The Δ-controller asks: "Is prioritizing metadata still worth it?"
 *    - If translation stalls going down (good) AND data not rising (safe) → push
 *    - If data stalls rising (bad) AND translation not improving (no benefit) → back off
 *    This is "try prioritizing translation, adapt if it hurts data without benefit."
 * 
 * 3. STABILITY MECHANISMS
 *    EMA + cooldown + max step reduce oscillations from noisy per-epoch counters.
 * 
 * ============================================================================
 * KEY INSIGHT: OPTIMIZING DERIVATIVES, NOT RAW METRICS
 * ============================================================================
 * 
 * The controller does NOT chase a fixed rho (tr/data ratio) target.
 * That would be wrong because: if translation gets faster and data gets slower,
 * rho goes down—but that's actually success, not failure!
 * 
 * Instead, we optimize the DERIVATIVES (Δ): the effect of changing meta_level
 * on stalls. We're chasing "translation improvement without data pain."
 * 
   * ============================================================================
   */

#include "mplru_controller_impl.h"
#include "simulator.h"
#include "config.hpp"
#include "stats.h"
#include "mimicos.h"
#include "sim_log.h"
#include "debug_config.h"
#include <iostream>
#include <algorithm>
#include <cmath>

// Static member initialization
std::vector<MPLRUController::PerCoreState> MPLRUController::s_per_core_state;
std::vector<SimLog*> MPLRUController::s_per_core_log;  // Per-core loggers
bool MPLRUController::s_initialized = false;
std::mutex MPLRUController::s_mutex;

// ============================================================================
// PerCoreState constructor
// ============================================================================

MPLRUController::PerCoreState::PerCoreState()
   // Configuration defaults
   : enabled(true)
   , epoch_instructions(100000)  // 100K instructions per epoch
   , tr_share_min(0.01f)
   , rho_min(0.02f)
   , meta_level_start(2)
   , meta_level_max(15)
   , data_pain_frac(0.05f)
   , tr_gain_frac(0.03f)
   , cooldown_epochs(2)
   , max_step_per_epoch(1)
   , ema_alpha(0.2f)
   , explore_epochs(3)
   , severe_data_pain_mult(2.0f)
   // Absolute Guardrail defaults
   , abs_guard_enabled(true)
   , data_share_max(0.995f)
   , abs_guard_epochs(2)
   , abs_guard_backoff_step(2)
   // Net Benefit defaults
   , net_benefit_enabled(true)
   , harm_to_benefit_ratio(1.5f)
   , min_benefit_cycles(1000.0f)
   , level_aware_guard(true)
   , high_level_threshold(10.0f)
   , high_level_ratio_mult(0.75f)
   // Hysteresis
   , engage_above_count(0)
   , engage_below_count(0)
   , engage_threshold_epochs(2)
   , disengage_threshold_epochs(4)
   // Runtime state
   , meta_level(0)
   , engaged(false)
   , cooldown_remaining(0)
   , exploration_remaining(0)
   , abs_guard_violation_count(0)
   , data_share_ema(0.0f)
   // Epoch timing
   , last_epoch_instruction_marker(0)
   // Stall tracking
   , cumulative_translation_cycles(0)
   , cumulative_data_cycles(0)
   , cumulative_total_cycles(0)
   , last_translation_cycles(0)
   , last_data_cycles(0)
   , last_total_cycles(0)
   // EMA
   , S_tr_ema(0.0f)
   , S_data_ema(0.0f)
   , C_ema(0.0f)
   , prev_S_tr_ema(0.0f)
   , prev_S_data_ema(0.0f)
   // Stats
   , engaged_epochs(0)
   , disengaged_epochs(0)
   , exploration_epochs(0)
   , level_ups(0)
   , level_downs(0)
   , data_pain_events(0)
   , tr_gain_events(0)
   , total_epochs(0)
   , cooldown_epochs_spent(0)
   , hold_decisions(0)
   // New guardrail/net-benefit stats
   , abs_guard_triggers(0)
   , net_benefit_backoffs(0)
   , net_benefit_pushes(0)
   , net_benefit_holds(0)
   // Per-level counters initialized below
   , total_translation_stalls(0)
   , total_data_stalls(0)
   , current_meta_level(0)
   , current_engaged(0)
   , tr_share_x1000(0)
   , rho_x1000(0)
   , S_tr_ema_scaled(0)
   , S_data_ema_scaled(0)
   , delta_tr_magnitude_x1000(0)
   , delta_data_magnitude_x1000(0)
   , tr_improving(0)
   , data_worsening(0)
   , last_tr_share(0.0f)
   , last_rho(0.0f)
{
   // Initialize per-level epoch counters
   for (int i = 0; i < 16; i++) {
      epochs_at_level[i] = 0;
   }
}

// ============================================================================
// Initialization / Cleanup
// ============================================================================

void MPLRUController::initialize(UInt32 num_cores)
{
   std::lock_guard<std::mutex> lock(s_mutex);
   
   if (s_initialized) {
      return;
   }
   
   s_per_core_state.resize(num_cores);
   
   // Create per-core loggers (SimLog handles debug level internally)
   s_per_core_log.resize(num_cores, nullptr);
   for (UInt32 i = 0; i < num_cores; i++) {
      s_per_core_log[i] = new SimLog("MPLRU", i, DEBUG_MPLRU);
   }
   if (num_cores > 0 && s_per_core_log[0]) {
      s_per_core_log[0]->info("Initialized for ", num_cores, " cores");
   }
   
   s_initialized = true;
}

void MPLRUController::cleanup()
{
   std::lock_guard<std::mutex> lock(s_mutex);
   
   // Print final stats
   for (size_t i = 0; i < s_per_core_state.size(); i++) {
      const PerCoreState& state = s_per_core_state[i];
      if (state.enabled && (state.engaged_epochs > 0 || state.disengaged_epochs > 0)) {
         // Log final stats to file
         if (s_per_core_log[i]) {
            s_per_core_log[i]->info("═══════════════════════════════════════════");
            s_per_core_log[i]->info("FINAL STATS");
            s_per_core_log[i]->info("  engaged_epochs=", state.engaged_epochs);
            s_per_core_log[i]->info("  disengaged_epochs=", state.disengaged_epochs);
            s_per_core_log[i]->info("  exploration_epochs=", state.exploration_epochs);
            s_per_core_log[i]->info("  level_ups=", state.level_ups);
            s_per_core_log[i]->info("  level_downs=", state.level_downs);
            s_per_core_log[i]->info("  data_pain_events=", state.data_pain_events);
            s_per_core_log[i]->info("  tr_gain_events=", state.tr_gain_events);
            s_per_core_log[i]->info("  final_level=", state.meta_level);
            s_per_core_log[i]->info("═══════════════════════════════════════════");
         }
      }
   }
   
   // Cleanup loggers
   for (size_t i = 0; i < s_per_core_log.size(); i++) {
      delete s_per_core_log[i];
   }
   s_per_core_log.clear();
   
   s_per_core_state.clear();
   s_initialized = false;
}

// ============================================================================
// Configuration
// ============================================================================

void MPLRUController::loadConfig(core_id_t core_id, const String& cfgname)
{
   if (!s_initialized || core_id >= (core_id_t)s_per_core_state.size()) {
      return;
   }
   
   PerCoreState& state = s_per_core_state[core_id];
   
   // Helper lambda for safe config reading
   auto getBool = [&](const String& key, bool def) -> bool {
      try { return Sim()->getCfg()->getBoolArray(cfgname + "/mplru/controller/" + key, core_id); }
      catch (...) { return def; }
   };
   auto getInt = [&](const String& key, int def) -> int {
      try { return Sim()->getCfg()->getIntArray(cfgname + "/mplru/controller/" + key, core_id); }
      catch (...) { return def; }
   };
   auto getFloat = [&](const String& key, float def) -> float {
      try { return Sim()->getCfg()->getFloatArray(cfgname + "/mplru/controller/" + key, core_id); }
      catch (...) { return def; }
   };
   
   state.enabled = getBool("enabled", true);
   state.epoch_instructions = getInt("epoch_instructions", 100000);
   state.tr_share_min = getFloat("tr_share_min", 0.01f);
   state.rho_min = getFloat("rho_min", 0.02f);
   state.meta_level_start = getInt("meta_level_start", 2);
   state.meta_level_max = getInt("meta_level_max", 15);
   state.data_pain_frac = getFloat("data_pain_frac", 0.05f);
   state.tr_gain_frac = getFloat("tr_gain_frac", 0.03f);
   state.cooldown_epochs = getInt("cooldown_epochs", 2);
   state.max_step_per_epoch = getInt("max_step_per_epoch", 1);
   state.ema_alpha = getFloat("ema_alpha", 0.2f);
   state.explore_epochs = getInt("explore_epochs", 3);
   state.severe_data_pain_mult = getFloat("severe_data_pain_mult", 2.0f);
   state.engage_threshold_epochs = getInt("engage_threshold_epochs", 2);
   state.disengage_threshold_epochs = getInt("disengage_threshold_epochs", 4);
   
   // Absolute Guardrail config
   state.abs_guard_enabled = getBool("abs_guard_enabled", true);
   state.data_share_max = getFloat("data_share_max", 0.995f);
   state.abs_guard_epochs = getInt("abs_guard_epochs", 2);
   state.abs_guard_backoff_step = getInt("abs_guard_backoff_step", 2);
   
   // Net Benefit config
   state.net_benefit_enabled = getBool("net_benefit_enabled", true);
   state.harm_to_benefit_ratio = getFloat("harm_to_benefit_ratio", 1.5f);
   state.min_benefit_cycles = getFloat("min_benefit_cycles", 1000.0f);
   
   // Level-Aware Guardrails config
   state.level_aware_guard = getBool("level_aware_guard", true);
   state.high_level_threshold = getFloat("high_level_threshold", 10.0f);
   state.high_level_ratio_mult = getFloat("high_level_ratio_mult", 0.75f);
   
   if (core_id == 0 && core_id < (core_id_t)s_per_core_log.size() && s_per_core_log[core_id]) {
      s_per_core_log[core_id]->info("Config loaded: enabled=", state.enabled,
                " epoch_instructions=", state.epoch_instructions,
                " tr_share_min=", state.tr_share_min,
                " rho_min=", state.rho_min,
                " meta_level_start=", state.meta_level_start,
                " meta_level_max=", state.meta_level_max,
                " explore_epochs=", state.explore_epochs);
      s_per_core_log[core_id]->info("Guardrail config: abs_guard_enabled=", state.abs_guard_enabled,
                " data_share_max=", state.data_share_max,
                " net_benefit_enabled=", state.net_benefit_enabled,
                " harm_to_benefit_ratio=", state.harm_to_benefit_ratio);
   }
   
   // Register stats after loading config
   registerStats(core_id);
}

// ============================================================================
// Statistics Registration
// ============================================================================

void MPLRUController::registerStats(core_id_t core_id)
{
   if (!s_initialized || core_id >= (core_id_t)s_per_core_state.size()) {
      return;
   }
   
   PerCoreState& state = s_per_core_state[core_id];
   String name = "mplru-controller";
   
   // ========================================
   // Engagement and phase tracking
   // ========================================
   registerStatsMetric(name, core_id, "total-epochs", &state.total_epochs);
   registerStatsMetric(name, core_id, "engaged-epochs", &state.engaged_epochs);
   registerStatsMetric(name, core_id, "disengaged-epochs", &state.disengaged_epochs);
   registerStatsMetric(name, core_id, "exploration-epochs", &state.exploration_epochs);
   registerStatsMetric(name, core_id, "cooldown-epochs-spent", &state.cooldown_epochs_spent);
   
   // ========================================
   // Level change tracking
   // ========================================
   registerStatsMetric(name, core_id, "level-ups", &state.level_ups);
   registerStatsMetric(name, core_id, "level-downs", &state.level_downs);
   registerStatsMetric(name, core_id, "hold-decisions", &state.hold_decisions);
   
   // ========================================
   // Controller decision events
   // ========================================
   registerStatsMetric(name, core_id, "data-pain-events", &state.data_pain_events);
   registerStatsMetric(name, core_id, "tr-gain-events", &state.tr_gain_events);
   
   // ========================================
   // Absolute guardrail and net benefit stats
   // ========================================
   registerStatsMetric(name, core_id, "abs-guard-triggers", &state.abs_guard_triggers);
   registerStatsMetric(name, core_id, "net-benefit-backoffs", &state.net_benefit_backoffs);
   registerStatsMetric(name, core_id, "net-benefit-pushes", &state.net_benefit_pushes);
   
   // ========================================
   // Per-level time distribution
   // ========================================
   registerStatsMetric(name, core_id, "epochs-at-level-0", &state.epochs_at_level[0]);
   registerStatsMetric(name, core_id, "epochs-at-level-1", &state.epochs_at_level[1]);
   registerStatsMetric(name, core_id, "epochs-at-level-2", &state.epochs_at_level[2]);
   registerStatsMetric(name, core_id, "epochs-at-level-3", &state.epochs_at_level[3]);
   registerStatsMetric(name, core_id, "epochs-at-level-4", &state.epochs_at_level[4]);
   registerStatsMetric(name, core_id, "epochs-at-level-5", &state.epochs_at_level[5]);
   registerStatsMetric(name, core_id, "epochs-at-level-6", &state.epochs_at_level[6]);
   registerStatsMetric(name, core_id, "epochs-at-level-7", &state.epochs_at_level[7]);
   registerStatsMetric(name, core_id, "epochs-at-level-8", &state.epochs_at_level[8]);
   registerStatsMetric(name, core_id, "epochs-at-level-9", &state.epochs_at_level[9]);
   registerStatsMetric(name, core_id, "epochs-at-level-10", &state.epochs_at_level[10]);
   registerStatsMetric(name, core_id, "epochs-at-level-11", &state.epochs_at_level[11]);
   registerStatsMetric(name, core_id, "epochs-at-level-12", &state.epochs_at_level[12]);
   registerStatsMetric(name, core_id, "epochs-at-level-13", &state.epochs_at_level[13]);
   registerStatsMetric(name, core_id, "epochs-at-level-14", &state.epochs_at_level[14]);
   registerStatsMetric(name, core_id, "epochs-at-level-15", &state.epochs_at_level[15]);
   
   // ========================================
   // Cumulative stall tracking
   // ========================================
   registerStatsMetric(name, core_id, "total-translation-stalls", &state.total_translation_stalls);
   registerStatsMetric(name, core_id, "total-data-stalls", &state.total_data_stalls);
   
   // ========================================
   // Current state snapshots (updated each epoch)
   // ========================================
   registerStatsMetric(name, core_id, "current-meta-level", &state.current_meta_level);
   registerStatsMetric(name, core_id, "current-engaged", &state.current_engaged);
   
   // ========================================
   // Scaled metrics for analysis (x1000 to preserve precision)
   // ========================================
   registerStatsMetric(name, core_id, "tr-share-x1000", &state.tr_share_x1000);
   registerStatsMetric(name, core_id, "rho-x1000", &state.rho_x1000);
   registerStatsMetric(name, core_id, "S-tr-ema", &state.S_tr_ema_scaled);
   registerStatsMetric(name, core_id, "S-data-ema", &state.S_data_ema_scaled);
   registerStatsMetric(name, core_id, "delta-tr-magnitude-x1000", &state.delta_tr_magnitude_x1000);
   registerStatsMetric(name, core_id, "delta-data-magnitude-x1000", &state.delta_data_magnitude_x1000);
   registerStatsMetric(name, core_id, "tr-improving", &state.tr_improving);
   registerStatsMetric(name, core_id, "data-worsening", &state.data_worsening);
   
   if (s_per_core_log[core_id]) {
      s_per_core_log[core_id]->info("Registered ", 34, " stats metrics");
   }
}

// ============================================================================
// Stats Snapshot Update (called at end of each epoch)
// ============================================================================
// Updates the UInt64 stats snapshots that are registered with StatsManager.
// These provide visibility into controller state for analysis tools.
// ============================================================================

static void updateStatsSnapshots(MPLRUController::PerCoreState& state, 
                                  float delta_S_tr, float delta_S_data,
                                  UInt64 tr_stalls_this_epoch, UInt64 data_stalls_this_epoch)
{
   // Increment total epoch counter
   state.total_epochs++;
   
   // Track cooldown epochs
   if (state.cooldown_remaining > 0) {
      state.cooldown_epochs_spent++;
   }
   
   // Track time spent at current level
   int level = std::clamp(state.meta_level, 0, 15);
   state.epochs_at_level[level]++;
   
   // Accumulate total stalls
   state.total_translation_stalls += tr_stalls_this_epoch;
   state.total_data_stalls += data_stalls_this_epoch;
   
   // Current state snapshots
   state.current_meta_level = (UInt64)state.meta_level;
   state.current_engaged = state.engaged ? 1 : 0;
   
   // Scaled float metrics (multiply by 1000 to preserve 3 decimal places)
   state.tr_share_x1000 = (UInt64)(state.last_tr_share * 1000.0f);
   state.rho_x1000 = (UInt64)(state.last_rho * 1000.0f);
   
   // EMA values (store as raw cycles, they're already meaningful)
   state.S_tr_ema_scaled = (UInt64)state.S_tr_ema;
   state.S_data_ema_scaled = (UInt64)state.S_data_ema;
   
   // Delta values (magnitude and direction separate for stats compatibility)
   // Negative delta_S_tr means translation improved (good)
   // Positive delta_S_data means data got worse (bad)
   float eps = 1.0f;
   float norm_delta_tr = delta_S_tr / (state.S_tr_ema + eps);
   float norm_delta_data = delta_S_data / (state.S_data_ema + eps);
   
   state.delta_tr_magnitude_x1000 = (UInt64)(std::abs(norm_delta_tr) * 1000.0f);
   state.delta_data_magnitude_x1000 = (UInt64)(std::abs(norm_delta_data) * 1000.0f);
   state.tr_improving = (norm_delta_tr < 0) ? 1 : 0;  // Negative = improving
   state.data_worsening = (norm_delta_data > 0) ? 1 : 0;  // Positive = worsening
}

// ============================================================================
// Epoch Processing (main Δ-controller logic)
// ============================================================================
//
// This is the heart of the MPLRU controller. Called periodically (every
// epoch_cycles), it:
//   1. Pulls stats from MimicOS (translation/data stalls, total cycles)
//   2. Updates EMAs to smooth noisy per-epoch measurements
//   3. Checks engage condition with hysteresis
//   4. During exploration: pushes aggressively unless severe data pain
//   5. Post-exploration: applies Δ-controller (marginal benefit test)
//
// The key insight is that we optimize DERIVATIVES, not raw metrics.
// We're asking: "Did the last level change help translation without hurting data?"
//
// ============================================================================

void MPLRUController::processEpoch(core_id_t core_id)
{
   if (!s_initialized || core_id >= (core_id_t)s_per_core_state.size()) {
      return;
   }
   
   PerCoreState& state = s_per_core_state[core_id];
   
   if (!state.enabled) {
      return;
   }

   // Get logger for this core (may be null if not initialized)
   SimLog* log = (core_id < (core_id_t)s_per_core_log.size()) ? s_per_core_log[core_id] : nullptr;
   
   if (log) log->trace("processEpoch called");
   // Small epsilon to avoid division by zero in ratio calculations
   const float eps = 1.0f;
   
   // ========================================
   // STEP 1: Pull statistics from MimicOS
   // ========================================
   // MimicOS stores centralized per-core stats that are updated by:
   //   - MMU: translation_latency, l2_tlb_hits/misses
   //   - MemoryManager: data_access_latency, cache_hits/misses
   //   - Core: cycles, instructions
   //
   // This "pull" model is cleaner than having MMUs push to controller.
   // ========================================
   MimicOS* mimicos = Sim()->getMimicOS();
   if (!mimicos || !mimicos->isPerCoreStatsInitialized()) {
      return;
   }
   const PerCoreStats& stats = mimicos->getPerCoreStats(core_id);
   
   // Get current cumulative values
   // Note: translation_latency and data_access_latency are SubsecondTime.
   // We use getNS() as a proxy for cycles (at ~1GHz, 1ns ≈ 1 cycle).
   // This is an approximation but sufficient for relative comparisons.
   UInt64 current_tr_cycles = stats.translation_latency.getNS();
   UInt64 current_data_cycles = stats.data_access_latency.getNS();
   // NOTE: We compute total as sum of translation + data stalls, NOT from stats.cycles.
   // Reason: translation_stalls and data_stalls are accumulated latencies (no parallelism),
   // but stats.cycles reflects actual elapsed cycles with parallelism effects.
   // Using the sum gives us a consistent denominator for tr_share calculation.
   UInt64 current_total_stalls = current_tr_cycles + current_data_cycles;
   
   if (log) log->trace("Stats: TR=", current_tr_cycles, " DATA=", current_data_cycles, " TOTAL_STALLS=", current_total_stalls);
   // Compute per-epoch deltas (this epoch's contribution)
   UInt64 tr_delta = current_tr_cycles - state.last_translation_cycles;
   UInt64 data_delta = current_data_cycles - state.last_data_cycles;
   UInt64 total_stall_delta = tr_delta + data_delta;  // Sum of stalls, not cycle delta
   
   float S_tr_epoch = (float)tr_delta;           // Translation stall cycles this epoch
   float S_data_epoch = (float)data_delta;       // Data stall cycles this epoch
   float C_epoch = (float)total_stall_delta;     // Total stalls this epoch (tr + data)
   
   // Update baselines for next epoch's delta calculation
   // NOTE: These are separate from last_epoch_instruction_marker (which tracks timing)
   state.last_translation_cycles = current_tr_cycles;
   state.last_data_cycles = current_data_cycles;
   state.last_total_cycles = current_total_stalls;  // Track total stalls, not cycles
   
   // Skip if epoch is too short (warmup, context switch, or measurement noise)
   if (C_epoch < 100.0f) {
      if (log) log->trace("Epoch skipped (too short): C_epoch=", C_epoch);
      return;
   }
   
   if (log) {
      log->trace("───────────────────────────────────────────");
      log->trace("EPOCH @instr=", stats.instructions_executed);
      log->trace("  Raw deltas: S_tr=", S_tr_epoch, " S_data=", S_data_epoch, " C=", C_epoch);
   }
   
   // ========================================
   // STEP 2: Update Exponential Moving Averages (EMAs)
   // ========================================
   // EMAs smooth out noisy per-epoch measurements. Without this, a single
   // anomalous epoch could cause rapid oscillation in meta_level.
   //
   // EMA formula: new_ema = α * sample + (1-α) * old_ema
   // With α=0.2, it takes ~5 epochs for a change to fully propagate.
   // ========================================
   state.prev_S_tr_ema = state.S_tr_ema;    // Save for Δ calculation
   state.prev_S_data_ema = state.S_data_ema;
   
   state.S_tr_ema = state.ema_alpha * S_tr_epoch + (1.0f - state.ema_alpha) * state.S_tr_ema;
   state.S_data_ema = state.ema_alpha * S_data_epoch + (1.0f - state.ema_alpha) * state.S_data_ema;
   state.C_ema = state.ema_alpha * C_epoch + (1.0f - state.ema_alpha) * state.C_ema;
   
   if (log) {
      log->trace("  EMA values: S_tr_ema=", state.S_tr_ema, " S_data_ema=", state.S_data_ema, " C_ema=", state.C_ema);
   }
   
   // ========================================
   // STEP 3: Compute engage metrics using EMA (not raw epoch values)
   // ========================================
   // Using EMA values prevents thrash at the engage boundary. Without this,
   // one noisy epoch could toggle engaged on/off even if the trend is stable.
   //
   // tr_share: What fraction of total stalls (tr + data) are translation stalls?
   //           tr_share >= 1% suggests translation is worth optimizing.
   //           Note: C_ema = S_tr_ema + S_data_ema (sum of stalls, not core cycles)
   //
   // rho: Ratio of translation stalls to data stalls.
   //      rho >= 2% means translation isn't negligible vs data.
   // ========================================
   float tr_share_ema = state.S_tr_ema / (state.C_ema + eps);
   float rho_ema = state.S_tr_ema / (state.S_data_ema + eps);
   
   state.last_tr_share = tr_share_ema;  // For stats/debugging
   state.last_rho = rho_ema;
   
   if (log) {
      log->debug("  Engage metrics: tr_share=", tr_share_ema, " (min=", state.tr_share_min, 
                 ") rho=", rho_ema, " (min=", state.rho_min, ")");
   }
   
   // ========================================
   // STEP 4: Hysteresis-based engage/disengage
   // ========================================
   // Problem: Without hysteresis, controller may rapidly toggle engaged state
   //          when metrics hover near the threshold.
   //
   // Solution: Require multiple consecutive epochs above/below threshold.
   //           - Engage after 2 epochs above (quick to react)
   //           - Disengage after 4 epochs below (conservative, avoid false negatives)
   // ========================================
   bool condition_met = (tr_share_ema >= state.tr_share_min) || (rho_ema >= state.rho_min);
   
   if (condition_met) {
      state.engage_above_count++;
      state.engage_below_count = 0;  // Reset below counter
   } else {
      state.engage_below_count++;
      state.engage_above_count = 0;  // Reset above counter
   }
   
   bool was_engaged = state.engaged;
   
   // Transition to engaged state
   if (!state.engaged && state.engage_above_count >= state.engage_threshold_epochs) {
      state.engaged = true;
      if (log) log->info(">>> ENGAGED: tr_share=", tr_share_ema, " rho=", rho_ema);
   }
   // Transition to disengaged state
   else if (state.engaged && state.engage_below_count >= state.disengage_threshold_epochs) {
      state.engaged = false;
      if (log) log->info("<<< DISENGAGED: tr_share=", tr_share_ema, " rho=", rho_ema);
   }
   
   // Update epoch counters for stats
   if (state.engaged) {
      state.engaged_epochs++;
   } else {
      state.disengaged_epochs++;
   }
   
   // ========================================
   // STEP 5: Handle engage transition → start exploration phase
   // ========================================
   // When we first engage, we want to:
   //   1. Fast-start to a reasonable level (don't start at 0)
   //   2. Begin exploration phase to push harder
   //
   // This ensures we actually TRY prioritizing translation before we
   // start evaluating whether it's working.
   // ========================================
   if (state.engaged && !was_engaged) {
      // Just became engaged: jump to starting level
      state.meta_level = std::max(state.meta_level, state.meta_level_start);
      // Begin exploration: will push harder for N epochs
      state.exploration_remaining = state.explore_epochs;
      state.cooldown_remaining = 0;  // Allow immediate action in exploration
      if (log) log->info("FAST START: level=", state.meta_level, " exploring for ", state.explore_epochs, " epochs");
   }
   
   // ========================================
   // STEP 6: If not engaged, force low level and exit
   // ========================================
   // When translation isn't significant, we shouldn't be protecting metadata.
   // Force level to 0 or 1 (mild protection at most).
   // ========================================
   if (!state.engaged) {
      int old_level = state.meta_level;
      state.meta_level = std::min(state.meta_level, 1);
      state.cooldown_remaining = 0;
      state.exploration_remaining = 0;
      if (log && old_level != state.meta_level) {
         log->trace("  Not engaged, clamping level: ", old_level, " -> ", state.meta_level);
      }
      // Update stats before returning
      updateStatsSnapshots(state, 0.0f, 0.0f, tr_delta, data_delta);
      return;  // Nothing more to do
   }
   
   // ========================================
   // STEP 7: Exploration phase
   // ========================================
   // For the first N epochs after engage, we push aggressively UNLESS
   // we see SEVERE data pain (2× normal threshold).
   //
   // Why? Because we want to "try prioritizing translation" before we
   // start judging whether it's working. If we're too conservative at
   // the start, we might never discover that higher levels help.
   //
   // If severe data pain occurs, we abort exploration early and back off.
   // ========================================
   if (state.exploration_remaining > 0) {
      state.exploration_remaining--;
      state.exploration_epochs++;
      
      // Compute current data pain (how much worse is data getting?)
      float delta_S_data = state.S_data_ema - state.prev_S_data_ema;
      float d_data = delta_S_data / (state.S_data_ema + eps);
      
      // During exploration, only back off if data pain is SEVERE
      float severe_threshold = state.data_pain_frac * state.severe_data_pain_mult;
      
      if (d_data > severe_threshold) {
         // Severe data pain detected: abort exploration, back off
         state.data_pain_events++;
         int old_level = state.meta_level;
         state.meta_level = std::clamp(state.meta_level - state.max_step_per_epoch, 0, state.meta_level_max);
         state.level_downs++;
         state.exploration_remaining = 0;  // End exploration early
         state.cooldown_remaining = state.cooldown_epochs;
         if (log) log->info("EXPLORATION ABORT: severe data pain d_data=", d_data, 
                            " > ", severe_threshold, " level: ", old_level, " -> ", state.meta_level);
      } else if (state.meta_level < state.meta_level_max) {
         // No severe pain: keep pushing higher
         int old_level = state.meta_level;
         state.meta_level++;
         state.level_ups++;
         state.cooldown_remaining = state.cooldown_epochs;
         if (log) log->debug("EXPLORATION PUSH: d_data=", d_data, " < ", severe_threshold, 
                             " level: ", old_level, " -> ", state.meta_level);
      }
      
      // Update stats before returning
      updateStatsSnapshots(state, 0.0f, delta_S_data, tr_delta, data_delta);
      return;  // Skip normal Δ-controller during exploration
   }
   
   // ========================================
   // STEP 8: Cooldown check
   // ========================================
   // After each level change, wait cooldown_epochs before adjusting again.
   // This prevents rapid oscillation and gives the system time to stabilize
   // after a level change.
   // ========================================
   if (state.cooldown_remaining > 0) {
      state.cooldown_remaining--;
      // Update stats before returning
      updateStatsSnapshots(state, 0.0f, 0.0f, tr_delta, data_delta);
      return;  // In cooldown, don't adjust
   }
   
   // ========================================
   // STEP 8.5: Absolute Guardrail (data share ceiling)
   // ========================================
   // The original Δ-controller uses epoch-to-epoch derivatives.
   // Problem: If data stalls are persistently high (not increasing),
   // d_data ≈ 0 and no pain is detected even though we're hurting the cache.
   //
   // Solution: Track absolute data share (S_data_ema / C_ema).
   // If data share exceeds a ceiling for N consecutive epochs, back off.
   //
   // This catches the "frog in boiling water" scenario where gradual
   // degradation goes unnoticed by the derivative-based controller.
   // ========================================
   if (state.abs_guard_enabled && state.meta_level > 0) {
      // Compute current data share (fraction of stalls that are data)
      state.data_share_ema = state.S_data_ema / (state.C_ema + eps);
      
      // Apply level-aware thresholds: stricter at high levels
      float effective_max = state.data_share_max;
      if (state.level_aware_guard && state.meta_level >= state.high_level_threshold) {
         effective_max = state.data_share_max * state.high_level_ratio_mult;
      }
      
      if (state.data_share_ema > effective_max) {
         state.abs_guard_violation_count++;
         if (log) log->trace("  ABS-GUARD: violation #", state.abs_guard_violation_count,
                             " data_share=", state.data_share_ema, " > ", effective_max);
         
         // Trigger backoff after N consecutive violations
         if (state.abs_guard_violation_count >= state.abs_guard_epochs) {
            state.abs_guard_triggers++;
            int old_level = state.meta_level;
            int step = state.abs_guard_backoff_step;
            state.meta_level = std::clamp(state.meta_level - step, 0, state.meta_level_max);
            state.level_downs++;
            state.cooldown_remaining = state.cooldown_epochs;
            state.abs_guard_violation_count = 0;  // Reset after backoff
            if (log) log->info("ABS-GUARD BACKOFF: data_share=", state.data_share_ema,
                               " level: ", old_level, " -> ", state.meta_level);
            
            // Update stats and skip Δ-controller this epoch
            updateStatsSnapshots(state, 0.0f, 0.0f, tr_delta, data_delta);
            return;
         }
      } else {
         // Below threshold: reset violation counter
         state.abs_guard_violation_count = 0;
      }
   }
   
   // ========================================
   // STEP 9: Δ-controller (marginal benefit test)
   // ========================================
   // Post-exploration, we use derivative-based control:
   //
   // Δ_tr = S_tr_ema - prev_S_tr_ema   (change in translation stalls)
   // Δ_data = S_data_ema - prev_S_data_ema  (change in data stalls)
   //
   // g_tr = -Δ_tr / S_tr_ema   (positive = translation IMPROVING)
   // d_data = Δ_data / S_data_ema  (positive = data pain INCREASING)
   //
   // Decision matrix:
   //   | tr improving | tr not improving |
   //   +-------------+------------------+
   //   | data ok     | PUSH harder      | HOLD        |
   //   | data pain   | HOLD             | BACK OFF    |
   //
   // The key insight: we only back off when data hurts AND translation
   // doesn't benefit. If translation is still improving, the data cost
   // might be worth it.
   // ========================================
   
   // Compute deltas (using EMA values for stability)
   float delta_S_tr = state.S_tr_ema - state.prev_S_tr_ema;
   float delta_S_data = state.S_data_ema - state.prev_S_data_ema;
   
   // Normalize to fractional changes
   float d_data = delta_S_data / (state.S_data_ema + eps);  // Positive = data getting worse
   float g_tr = -delta_S_tr / (state.S_tr_ema + eps);       // Positive = translation improving
   
   // Threshold checks (original derivative-based signals)
   bool data_pain = d_data > state.data_pain_frac;  // Data stalls increased by >5%
   bool tr_gain = g_tr > state.tr_gain_frac;        // Translation stalls decreased by >3%
   
   if (log) {
      log->trace("  Δ-controller: d_data=", d_data, " (thresh=", state.data_pain_frac, 
                 ") g_tr=", g_tr, " (thresh=", state.tr_gain_frac, ")");
      log->trace("  Signals: data_pain=", data_pain ? "YES" : "no", 
                 " tr_gain=", tr_gain ? "YES" : "no", " level=", state.meta_level);
   }
   
   // Update event counters for debugging
   if (data_pain) state.data_pain_events++;
   if (tr_gain) state.tr_gain_events++;
   
   // ========================================
   // Net Benefit Test (magnitude-aware decision)
   // ========================================
   // The original decision used binary signals (pain yes/no, gain yes/no).
   // Problem: A large harm with a tiny benefit still passes if both thresholds are met.
   //
   // Solution: Compare absolute magnitudes of harm vs benefit.
   // benefit = max(0, -delta_S_tr) (cycles saved in translation)
   // harm = max(0, delta_S_data)   (cycles added in data stalls)
   //
   // If harm > benefit * ratio → net negative, should back off
   // If benefit > min_cycles AND harm < benefit * ratio → net positive, can push
   //
   // Level-aware: Apply stricter ratio at high meta-levels
   // ========================================
   bool net_benefit_push = false;
   bool net_benefit_backoff = false;
   
   if (state.net_benefit_enabled) {
      float benefit = std::max(0.0f, -delta_S_tr);  // Cycles saved (tr improving)
      float harm = std::max(0.0f, delta_S_data);    // Cycles added (data worsening)
      
      // Apply level-aware harm tolerance
      float effective_ratio = state.harm_to_benefit_ratio;
      if (state.level_aware_guard && state.meta_level >= state.high_level_threshold) {
         effective_ratio = state.harm_to_benefit_ratio * state.high_level_ratio_mult;
      }
      
      if (log) {
         log->trace("  Net-benefit: benefit=", benefit, " harm=", harm, 
                    " ratio_thresh=", effective_ratio, " min_benefit=", state.min_benefit_cycles);
      }
      
      // Net negative check: harm exceeds tolerable ratio of benefit
      if (harm > benefit * effective_ratio && harm > state.min_benefit_cycles) {
         net_benefit_backoff = true;
         state.net_benefit_backoffs++;
         if (log) log->debug("  Net-benefit: BACKOFF (harm=", harm, " > benefit*ratio=", 
                             benefit * effective_ratio, ")");
      }
      // Net positive check: meaningful benefit with acceptable harm
      else if (benefit > state.min_benefit_cycles && harm <= benefit * effective_ratio) {
         net_benefit_push = true;
         state.net_benefit_pushes++;
         if (log) log->debug("  Net-benefit: PUSH (benefit=", benefit, " > min=", 
                             state.min_benefit_cycles, " harm acceptable)");
      }
   }
   
   int level_change = 0;
   int old_level = state.meta_level;
   
   // Decision rule (enhanced with net benefit test):
   // Priority 1: Net benefit backoff overrides if harm >> benefit
   // Priority 2: Original derivative signals with net benefit modulation
   //
   // Back off if:
   //   - (data_pain AND !tr_gain) OR
   //   - net_benefit_backoff (harm exceeds tolerable ratio)
   //
   // Push if:
   //   - (tr_gain AND !data_pain) OR
   //   - net_benefit_push (meaningful benefit with acceptable harm)
   //
   // Hold otherwise
   if (net_benefit_backoff) {
      // Net benefit test says harm > benefit: back off regardless of signals
      level_change = -state.max_step_per_epoch;
      state.level_downs++;
   }
   else if (data_pain && !tr_gain) {
      // Original: data hurts AND translation doesn't benefit
      level_change = -state.max_step_per_epoch;
      state.level_downs++;
   }
   else if (net_benefit_push && !data_pain) {
      // Net positive AND no data pain signal: push
      level_change = state.max_step_per_epoch;
      state.level_ups++;
   }
   else if (tr_gain && !data_pain) {
      // Original: translation benefits AND data doesn't hurt
      level_change = state.max_step_per_epoch;
      state.level_ups++;
   }
   else {
      // Hold decision (mixed signals, both improving, or insufficient magnitude)
      state.hold_decisions++;
   }
   
   // Apply level change with bounds checking
   if (level_change != 0) {
      state.meta_level = std::clamp(state.meta_level + level_change, 0, state.meta_level_max);
      state.cooldown_remaining = state.cooldown_epochs;  // Enter cooldown
      if (log) {
         const char* action = (level_change > 0) ? "PUSH" : "BACK OFF";
         log->info("Δ-CONTROLLER ", action, ": level ", old_level, " -> ", state.meta_level,
                   " (d_data=", d_data, " g_tr=", g_tr, ")");
      }
   }
   else if (log) {
      log->trace("  Δ-controller: HOLD at level=", state.meta_level);
   }
   
   // ========================================
   // STEP 10: Update stats snapshots for external monitoring
   // ========================================
   updateStatsSnapshots(state, delta_S_tr, delta_S_data, tr_delta, data_delta);
}

// ============================================================================
// Try Epoch Processing
// ============================================================================
//
// Called by the replacement policy (CacheSetMPLRU) on each cache access.
// This is the entry point that decides when to run processEpoch().
//
// WHY CALLED FROM REPLACEMENT POLICY (not MMU):
// - The replacement policy is the consumer of meta_level
// - MMUs/MemoryManager only update stats; they don't call replacement policies
// - This separation of concerns keeps the architecture clean
//
// The function checks if enough cycles have elapsed since the last epoch
// and triggers processEpoch() if needed. This self-timing approach means
// callers don't need to track when to trigger epochs.
//
// ============================================================================

void MPLRUController::tryProcessEpoch(core_id_t core_id)
{
   if (!s_initialized || core_id >= (core_id_t)s_per_core_state.size()) {
      return;
   }
   
   PerCoreState& state = s_per_core_state[core_id];
   
   if (!state.enabled) {
      return;
   }
   
   // Get current instruction count from MimicOS (the authoritative source)
   MimicOS* mimicos = Sim()->getMimicOS();
   if (!mimicos || !mimicos->isPerCoreStatsInitialized()) {
      return;
   }
   
   const PerCoreStats& stats = mimicos->getPerCoreStats(core_id);
   UInt64 current_instructions = stats.instructions_executed;
   
   // Check if enough instructions have passed since last epoch
   // NOTE: last_epoch_instruction_marker is separate from last_total_cycles
   //       (which is used for delta calculation in processEpoch)
   if (current_instructions - state.last_epoch_instruction_marker >= state.epoch_instructions) {
      SimLog* log = (core_id < (core_id_t)s_per_core_log.size()) ? s_per_core_log[core_id] : nullptr;
      if (log) log->trace("Epoch triggered: instructions=", current_instructions, 
                          " last_marker=", state.last_epoch_instruction_marker);
      state.last_epoch_instruction_marker = current_instructions;
      processEpoch(core_id);
   }
}

// ============================================================================
// Getters
// ============================================================================
//
// These provide read-only access to controller state.
// getMetaLevel() is the main output used by the replacement policy.
//
// ============================================================================

int MPLRUController::getMetaLevel(core_id_t core_id)
{
   if (!s_initialized || core_id >= (core_id_t)s_per_core_state.size()) {
      return 0;  // Default: vanilla LRU if controller not ready
   }
   return s_per_core_state[core_id].meta_level;
}

bool MPLRUController::isEngaged(core_id_t core_id)
{
   if (!s_initialized || core_id >= (core_id_t)s_per_core_state.size()) {
      return false;
   }
   return s_per_core_state[core_id].engaged;
}

const MPLRUController::PerCoreState* MPLRUController::getState(core_id_t core_id)
{
   if (!s_initialized || core_id >= (core_id_t)s_per_core_state.size()) {
      return nullptr;
   }
   return &s_per_core_state[core_id];
}
