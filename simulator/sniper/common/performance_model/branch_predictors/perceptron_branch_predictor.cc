/**
 * Perceptron Branch Predictor for Sniper
 * 
 * Ported from ChampSim's Perceptron implementation.
 * 
 * Original paper: "Dynamic Branch Prediction with Perceptrons"
 * Jimenez & Lin, HPCA 2001
 */

#include "perceptron_branch_predictor.h"
#include "simulator.h"
#include "config.hpp"
#include "stats.h"
#include <cmath>
#include <algorithm>

// =============================================================================
// Perceptron Methods
// =============================================================================

int PerceptronBranchPredictor::Perceptron::predict(const std::vector<bool>& history) const
{
    int output = bias;
    
    for (size_t i = 0; i < history.size() && i < weights.size(); i++) {
        if (history[i]) {
            output += weights[i];
        } else {
            output -= weights[i];
        }
    }
    
    return output;
}

void PerceptronBranchPredictor::Perceptron::update(bool taken, const std::vector<bool>& history)
{
    // Update bias
    if (taken) {
        if (bias < 127) bias++;
    } else {
        if (bias > -128) bias--;
    }
    
    // Update weights
    for (size_t i = 0; i < history.size() && i < weights.size(); i++) {
        bool correlates = (history[i] == taken);
        if (correlates) {
            if (weights[i] < 127) weights[i]++;
        } else {
            if (weights[i] > -128) weights[i]--;
        }
    }
}

// =============================================================================
// Constructor / Destructor
// =============================================================================

PerceptronBranchPredictor::PerceptronBranchPredictor(String name, core_id_t core_id,
                                                       UInt32 history_length,
                                                       UInt32 num_perceptrons)
    : BranchPredictor(name, core_id)
    , m_history_length(history_length)
    , m_num_perceptrons(num_perceptrons)
    , m_theta(static_cast<int>(std::lround(1.93 * history_length + 14)))
{
    // Initialize perceptrons
    m_perceptrons.reserve(m_num_perceptrons);
    for (UInt32 i = 0; i < m_num_perceptrons; i++) {
        m_perceptrons.emplace_back(m_history_length);
    }
    
    // Initialize history registers
    m_spec_global_history.resize(m_history_length, false);
    m_global_history.resize(m_history_length, false);
}

PerceptronBranchPredictor::~PerceptronBranchPredictor() {}

// =============================================================================
// Prediction
// =============================================================================

bool PerceptronBranchPredictor::predict(bool indirect, IntPtr ip, IntPtr target)
{
    UInt32 index = getIndex(ip);
    int output = m_perceptrons[index].predict(m_spec_global_history);
    
    bool prediction = (output >= 0);
    
    // Save state for update
    PerceptronState state;
    state.ip = ip;
    state.prediction = prediction;
    state.output = output;
    state.history = m_spec_global_history;
    
    m_state_buffer.push_back(state);
    if (m_state_buffer.size() > MAX_STATE_ENTRIES) {
        m_state_buffer.pop_front();
    }
    
    // Update speculative global history
    for (size_t i = m_spec_global_history.size() - 1; i > 0; i--) {
        m_spec_global_history[i] = m_spec_global_history[i - 1];
    }
    m_spec_global_history[0] = prediction;
    
    return prediction;
}

// =============================================================================
// Update
// =============================================================================

void PerceptronBranchPredictor::update(bool predicted, bool actual, bool indirect, 
                                        IntPtr ip, IntPtr target)
{
    updateCounters(predicted, actual);
    
    // Find the state for this branch
    auto state_it = std::find_if(m_state_buffer.begin(), m_state_buffer.end(),
                                  [ip](const PerceptronState& s) { return s.ip == ip; });
    
    if (state_it == m_state_buffer.end()) {
        // State was lost, use current history
        UInt32 index = getIndex(ip);
        m_perceptrons[index].update(actual, m_global_history);
    } else {
        PerceptronState state = *state_it;
        m_state_buffer.erase(state_it);
        
        // Update real global history
        for (size_t i = m_global_history.size() - 1; i > 0; i--) {
            m_global_history[i] = m_global_history[i - 1];
        }
        m_global_history[0] = actual;
        
        // Restore speculative history on misprediction
        if (state.prediction != actual) {
            m_spec_global_history = m_global_history;
        }
        
        // Train if misprediction or output within threshold
        if ((state.output <= m_theta && state.output >= -m_theta) || 
            (state.prediction != actual)) {
            UInt32 index = getIndex(ip);
            m_perceptrons[index].update(actual, state.history);
        }
    }
}

int8_t PerceptronBranchPredictor::saturate(int value)
{
    if (value > MAX_WEIGHT) return MAX_WEIGHT;
    if (value < MIN_WEIGHT) return MIN_WEIGHT;
    return static_cast<int8_t>(value);
}
