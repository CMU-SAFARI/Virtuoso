/**
 * SPP (Signature Path Prefetcher) for Sniper
 * 
 * Ported from ChampSim's spp_dev implementation.
 */

#include "spp_prefetcher.h" 
#include "simulator.h"
#include "config.hpp"
#include "stats.h"
#include <cmath>
#include <algorithm>

// =============================================================================
// Constructor / Destructor
// =============================================================================

SPPPrefetcher::SPPPrefetcher(String configName, core_id_t core_id)
    : m_core_id(core_id)
    , m_lookahead_on(Sim()->getCfg()->getBoolDefault("perf_model/" + configName + "/prefetcher/spp/lookahead_on", true))
    , m_filter_on(Sim()->getCfg()->getBoolDefault("perf_model/" + configName + "/prefetcher/spp/filter_on", true))
    , m_ghr_on(Sim()->getCfg()->getBoolDefault("perf_model/" + configName + "/prefetcher/spp/ghr_on", true))
{
    bzero(&stats, sizeof(stats));
    registerStatsMetric("spp", m_core_id, "pref_called", &stats.pref_called);
    registerStatsMetric("spp", m_core_id, "prefetches_issued", &stats.prefetches_issued);
    registerStatsMetric("spp", m_core_id, "st_hits", &stats.st_hits);
    registerStatsMetric("spp", m_core_id, "st_misses", &stats.st_misses);
    registerStatsMetric("spp", m_core_id, "filter_hits", &stats.filter_hits);
}

SPPPrefetcher::~SPPPrefetcher() {}

// =============================================================================
// Main Prefetch Interface
// =============================================================================

std::vector<IntPtr> SPPPrefetcher::getNextAddress(IntPtr current_address, core_id_t core_id,
                                                   Core::mem_op_t mem_op_type, bool cache_hit,
                                                   bool prefetch_hit, IntPtr eip)
{
    std::vector<IntPtr> pref_addr;
    
    // Only prefetch on read operations
    if (mem_op_type == Core::WRITE) {
        return pref_addr;
    }

    stats.pref_called++;

    IntPtr page = current_address >> LOG2_PAGE_SIZE;
    uint32_t page_offset = (current_address >> LOG2_BLOCK_SIZE) & (BLOCKS_PER_PAGE - 1);

    uint32_t last_sig = 0, curr_sig = 0, depth = 0;
    int32_t delta = 0;

    const uint32_t MSHR_SIZE = 16; // Typical MSHR size
    std::vector<uint32_t> confidence_q(MSHR_SIZE, 0);
    std::vector<int32_t> delta_q(MSHR_SIZE, 0);

    confidence_q[0] = 100;
    GHR.global_accuracy = GHR.pf_issued ? ((100 * GHR.pf_useful) / GHR.pf_issued) : 0;

    // Stage 1: Read and update signature stored in ST
    ST.read_and_update_sig(current_address, last_sig, curr_sig, delta);

    // Check prefetch filter to update global accuracy counters
    if (m_filter_on) {
        FILTER.check(current_address, L2C_DEMAND, GHR.pf_useful, GHR.pf_issued);
    }

    // Stage 2: Update delta patterns stored in PT
    if (last_sig) {
        PT.update_pattern(last_sig, delta);
    }

    // Stage 3: Start prefetching
    IntPtr base_addr = current_address;
    uint32_t lookahead_conf = 100, pf_q_head = 0, pf_q_tail = 0;
    bool do_lookahead = false;

    do {
        uint32_t lookahead_way = PT_WAY;
        PT.read_pattern(curr_sig, delta_q, confidence_q, lookahead_way, lookahead_conf, pf_q_tail, depth);

        do_lookahead = false;
        for (uint32_t i = pf_q_head; i < pf_q_tail; i++) {
            if (confidence_q[i] >= PF_THRESHOLD) {
                IntPtr pf_block = (base_addr >> LOG2_BLOCK_SIZE) + delta_q[i];
                IntPtr pf_address = pf_block << LOG2_BLOCK_SIZE;
                IntPtr pf_page = pf_address >> LOG2_PAGE_SIZE;

                if (pf_page == page) {
                    // Prefetch within same page
                    bool should_prefetch = true;
                    
                    if (m_filter_on) {
                        should_prefetch = FILTER.check(pf_address, 
                            (confidence_q[i] >= FILL_THRESHOLD) ? SPP_L2C_PREFETCH : SPP_LLC_PREFETCH,
                            GHR.pf_useful, GHR.pf_issued);
                    }

                    if (should_prefetch) {
                        pref_addr.push_back(pf_address);
                        stats.prefetches_issued++;

                        if (confidence_q[i] >= FILL_THRESHOLD) {
                            GHR.pf_issued++;
                            if (GHR.pf_issued > GLOBAL_COUNTER_MAX) {
                                GHR.pf_issued >>= 1;
                                GHR.pf_useful >>= 1;
                            }
                        }
                    } else {
                        stats.filter_hits++;
                    }
                } else if (m_ghr_on) {
                    // Cross-page: store in GHR for bootstrapping
                    uint32_t pf_offset = (pf_address >> LOG2_BLOCK_SIZE) & (BLOCKS_PER_PAGE - 1);
                    GHR.update_entry(curr_sig, confidence_q[i], pf_offset, delta_q[i]);
                }

                do_lookahead = true;
                pf_q_head++;
            }
        }

        // Update base_addr and curr_sig for lookahead
        if (lookahead_way < PT_WAY) {
            uint32_t set = get_hash(curr_sig) % PT_SET;
            base_addr += (PT.delta[set][lookahead_way] << LOG2_BLOCK_SIZE);

            auto sig_delta = (PT.delta[set][lookahead_way] < 0) 
                ? (((-1) * PT.delta[set][lookahead_way]) + (1 << (SIG_DELTA_BIT - 1))) 
                : PT.delta[set][lookahead_way];
            curr_sig = ((curr_sig << SIG_SHIFT) ^ sig_delta) & SIG_MASK;
        }

    } while (m_lookahead_on && do_lookahead);

    return pref_addr;
}

// =============================================================================
// Hash Function
// =============================================================================

uint64_t SPPPrefetcher::get_hash(uint64_t key)
{
    // Robert Jenkins' 32 bit mix function
    key += (key << 12);
    key ^= (key >> 22);
    key += (key << 4);
    key ^= (key >> 9);
    key += (key << 10);
    key ^= (key >> 2);
    key += (key << 7);
    key ^= (key >> 12);

    // Knuth's multiplicative method
    key = (key >> 3) * 2654435761;

    return key;
}

// =============================================================================
// Signature Table Implementation
// =============================================================================

SPPPrefetcher::SignatureTable::SignatureTable()
{
    for (uint32_t set = 0; set < ST_SET; set++) {
        for (uint32_t way = 0; way < ST_WAY; way++) {
            valid[set][way] = false;
            tag[set][way] = 0;
            last_offset[set][way] = 0;
            sig[set][way] = 0;
            lru[set][way] = way;
        }
    }
}

void SPPPrefetcher::SignatureTable::read_and_update_sig(IntPtr addr, uint32_t& last_sig, 
                                                         uint32_t& curr_sig, int32_t& delta)
{
    IntPtr page = addr >> SPPPrefetcher::LOG2_PAGE_SIZE;
    uint32_t page_offset = (addr >> SPPPrefetcher::LOG2_BLOCK_SIZE) & (SPPPrefetcher::BLOCKS_PER_PAGE - 1);
    
    auto set = SPPPrefetcher::get_hash(page) % ST_SET;
    uint64_t partial_page = page & ((1ULL << ST_TAG_BIT) - 1);
    
    uint32_t match = ST_WAY;
    bool st_hit = false;

    // Search for existing entry
    for (uint32_t way = 0; way < ST_WAY; way++) {
        if (valid[set][way] && tag[set][way] == partial_page) {
            match = way;
            last_sig = sig[set][way];
            delta = static_cast<int32_t>(page_offset) - static_cast<int32_t>(last_offset[set][way]);

            if (delta != 0) {
                // Build new signature
                int32_t sig_delta = (delta < 0) 
                    ? (((-1) * delta) + (1 << (SIG_DELTA_BIT - 1))) 
                    : delta;
                sig[set][way] = ((last_sig << SIG_SHIFT) ^ sig_delta) & SIG_MASK;
                curr_sig = sig[set][way];
                last_offset[set][way] = page_offset;
            } else {
                last_sig = 0; // Same cache line
            }
            st_hit = true;
            break;
        }
    }

    // If not found, look for invalid entry
    if (!st_hit) {
        for (uint32_t way = 0; way < ST_WAY; way++) {
            if (!valid[set][way]) {
                match = way;
                valid[set][way] = true;
                tag[set][way] = partial_page;
                sig[set][way] = 0;
                curr_sig = 0;
                last_offset[set][way] = page_offset;
                st_hit = true;
                break;
            }
        }
    }

    // If still not found, use LRU replacement
    if (!st_hit) {
        for (uint32_t way = 0; way < ST_WAY; way++) {
            if (lru[set][way] == ST_WAY - 1) {
                match = way;
                tag[set][way] = partial_page;
                sig[set][way] = 0;
                curr_sig = 0;
                last_offset[set][way] = page_offset;
                break;
            }
        }
    }

    // Update LRU
    if (match < ST_WAY) {
        for (uint32_t way = 0; way < ST_WAY; way++) {
            if (lru[set][way] < lru[set][match]) {
                lru[set][way]++;
            }
        }
        lru[set][match] = 0;
    }
}

// =============================================================================
// Pattern Table Implementation
// =============================================================================

SPPPrefetcher::PatternTable::PatternTable()
{
    for (uint32_t set = 0; set < PT_SET; set++) {
        for (uint32_t way = 0; way < PT_WAY; way++) {
            delta[set][way] = 0;
            c_delta[set][way] = 0;
        }
        c_sig[set] = 0;
    }
}

void SPPPrefetcher::PatternTable::update_pattern(uint32_t last_sig, int32_t curr_delta)
{
    uint32_t set = SPPPrefetcher::get_hash(last_sig) % PT_SET;

    // Update pattern
    for (uint32_t way = 0; way < PT_WAY; way++) {
        if (delta[set][way] == curr_delta) {
            // Found matching delta, increment counter
            if (c_delta[set][way] < C_DELTA_MAX) {
                c_delta[set][way]++;
            }
            if (c_sig[set] < C_SIG_MAX) {
                c_sig[set]++;
            }
            return;
        }
    }

    // Not found, find victim way
    uint32_t victim_way = 0;
    uint32_t min_counter = c_delta[set][0];
    for (uint32_t way = 1; way < PT_WAY; way++) {
        if (c_delta[set][way] < min_counter) {
            min_counter = c_delta[set][way];
            victim_way = way;
        }
    }

    // Replace victim
    delta[set][victim_way] = curr_delta;
    c_delta[set][victim_way] = 0;
    if (c_sig[set] < C_SIG_MAX) {
        c_sig[set]++;
    }
}

void SPPPrefetcher::PatternTable::read_pattern(uint32_t curr_sig, std::vector<int32_t>& prefetch_delta,
                                                std::vector<uint32_t>& confidence_q, uint32_t& lookahead_way,
                                                uint32_t& lookahead_conf, uint32_t& pf_q_tail, uint32_t& depth)
{
    uint32_t set = SPPPrefetcher::get_hash(curr_sig) % PT_SET;

    if (c_sig[set] == 0) {
        return;
    }

    // Find patterns with high confidence
    for (uint32_t way = 0; way < PT_WAY; way++) {
        if (c_delta[set][way] > 0 && pf_q_tail < prefetch_delta.size()) {
            uint32_t conf = (100 * c_delta[set][way]) / c_sig[set];
            
            if (conf >= PF_THRESHOLD) {
                prefetch_delta[pf_q_tail] = delta[set][way];
                confidence_q[pf_q_tail] = conf;

                // Track best lookahead candidate
                if (conf > lookahead_conf) {
                    lookahead_conf = conf;
                    lookahead_way = way;
                }

                pf_q_tail++;
                depth++;
            }
        }
    }
}

// =============================================================================
// Prefetch Filter Implementation
// =============================================================================

SPPPrefetcher::PrefetchFilter::PrefetchFilter()
{
    for (uint32_t set = 0; set < FILTER_SET; set++) {
        remainder_tag[set] = 0;
        valid[set] = false;
        useful[set] = false;
    }
}

bool SPPPrefetcher::PrefetchFilter::check(IntPtr pf_addr, FilterRequest filter_request,
                                           uint32_t& pf_useful_counter, uint32_t& pf_issued_counter)
{
    uint64_t cache_line = pf_addr >> SPPPrefetcher::LOG2_BLOCK_SIZE;
    uint64_t hash = SPPPrefetcher::get_hash(cache_line);
    uint64_t quotient = (hash >> REMAINDER_BIT) & ((1 << QUOTIENT_BIT) - 1);
    uint64_t remainder = hash & ((1 << REMAINDER_BIT) - 1);

    switch (filter_request) {
        case SPP_L2C_PREFETCH:
        case SPP_LLC_PREFETCH:
            if (valid[quotient] && remainder_tag[quotient] == remainder) {
                return false; // Already prefetched
            }
            valid[quotient] = true;
            useful[quotient] = false;
            remainder_tag[quotient] = remainder;
            return true;

        case L2C_DEMAND:
            if (valid[quotient] && remainder_tag[quotient] == remainder) {
                if (!useful[quotient]) {
                    useful[quotient] = true;
                    if (pf_useful_counter < GLOBAL_COUNTER_MAX) {
                        pf_useful_counter++;
                    }
                }
            }
            return false;

        case L2C_EVICT:
            if (valid[quotient] && remainder_tag[quotient] == remainder) {
                valid[quotient] = false;
            }
            return false;
    }

    return false;
}

// =============================================================================
// Global History Register Implementation
// =============================================================================

SPPPrefetcher::GlobalRegister::GlobalRegister()
{
    pf_useful = 0;
    pf_issued = 0;
    global_accuracy = 0;

    for (uint32_t i = 0; i < MAX_GHR_ENTRY; i++) {
        valid[i] = 0;
        sig[i] = 0;
        confidence[i] = 0;
        offset[i] = 0;
        delta[i] = 0;
    }
}

void SPPPrefetcher::GlobalRegister::update_entry(uint32_t pf_sig, uint32_t pf_confidence,
                                                  uint32_t pf_offset, int32_t pf_delta)
{
    // Find invalid or oldest entry
    uint32_t victim = 0;
    for (uint32_t i = 0; i < MAX_GHR_ENTRY; i++) {
        if (!valid[i]) {
            victim = i;
            break;
        }
        if (confidence[i] < confidence[victim]) {
            victim = i;
        }
    }

    valid[victim] = 1;
    sig[victim] = pf_sig;
    confidence[victim] = pf_confidence;
    offset[victim] = pf_offset;
    delta[victim] = pf_delta;
}

uint32_t SPPPrefetcher::GlobalRegister::check_entry(uint32_t page_offset)
{
    for (uint32_t i = 0; i < MAX_GHR_ENTRY; i++) {
        if (valid[i] && offset[i] == page_offset) {
            return sig[i];
        }
    }
    return 0;
}
