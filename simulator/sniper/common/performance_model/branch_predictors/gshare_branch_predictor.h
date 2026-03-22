/**
 * Gshare Branch Predictor for Sniper
 * 
 * Ported from ChampSim's gshare implementation.
 * 
 * Gshare XORs the PC with global branch history to index into
 * a table of 2-bit saturating counters. This combines the benefits
 * of global history correlation with PC-based indexing.
 * 
 * Original paper: "Alternative Implementations of Two-Level Adaptive
 * Branch Prediction" McFarling, ISCA 1993
 */

#ifndef GSHARE_BRANCH_PREDICTOR_H
#define GSHARE_BRANCH_PREDICTOR_H

#include "branch_predictor.h"
#include "fixed_types.h"
#include <vector>
#include <cstdint>

class GshareBranchPredictor : public BranchPredictor
{
public:
    GshareBranchPredictor(String name, core_id_t core_id,
                          UInt32 history_length = 14,
                          UInt32 table_size = 16384);
    ~GshareBranchPredictor();

    bool predict(bool indirect, IntPtr ip, IntPtr target) override;
    void update(bool predicted, bool actual, bool indirect, IntPtr ip, IntPtr target) override;

private:
    // Configuration
    UInt32 m_history_length;
    UInt32 m_table_size;
    UInt32 m_table_mask;

    // Global branch history register
    uint64_t m_branch_history;
    uint64_t m_history_mask;

    // Pattern history table with 2-bit saturating counters
    std::vector<uint8_t> m_pattern_table;

    // Counter constants (2-bit saturating counter: 0-3)
    static constexpr uint8_t COUNTER_MAX = 3;
    static constexpr uint8_t COUNTER_WEAKLY_TAKEN = 2;

    // Helper functions
    UInt32 getIndex(IntPtr ip) const;
};

#endif /* GSHARE_BRANCH_PREDICTOR_H */
