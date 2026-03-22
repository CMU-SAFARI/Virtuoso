/**
 * Perceptron Branch Predictor for Sniper
 * 
 * Ported from ChampSim's Perceptron implementation.
 * 
 * Original paper: "Dynamic Branch Prediction with Perceptrons"
 * Jimenez & Lin, HPCA 2001
 * 
 * Uses a table of perceptrons indexed by IP, each computing a weighted
 * sum of global history bits to make predictions.
 */

#ifndef PERCEPTRON_BRANCH_PREDICTOR_H
#define PERCEPTRON_BRANCH_PREDICTOR_H

#include "branch_predictor.h"
#include "fixed_types.h"
#include <vector>
#include <bitset>
#include <deque>
#include <cstdint>

class PerceptronBranchPredictor : public BranchPredictor
{
public:
    PerceptronBranchPredictor(String name, core_id_t core_id,
                               UInt32 history_length = 24,
                               UInt32 num_perceptrons = 163);
    ~PerceptronBranchPredictor();

    bool predict(bool indirect, IntPtr ip, IntPtr target) override;
    void update(bool predicted, bool actual, bool indirect, IntPtr ip, IntPtr target) override;

private:
    // Configuration
    UInt32 m_history_length;
    UInt32 m_num_perceptrons;
    int m_theta;  // Training threshold
    
    // Weight bits (using 8-bit signed weights)
    static constexpr int WEIGHT_BITS = 8;
    static constexpr int MAX_WEIGHT = (1 << (WEIGHT_BITS - 1)) - 1;   // 127
    static constexpr int MIN_WEIGHT = -(1 << (WEIGHT_BITS - 1));      // -128

    // Single perceptron
    struct Perceptron {
        int8_t bias;
        std::vector<int8_t> weights;

        Perceptron(UInt32 history_len) : bias(0), weights(history_len, 0) {}

        int predict(const std::vector<bool>& history) const;
        void update(bool taken, const std::vector<bool>& history);
    };

    // Perceptron state for delayed updates
    struct PerceptronState {
        IntPtr ip;
        bool prediction;
        int output;
        std::vector<bool> history;
    };

    // Tables
    std::vector<Perceptron> m_perceptrons;
    std::deque<PerceptronState> m_state_buffer;
    static constexpr size_t MAX_STATE_ENTRIES = 100;

    // Global history registers
    std::vector<bool> m_spec_global_history;  // Speculative (updated on predict)
    std::vector<bool> m_global_history;       // Real (updated on update)

    // Helper functions
    UInt32 getIndex(IntPtr ip) const { return ip % m_num_perceptrons; }
    static int8_t saturate(int value);
};

#endif /* PERCEPTRON_BRANCH_PREDICTOR_H */
