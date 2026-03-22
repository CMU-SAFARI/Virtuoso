/**
 * Hashed Perceptron Branch Predictor for Sniper
 * 
 * Ported from ChampSim's Hashed Perceptron implementation.
 * 
 * Uses geometric history lengths and hashed indexing with dynamic
 * threshold adjustment for high-accuracy branch prediction.
 */

#include "hashed_perceptron_branch_predictor.h"
#include "simulator.h"
#include "config.hpp"
#include "stats.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <cstring>

// =============================================================================
// Folded History Register
// =============================================================================

HashedPerceptronBranchPredictor::FoldedHistory::FoldedHistory(size_t history_length, size_t fold_width)
    : m_history_length(history_length)
    , m_fold_width(fold_width)
    , m_folded(0)
    , m_head(0)
{
    if (history_length > 0) {
        m_history.resize(history_length, false);
    }
}

void HashedPerceptronBranchPredictor::FoldedHistory::push_back(bool taken)
{
    if (m_history_length == 0) return;
    
    // Get the bit that's about to be shifted out
    bool old_bit = m_history[m_head];
    
    // Store new bit
    m_history[m_head] = taken;
    m_head = (m_head + 1) % m_history_length;
    
    // Update folded value: shift, XOR in new bit, XOR out old bit at fold position
    m_folded = ((m_folded << 1) | (taken ? 1 : 0)) ^ 
               ((old_bit ? 1ULL : 0ULL) << (m_history_length % m_fold_width));
    m_folded &= ((1ULL << m_fold_width) - 1);
}

void HashedPerceptronBranchPredictor::FoldedHistory::reset()
{
    m_folded = 0;
    m_head = 0;
    std::fill(m_history.begin(), m_history.end(), false);
}

// =============================================================================
// Constructor / Destructor
// =============================================================================

HashedPerceptronBranchPredictor::HashedPerceptronBranchPredictor(String name, core_id_t core_id)
    : BranchPredictor(name, core_id)
    , m_theta(10)
    , m_tc(0)
{
    // Initialize weight tables to zero
    for (auto& table : m_tables) {
        std::fill(table.begin(), table.end(), 0);
    }

    // Initialize folded history registers
    m_ghist_words.reserve(NTABLES);
    for (size_t i = 0; i < NTABLES; i++) {
        m_ghist_words.emplace_back(HISTORY_LENGTHS[i], TABLE_INDEX_BITS);
    }

    // Initialize last result
    std::fill(m_last_result.indices.begin(), m_last_result.indices.end(), 0);
    m_last_result.yout = 0;
}

HashedPerceptronBranchPredictor::~HashedPerceptronBranchPredictor() {}

// =============================================================================
// Prediction
// =============================================================================

bool HashedPerceptronBranchPredictor::predict(bool indirect, IntPtr ip, IntPtr target)
{
    PredictionResult result;
    
    // Compute indices for all tables
    for (size_t i = 0; i < NTABLES; i++) {
        result.indices[i] = getIndex(i, ip);
    }
    
    // Compute perceptron sum
    result.yout = 0;
    for (size_t i = 0; i < NTABLES; i++) {
        result.yout += m_tables[i][result.indices[i]];
    }
    
    m_last_result = result;
    
    return result.yout >= THRESHOLD;
}

// =============================================================================
// Update
// =============================================================================

void HashedPerceptronBranchPredictor::update(bool predicted, bool actual, bool indirect,
                                              IntPtr ip, IntPtr target)
{
    updateCounters(predicted, actual);
    
    // Update all folded history registers
    for (auto& hist : m_ghist_words) {
        hist.push_back(actual);
    }
    
    // Perceptron learning rule: train on misprediction or weak correct prediction
    bool prediction_correct = (actual == (m_last_result.yout >= THRESHOLD));
    bool prediction_weak = (std::abs(m_last_result.yout) < m_theta);
    
    if (!prediction_correct || prediction_weak) {
        // Update weights
        for (size_t i = 0; i < NTABLES; i++) {
            int new_weight = m_tables[i][m_last_result.indices[i]];
            new_weight += actual ? 1 : -1;
            m_tables[i][m_last_result.indices[i]] = saturateWeight(new_weight);
        }
        
        // Adjust threshold dynamically
        adjustThreshold(prediction_correct);
    }
}

// =============================================================================
// Helper Functions
// =============================================================================

uint64_t HashedPerceptronBranchPredictor::getIndex(size_t table, IntPtr ip) const
{
    // XOR PC slice with folded history
    uint64_t pc_slice = ip & ((1ULL << TABLE_INDEX_BITS) - 1);
    return (m_ghist_words[table].value() ^ pc_slice) & (TABLE_SIZE - 1);
}

int8_t HashedPerceptronBranchPredictor::saturateWeight(int value) const
{
    if (value > MAX_WEIGHT) return MAX_WEIGHT;
    if (value < MIN_WEIGHT) return MIN_WEIGHT;
    return static_cast<int8_t>(value);
}

void HashedPerceptronBranchPredictor::adjustThreshold(bool correct)
{
    // Dynamic threshold setting from Seznec's O-GEHL paper
    constexpr int SPEED = 18;  // Speed for dynamic threshold adjustment
    
    if (!correct) {
        // Increase theta after enough mispredictions
        m_tc++;
        if (m_tc >= SPEED) {
            m_theta++;
            m_tc = 0;
        }
    } else {
        // Decrease theta after enough weak but correct predictions
        m_tc--;
        if (m_tc <= -SPEED) {
            m_theta--;
            m_tc = 0;
        }
    }
}
