/**
 * TAGE (TAgged GEometric history length) Branch Predictor for Sniper
 * 
 * Based on: "A case for (partially) TAgged GEometric history length branch prediction"
 * André Seznec, JILP 2006
 * 
 * TAGE uses multiple tagged predictor tables with geometrically increasing
 * history lengths. It provides high accuracy by:
 * 1. Using a base bimodal predictor for branches with no history correlation
 * 2. Multiple tagged tables indexed by different history lengths
 * 3. Useful bits to handle aliasing and improve replacement
 * 4. Alternate prediction for newly allocated entries
 */

#ifndef TAGE_BRANCH_PREDICTOR_H
#define TAGE_BRANCH_PREDICTOR_H

#include "branch_predictor.h"
#include "fixed_types.h"
#include <vector>
#include <cstdint>
#include <array>

class TAGEBranchPredictor : public BranchPredictor
{
public:
    TAGEBranchPredictor(String name, core_id_t core_id);
    ~TAGEBranchPredictor();

    bool predict(bool indirect, IntPtr ip, IntPtr target) override;
    void update(bool predicted, bool actual, bool indirect, IntPtr ip, IntPtr target) override;

private:
    // Configuration - based on TAGE-SC-L 64KB configuration
    static constexpr int NHIST = 12;           // Number of tagged tables
    static constexpr int LOGB = 13;            // Log2 of bimodal table size (8K entries)
    static constexpr int LOGG = 10;            // Log2 of tagged table size (1K entries each)
    static constexpr int TBITS = 9;            // Tag width
    static constexpr int CBITS = 3;            // Counter bits
    static constexpr int UBITS = 2;            // Useful counter bits
    
    static constexpr int BIMODAL_SIZE = 1 << LOGB;
    static constexpr int TABLE_SIZE = 1 << LOGG;
    
    // Geometric history lengths: 4, 6, 10, 16, 25, 40, 64, 101, 160, 254, 403, 640
    static constexpr std::array<int, NHIST> HIST_LENGTHS = {
        4, 6, 10, 16, 25, 40, 64, 101, 160, 254, 403, 640
    };
    static constexpr int MAX_HIST = 640;

    // Bimodal table entry (2-bit counter)
    std::vector<int8_t> m_bimodal;

    // Tagged table entry
    struct TaggedEntry {
        int8_t ctr;      // Prediction counter (signed)
        uint16_t tag;    // Partial tag
        int8_t useful;   // Useful counter
        
        TaggedEntry() : ctr(0), tag(0), useful(0) {}
    };

    // Tagged tables
    std::array<std::vector<TaggedEntry>, NHIST> m_tagged;

    // Global history (circular buffer)
    std::vector<bool> m_ghist;
    int m_ghist_ptr;

    // Path history (for tag computation)
    uint64_t m_phist;

    // State from last prediction (for update)
    struct PredictionState {
        IntPtr ip;
        int provider;           // Which table provided prediction (-1 = bimodal)
        int altprovider;        // Alternate provider
        bool provider_pred;     // Prediction from provider
        bool altpred;           // Alternate prediction
        bool prediction;        // Final prediction
        std::array<int, NHIST> indices;    // Indices into tagged tables
        std::array<uint16_t, NHIST> tags;  // Tags for each table
        std::array<bool, NHIST> hits;      // Whether each table hit
    };
    PredictionState m_last_state;

    // Alternate prediction confidence
    int m_alt_better_count;
    static constexpr int ALT_BETTER_THRESHOLD = 8;

    // Random for allocation
    uint32_t m_rand_seed;

    // Helper functions
    int getIndex(IntPtr ip, int table) const;
    uint16_t getTag(IntPtr ip, int table) const;
    uint64_t getFoldedHistory(int length, int outwidth) const;
    bool getBimodalPrediction(IntPtr ip) const;
    void updateBimodal(IntPtr ip, bool taken);
    int8_t saturateCounter(int value, int bits) const;
    uint32_t random();
};

#endif /* TAGE_BRANCH_PREDICTOR_H */
