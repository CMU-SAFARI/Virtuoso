/**
 * IPCP (Instruction Pointer Classifier Prefetcher) for Sniper
 * 
 * Ported from ChampSim's IPCP implementation.
 * Original paper: "Bouquet of Instruction Pointers: Instruction Pointer 
 * Classifier-based Spatial Hardware Prefetching" (ISCA'20)
 */

#include "ipcp_prefetcher.h"
#include "simulator.h"
#include "config.hpp"
#include "stats.h"
#include <cstring>
#include <algorithm>

// =============================================================================
// Constructor / Destructor
// =============================================================================

IPCPPrefetcher::IPCPPrefetcher(String configName, core_id_t core_id)
    : m_core_id(core_id)
    , m_prefetch_degree(3)
    , m_spec_nl_threshold(15)
    , num_misses(0)
    , prev_cycle(0)
    , mpkc(0.0f)
    , spec_nl(0)
{
    // Initialize tables
    memset(ghb, 0, sizeof(ghb));

    bzero(&stats, sizeof(stats));
    registerStatsMetric("ipcp", m_core_id, "pref_called", &stats.pref_called);
    registerStatsMetric("ipcp", m_core_id, "stream_prefetches", &stats.stream_prefetches);
    registerStatsMetric("ipcp", m_core_id, "cs_prefetches", &stats.cs_prefetches);
    registerStatsMetric("ipcp", m_core_id, "cplx_prefetches", &stats.cplx_prefetches);
    registerStatsMetric("ipcp", m_core_id, "nl_prefetches", &stats.nl_prefetches);
    registerStatsMetric("ipcp", m_core_id, "prefetches_issued", &stats.prefetches_issued);
}

IPCPPrefetcher::~IPCPPrefetcher() {}

// =============================================================================
// Main Prefetch Interface
// =============================================================================

std::vector<IntPtr> IPCPPrefetcher::getNextAddress(IntPtr current_address, core_id_t core_id,
                                                    Core::mem_op_t mem_op_type, bool cache_hit,
                                                    bool prefetch_hit, IntPtr eip)
{
    std::vector<IntPtr> pref_addr;

    // Only prefetch on read operations
    if (mem_op_type == Core::WRITE) {
        return pref_addr;
    }

    stats.pref_called++;

    uint64_t addr = current_address;
    uint64_t ip = eip;
    uint64_t curr_page = addr >> LOG2_PAGE_SIZE;
    uint64_t cl_addr = addr >> LOG2_BLOCK_SIZE;
    uint64_t cl_offset = (addr >> LOG2_BLOCK_SIZE) & 0x3F;
    uint16_t signature = 0, last_signature = 0;
    int num_prefs = 0;

    // Update miss counter
    if (!cache_hit) {
        num_misses++;
    }

    // Update spec_nl bit when num_misses crosses threshold
    // Simple cycle approximation using pref_called as proxy
    if (num_misses >= 256) {
        uint64_t current_cycle = stats.pref_called;
        if (current_cycle > prev_cycle) {
            mpkc = ((float)num_misses / (float)(current_cycle - prev_cycle)) * 1000;
        }
        prev_cycle = current_cycle;
        
        if (mpkc > (float)m_spec_nl_threshold) {
            spec_nl = 0;
        } else {
            spec_nl = 1;
        }
        num_misses = 0;
    }

    // Calculate index and tag
    uint16_t ip_tag = (ip >> NUM_IP_INDEX_BITS) & ((1 << NUM_IP_TAG_BITS) - 1);
    int index = ip & ((1 << NUM_IP_INDEX_BITS) - 1);

    // Check for new/conflict IP
    if (ip_table[index].ip_tag != ip_tag) {
        if (ip_table[index].ip_valid == 0) {
            // New IP, initialize entry
            ip_table[index].ip_tag = ip_tag;
            ip_table[index].last_page = curr_page;
            ip_table[index].last_cl_offset = cl_offset;
            ip_table[index].last_stride = 0;
            ip_table[index].signature = 0;
            ip_table[index].conf = 0;
            ip_table[index].str_valid = 0;
            ip_table[index].str_strength = 0;
            ip_table[index].str_dir = 0;
            ip_table[index].ip_valid = 1;
        } else {
            // Conflict, reset valid bit
            ip_table[index].ip_valid = 0;
        }

        // Issue next-line prefetch for new IP
        uint64_t pf_address = ((addr >> LOG2_BLOCK_SIZE) + 1) << LOG2_BLOCK_SIZE;
        if ((pf_address >> LOG2_PAGE_SIZE) == curr_page) {
            pref_addr.push_back(pf_address);
            stats.nl_prefetches++;
            stats.prefetches_issued++;
        }
        return pref_addr;
    } else {
        // Same IP, set valid
        ip_table[index].ip_valid = 1;
    }

    // Calculate stride
    int64_t stride = 0;
    if (cl_offset > ip_table[index].last_cl_offset) {
        stride = cl_offset - ip_table[index].last_cl_offset;
    } else {
        stride = ip_table[index].last_cl_offset - cl_offset;
        stride *= -1;
    }

    // Skip if same address seen twice
    if (stride == 0) {
        return pref_addr;
    }

    // Page boundary learning
    if (curr_page != ip_table[index].last_page) {
        if (stride < 0) {
            stride += 64;
        } else {
            stride -= 64;
        }
    }

    // Update CS confidence
    ip_table[index].conf = update_conf((int)stride, (int)ip_table[index].last_stride, ip_table[index].conf);

    // Update CS stride only if confidence is zero
    if (ip_table[index].conf == 0) {
        ip_table[index].last_stride = stride;
    }

    // Update CPLX confidence
    last_signature = ip_table[index].signature;
    dpt[last_signature].conf = update_conf((int)stride, dpt[last_signature].delta, dpt[last_signature].conf);

    // Update CPLX delta only if confidence is zero
    if (dpt[last_signature].conf == 0) {
        dpt[last_signature].delta = (int)stride;
    }

    // Calculate and update new signature
    signature = update_signature(last_signature, (int)stride);
    ip_table[index].signature = signature;

    // Check GHB for stream
    check_for_stream(index, cl_addr);

    // Prefetch based on IP classification
    if (ip_table[index].str_valid == 1) {
        // Stream IP - prefetch with twice the usual degree
        unsigned stream_degree = m_prefetch_degree * 2;
        for (unsigned i = 0; i < stream_degree; i++) {
            uint64_t pf_address;
            if (ip_table[index].str_dir == 1) {
                // Positive stream
                pf_address = (cl_addr + i + 1) << LOG2_BLOCK_SIZE;
            } else {
                // Negative stream
                pf_address = (cl_addr - i - 1) << LOG2_BLOCK_SIZE;
            }

            // Check page boundary
            if ((pf_address >> LOG2_PAGE_SIZE) != curr_page) {
                break;
            }

            pref_addr.push_back(pf_address);
            stats.stream_prefetches++;
            num_prefs++;
        }
    } else if (ip_table[index].conf > 1 && ip_table[index].last_stride != 0) {
        // CS (Constant Stride) IP
        for (unsigned i = 0; i < m_prefetch_degree; i++) {
            uint64_t pf_address = (cl_addr + (ip_table[index].last_stride * (i + 1))) << LOG2_BLOCK_SIZE;

            // Check page boundary
            if ((pf_address >> LOG2_PAGE_SIZE) != curr_page) {
                break;
            }

            pref_addr.push_back(pf_address);
            stats.cs_prefetches++;
            num_prefs++;
        }
    } else if (dpt[signature].conf >= 0 && dpt[signature].delta != 0) {
        // CPLX (Complex Stride) IP
        int pref_offset = 0;
        uint16_t curr_sig = signature;
        for (unsigned i = 0; i < m_prefetch_degree; i++) {
            pref_offset += dpt[curr_sig].delta;
            uint64_t pf_address = ((cl_addr + pref_offset) << LOG2_BLOCK_SIZE);

            // Check page boundary and valid delta
            if (((pf_address >> LOG2_PAGE_SIZE) != curr_page) ||
                (dpt[curr_sig].conf == -1) ||
                (dpt[curr_sig].delta == 0)) {
                break;
            }

            // Prefetch only when confidence > 0
            if (dpt[curr_sig].conf > 0) {
                pref_addr.push_back(pf_address);
                stats.cplx_prefetches++;
                num_prefs++;
            }
            curr_sig = update_signature(curr_sig, dpt[curr_sig].delta);
        }
    }

    // Speculative next-line prefetch if no prefetches issued
    if (num_prefs == 0 && spec_nl == 1) {
        uint64_t pf_address = ((addr >> LOG2_BLOCK_SIZE) + 1) << LOG2_BLOCK_SIZE;
        if ((pf_address >> LOG2_PAGE_SIZE) == curr_page) {
            pref_addr.push_back(pf_address);
            stats.nl_prefetches++;
        }
    }

    stats.prefetches_issued += pref_addr.size();

    // Update IP table
    ip_table[index].last_cl_offset = cl_offset;
    ip_table[index].last_page = curr_page;

    // Update GHB
    update_ghb(cl_addr);

    return pref_addr;
}

// =============================================================================
// Helper Functions
// =============================================================================

uint16_t IPCPPrefetcher::update_signature(uint16_t old_sig, int delta)
{
    uint16_t new_sig = 0;
    int sig_delta = 0;

    // 7-bit sign magnitude form for deltas from +63 to -63
    sig_delta = (delta < 0) ? (((-1) * delta) + (1 << 6)) : delta;
    new_sig = ((old_sig << 1) ^ sig_delta) & 0xFFF;  // 12-bit signature

    return new_sig;
}

int IPCPPrefetcher::update_conf(int stride, int pred_stride, int conf)
{
    if (stride == pred_stride) {
        // Increment with 2-bit saturating counter
        conf++;
        if (conf > 3) conf = 3;
    } else {
        conf--;
        if (conf < 0) conf = 0;
    }
    return conf;
}

void IPCPPrefetcher::check_for_stream(int index, uint64_t cl_addr)
{
    int pos_count = 0, neg_count = 0, count = 0;
    uint64_t check_addr = cl_addr;

    // Check for positive stream
    for (unsigned i = 0; i < NUM_GHB_ENTRIES; i++) {
        check_addr--;
        for (unsigned j = 0; j < NUM_GHB_ENTRIES; j++) {
            if (check_addr == ghb[j]) {
                pos_count++;
                break;
            }
        }
    }

    check_addr = cl_addr;
    // Check for negative stream
    for (unsigned i = 0; i < NUM_GHB_ENTRIES; i++) {
        check_addr++;
        for (unsigned j = 0; j < NUM_GHB_ENTRIES; j++) {
            if (check_addr == ghb[j]) {
                neg_count++;
                break;
            }
        }
    }

    if (pos_count > neg_count) {
        ip_table[index].str_dir = 1;
        count = pos_count;
    } else {
        ip_table[index].str_dir = 0;
        count = neg_count;
    }

    if (count > (int)NUM_GHB_ENTRIES / 2) {
        // Stream detected
        ip_table[index].str_valid = 1;
        if (count >= (int)(NUM_GHB_ENTRIES * 3) / 4) {
            // Strong stream
            ip_table[index].str_strength = 1;
        }
    } else {
        if (ip_table[index].str_strength == 0) {
            // Weak stream, reset
            ip_table[index].str_valid = 0;
        }
    }
}

void IPCPPrefetcher::update_ghb(uint64_t cl_addr)
{
    // Search for matching address
    unsigned ghb_index = 0;
    for (ghb_index = 0; ghb_index < NUM_GHB_ENTRIES; ghb_index++) {
        if (cl_addr == ghb[ghb_index]) {
            break;
        }
    }

    // Only update upon finding new address
    if (ghb_index == NUM_GHB_ENTRIES) {
        for (ghb_index = NUM_GHB_ENTRIES - 1; ghb_index > 0; ghb_index--) {
            ghb[ghb_index] = ghb[ghb_index - 1];
        }
        ghb[0] = cl_addr;
    }
}
