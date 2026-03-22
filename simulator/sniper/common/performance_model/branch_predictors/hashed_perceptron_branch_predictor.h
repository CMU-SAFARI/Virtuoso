/**
 * Hashed Perceptron Branch Predictor for Sniper
 * 
 * Ported from ChampSim's Hashed Perceptron implementation.
 * 
 * Combines ideas from multiple papers:
 * - "Fast Path-Based Neural Branch Prediction" Jimenez, MICRO 2003
 * - "Piecewise Linear Branch Prediction" Jimenez, ISCA 2005
 * - "The O-GEHL Branch Predictor" Seznec, CBP 2004
 * 
 * Uses geometric history lengths and hashed indexing for improved accuracy.
 */

#ifndef HASHED_PERCEPTRON_BRANCH_PREDICTOR_H
#define HASHED_PERCEPTRON_BRANCH_PREDICTOR_H

#include "branch_predictor.h"
#include "fixed_types.h"
#include <vector>
#include <array>
#include <cstdint>

class HashedPerceptronBranchPredictor : public BranchPredictor
{
public:
    HashedPerceptronBranchPredictor(String name, core_id_t core_id);
    ~HashedPerceptronBranchPredictor();

    bool predict(bool indirect, IntPtr ip, IntPtr target) override;
    void update(bool predicted, bool actual, bool indirect, IntPtr ip, IntPtr target) override;

private:
    // Configuration constants
    static constexpr size_t NTABLES = 16;           // Number of weight tables
    static constexpr size_t TABLE_SIZE = 4096;      // 12-bit indices (1 << 12)
    static constexpr size_t TABLE_INDEX_BITS = 12;
    static constexpr size_t MAX_HIST = 232;         // Maximum history length
    static constexpr int THRESHOLD = 1;             // Prediction threshold

    // Geometric history lengths for each table
    static constexpr std::array<size_t, NTABLES> HISTORY_LENGTHS = {
        0, 3, 4, 6, 8, 10, 14, 19, 26, 36, 49, 67, 91, 125, 170, 232
    };

    // 8-bit signed weight type
    static constexpr int MAX_WEIGHT = 127;
    static constexpr int MIN_WEIGHT = -128;

    // Folded history register - maintains XOR-folded history
    class FoldedHistory {
    public:
        FoldedHistory(size_t history_length, size_t fold_width);
        void push_back(bool taken);
        uint64_t value() const { return m_folded; }
        void reset();

    private:
        size_t m_history_length;
        size_t m_fold_width;
        uint64_t m_folded;
        std::vector<bool> m_history;
        size_t m_head;
    };

    // Weight tables (NTABLES tables, each with TABLE_SIZE 8-bit weights)
    std::array<std::array<int8_t, TABLE_SIZE>, NTABLES> m_tables;

    // Folded history registers for each table
    std::vector<FoldedHistory> m_ghist_words;

    // Dynamic threshold setting
    int m_theta;
    int m_tc;  // Threshold counter

    // Last prediction state for update
    struct PredictionResult {
        std::array<uint64_t, NTABLES> indices;
        int yout;
    };
    PredictionResult m_last_result;

    // Helper functions
    uint64_t getIndex(size_t table, IntPtr ip) const;
    int8_t saturateWeight(int value) const;
    void adjustThreshold(bool correct);
};

#endif /* HASHED_PERCEPTRON_BRANCH_PREDICTOR_H */
