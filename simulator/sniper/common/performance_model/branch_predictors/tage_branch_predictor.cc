/**
 * TAGE (TAgged GEometric history length) Branch Predictor for Sniper
 * 
 * Based on: "A case for (partially) TAgged GEometric history length branch prediction"
 * André Seznec, JILP 2006
 */

#include "tage_branch_predictor.h"
#include "simulator.h"
#include "config.hpp"
#include "stats.h"
#include <cstring>
#include <algorithm>

// =============================================================================
// Constructor / Destructor
// =============================================================================

TAGEBranchPredictor::TAGEBranchPredictor(String name, core_id_t core_id)
    : BranchPredictor(name, core_id)
    , m_ghist_ptr(0)
    , m_phist(0)
    , m_alt_better_count(ALT_BETTER_THRESHOLD)
    , m_rand_seed(0xDEADBEEF)
{
    // Initialize bimodal table (2-bit counters, initialized to weakly taken = 2)
    m_bimodal.resize(BIMODAL_SIZE, 2);

    // Initialize tagged tables
    for (int i = 0; i < NHIST; i++) {
        m_tagged[i].resize(TABLE_SIZE);
    }

    // Initialize global history
    m_ghist.resize(MAX_HIST, false);

    // Initialize last state
    m_last_state = {};
}

TAGEBranchPredictor::~TAGEBranchPredictor() {}

// =============================================================================
// Prediction
// =============================================================================

bool TAGEBranchPredictor::predict(bool indirect, IntPtr ip, IntPtr target)
{
    PredictionState state;
    state.ip = ip;
    state.provider = -1;          // Default to bimodal
    state.altprovider = -1;
    state.provider_pred = false;
    state.altpred = getBimodalPrediction(ip);
    
    // Compute indices and tags for all tables
    for (int i = 0; i < NHIST; i++) {
        state.indices[i] = getIndex(ip, i);
        state.tags[i] = getTag(ip, i);
        state.hits[i] = (m_tagged[i][state.indices[i]].tag == state.tags[i]);
    }

    // Find provider (longest matching history) and alternate provider
    for (int i = NHIST - 1; i >= 0; i--) {
        if (state.hits[i]) {
            if (state.provider < 0) {
                state.provider = i;
                state.provider_pred = (m_tagged[i][state.indices[i]].ctr >= 0);
            } else if (state.altprovider < 0) {
                state.altprovider = i;
                state.altpred = (m_tagged[i][state.indices[i]].ctr >= 0);
                break;
            }
        }
    }

    // If no tagged table hit, use bimodal
    if (state.provider < 0) {
        state.prediction = state.altpred;
    } else {
        // Check if we should use alternate prediction
        // Use alternate if provider entry is newly allocated (weak counter) and alt seems better
        TaggedEntry& entry = m_tagged[state.provider][state.indices[state.provider]];
        bool use_alt = (entry.ctr == 0 || entry.ctr == -1) && 
                       (entry.useful == 0) && 
                       (m_alt_better_count >= ALT_BETTER_THRESHOLD);
        
        if (use_alt) {
            state.prediction = state.altpred;
        } else {
            state.prediction = state.provider_pred;
        }
    }

    m_last_state = state;
    return state.prediction;
}

// =============================================================================
// Update
// =============================================================================

void TAGEBranchPredictor::update(bool predicted, bool actual, bool indirect,
                                  IntPtr ip, IntPtr target)
{
    updateCounters(predicted, actual);

    PredictionState& state = m_last_state;
    
    // Verify this update matches the last prediction
    if (state.ip != ip) {
        // State mismatch, just update history and return
        // Shift global history
        m_ghist_ptr = (m_ghist_ptr - 1 + MAX_HIST) % MAX_HIST;
        m_ghist[m_ghist_ptr] = actual;
        m_phist = (m_phist << 1) | (ip & 1);
        return;
    }

    // Update useful counter for alternate prediction tracking
    if (state.provider >= 0) {
        TaggedEntry& entry = m_tagged[state.provider][state.indices[state.provider]];
        if ((entry.ctr == 0 || entry.ctr == -1) && entry.useful == 0) {
            // Newly allocated entry - track if alt would have been better
            if (state.altpred != state.provider_pred) {
                if (state.altpred == actual) {
                    if (m_alt_better_count < 15) m_alt_better_count++;
                } else {
                    if (m_alt_better_count > 0) m_alt_better_count--;
                }
            }
        }
    }

    // Update provider entry
    if (state.provider >= 0) {
        TaggedEntry& entry = m_tagged[state.provider][state.indices[state.provider]];
        
        // Update prediction counter
        if (actual) {
            entry.ctr = saturateCounter(entry.ctr + 1, CBITS);
        } else {
            entry.ctr = saturateCounter(entry.ctr - 1, CBITS);
        }

        // Update useful counter
        if (state.provider_pred != state.altpred) {
            if (state.provider_pred == actual) {
                // Provider was correct, alt was wrong - increase useful
                entry.useful = saturateCounter(entry.useful + 1, UBITS);
            } else {
                // Provider was wrong, alt was correct - decrease useful
                entry.useful = saturateCounter(entry.useful - 1, UBITS);
            }
        }
    } else {
        // Update bimodal
        updateBimodal(ip, actual);
    }

    // Allocate new entry on misprediction
    if (predicted != actual) {
        // Try to allocate in a table with longer history than provider
        int start = (state.provider < 0) ? 0 : state.provider + 1;
        
        // Count tables where we can allocate (useful == 0)
        int alloc_count = 0;
        for (int i = start; i < NHIST; i++) {
            if (!state.hits[i] && m_tagged[i][state.indices[i]].useful == 0) {
                alloc_count++;
            }
        }

        if (alloc_count > 0) {
            // Randomly choose which entry to allocate
            int alloc_idx = random() % alloc_count;
            int count = 0;
            
            for (int i = start; i < NHIST; i++) {
                if (!state.hits[i] && m_tagged[i][state.indices[i]].useful == 0) {
                    if (count == alloc_idx) {
                        // Allocate this entry
                        TaggedEntry& entry = m_tagged[i][state.indices[i]];
                        entry.tag = state.tags[i];
                        entry.ctr = actual ? 0 : -1;  // Weak prediction
                        entry.useful = 0;
                        break;
                    }
                    count++;
                }
            }
        } else {
            // No room to allocate - decay useful counters
            for (int i = start; i < NHIST; i++) {
                if (!state.hits[i]) {
                    TaggedEntry& entry = m_tagged[i][state.indices[i]];
                    if (entry.useful > 0) entry.useful--;
                }
            }
        }
    }

    // Update global history
    m_ghist_ptr = (m_ghist_ptr - 1 + MAX_HIST) % MAX_HIST;
    m_ghist[m_ghist_ptr] = actual;

    // Update path history
    m_phist = (m_phist << 1) | (ip & 1);
}

// =============================================================================
// Helper Functions
// =============================================================================

int TAGEBranchPredictor::getIndex(IntPtr ip, int table) const
{
    int hist_len = HIST_LENGTHS[table];
    
    // Fold history to LOGG bits
    uint64_t folded_hist = getFoldedHistory(hist_len, LOGG);
    
    // XOR with PC
    uint64_t index = (ip >> 2) ^ folded_hist ^ (folded_hist >> LOGG);
    
    return index & (TABLE_SIZE - 1);
}

uint16_t TAGEBranchPredictor::getTag(IntPtr ip, int table) const
{
    int hist_len = HIST_LENGTHS[table];
    
    // Use different folding for tag to decorrelate from index
    uint64_t folded_hist1 = getFoldedHistory(hist_len, TBITS);
    uint64_t folded_hist2 = getFoldedHistory(hist_len, TBITS - 1);
    
    // Include path history
    uint64_t tag = (ip >> 2) ^ folded_hist1 ^ (folded_hist2 << 1) ^ (m_phist & ((1ULL << TBITS) - 1));
    
    return tag & ((1 << TBITS) - 1);
}

uint64_t TAGEBranchPredictor::getFoldedHistory(int length, int outwidth) const
{
    uint64_t folded = 0;
    
    // Fold the history by XORing chunks
    for (int i = 0; i < length; i += outwidth) {
        uint64_t chunk = 0;
        for (int j = 0; j < outwidth && (i + j) < length; j++) {
            int pos = (m_ghist_ptr + i + j) % MAX_HIST;
            if (m_ghist[pos]) {
                chunk |= (1ULL << j);
            }
        }
        folded ^= chunk;
    }
    
    return folded;
}

bool TAGEBranchPredictor::getBimodalPrediction(IntPtr ip) const
{
    int index = (ip >> 2) & (BIMODAL_SIZE - 1);
    return m_bimodal[index] >= 2;
}

void TAGEBranchPredictor::updateBimodal(IntPtr ip, bool taken)
{
    int index = (ip >> 2) & (BIMODAL_SIZE - 1);
    if (taken) {
        if (m_bimodal[index] < 3) m_bimodal[index]++;
    } else {
        if (m_bimodal[index] > 0) m_bimodal[index]--;
    }
}

int8_t TAGEBranchPredictor::saturateCounter(int value, int bits) const
{
    int max_val = (1 << (bits - 1)) - 1;   // e.g., 3 for 3 bits
    int min_val = -(1 << (bits - 1));      // e.g., -4 for 3 bits
    
    if (value > max_val) return max_val;
    if (value < min_val) return min_val;
    return value;
}

uint32_t TAGEBranchPredictor::random()
{
    // Simple xorshift32
    m_rand_seed ^= m_rand_seed << 13;
    m_rand_seed ^= m_rand_seed >> 17;
    m_rand_seed ^= m_rand_seed << 5;
    return m_rand_seed;
}
