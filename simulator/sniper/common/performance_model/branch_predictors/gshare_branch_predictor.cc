/**
 * Gshare Branch Predictor for Sniper
 * 
 * Ported from ChampSim's gshare implementation.
 * 
 * Gshare XORs the PC with global branch history to index into
 * a table of 2-bit saturating counters.
 */

#include "gshare_branch_predictor.h"
#include "simulator.h"
#include "config.hpp"
#include "stats.h"
#include <cstring>

// =============================================================================
// Constructor / Destructor
// =============================================================================

GshareBranchPredictor::GshareBranchPredictor(String name, core_id_t core_id,
                                               UInt32 history_length,
                                               UInt32 table_size)
    : BranchPredictor(name, core_id)
    , m_history_length(history_length)
    , m_table_size(table_size)
    , m_table_mask(table_size - 1)
    , m_branch_history(0)
{
    // Create history mask based on history length
    m_history_mask = (1ULL << m_history_length) - 1;
    
    // Initialize pattern table with weakly not-taken (1)
    m_pattern_table.resize(m_table_size, 1);
}

GshareBranchPredictor::~GshareBranchPredictor() {}

// =============================================================================
// Prediction
// =============================================================================

bool GshareBranchPredictor::predict(bool indirect, IntPtr ip, IntPtr target)
{
    UInt32 index = getIndex(ip);
    
    // Predict taken if counter >= 2 (weakly taken or strongly taken)
    return m_pattern_table[index] >= COUNTER_WEAKLY_TAKEN;
}

// =============================================================================
// Update
// =============================================================================

void GshareBranchPredictor::update(bool predicted, bool actual, bool indirect,
                                    IntPtr ip, IntPtr target)
{
    updateCounters(predicted, actual);
    
    UInt32 index = getIndex(ip);
    
    // Update 2-bit saturating counter
    if (actual) {
        // Branch was taken - increment counter
        if (m_pattern_table[index] < COUNTER_MAX) {
            m_pattern_table[index]++;
        }
    } else {
        // Branch was not taken - decrement counter
        if (m_pattern_table[index] > 0) {
            m_pattern_table[index]--;
        }
    }
    
    // Update global branch history register
    m_branch_history = ((m_branch_history << 1) | (actual ? 1 : 0)) & m_history_mask;
}

// =============================================================================
// Helper Functions
// =============================================================================

UInt32 GshareBranchPredictor::getIndex(IntPtr ip) const
{
    // XOR PC bits with global history to get table index
    // Use multiple slices of the PC for better distribution
    uint64_t hash = m_branch_history;
    
    // XOR with different slices of the PC (like ChampSim does)
    hash ^= (ip & m_table_mask);
    hash ^= ((ip >> m_history_length) & m_history_mask);
    hash ^= ((ip >> (2 * m_history_length)) & m_history_mask);
    
    return hash & m_table_mask;
}
