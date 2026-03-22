/**
 * Berti Prefetcher for Sniper
 * 
 * Ported from ChampSim's Berti implementation.
 */

#include "berti_prefetcher.h"
#include "simulator.h"
#include "config.hpp"
#include "stats.h"
#include <cmath>
#include <algorithm>
#include <cassert>

// =============================================================================
// Constructor / Destructor
// =============================================================================

BertiPrefetcher::BertiPrefetcher(String configName, core_id_t core_id)
    : m_core_id(core_id)
    , m_current_cycle(0)
    , prev_requests_head(0)
{
    init_current_pages_table();
    init_prev_requests_table();
    init_record_pages_table();
    init_ip_table();

    bzero(&stats, sizeof(stats));
    registerStatsMetric("berti", m_core_id, "pref_called", &stats.pref_called);
    registerStatsMetric("berti", m_core_id, "current_pages_hits", &stats.current_pages_hits);
    registerStatsMetric("berti", m_core_id, "record_pages_hits", &stats.record_pages_hits);
    registerStatsMetric("berti", m_core_id, "prefetches_issued", &stats.prefetches_issued);
    registerStatsMetric("berti", m_core_id, "berti_predictions", &stats.berti_predictions);
}

BertiPrefetcher::~BertiPrefetcher() {}

// =============================================================================
// Main Prefetch Interface
// =============================================================================

std::vector<IntPtr> BertiPrefetcher::getNextAddress(IntPtr current_address, core_id_t core_id,
                                                     Core::mem_op_t mem_op_type, bool cache_hit,
                                                     bool prefetch_hit, IntPtr eip)
{
    std::vector<IntPtr> pref_addr;
    
    // Only prefetch on read operations
    if (mem_op_type == Core::WRITE) {
        return pref_addr;
    }

    stats.pref_called++;
    m_current_cycle++;  // Simple cycle counter

    uint64_t line_addr = current_address >> LOG2_BLOCK_SIZE;
    uint64_t page_addr = line_addr >> PAGE_BLOCKS_BITS;
    uint64_t offset = line_addr & PAGE_OFFSET_MASK;

    // Find entry in current pages table
    uint64_t index = get_current_pages_entry(page_addr);

    // Check if this offset was already requested
    if (index == CURRENT_PAGES_ENTRIES || !requested_offset_current_pages(index, offset)) {
        
        if (index < CURRENT_PAGES_ENTRIES) {
            // Found in current pages table
            stats.current_pages_hits++;

            if (requested_offset_current_pages(index, offset)) {
                return pref_addr;
            }

            uint64_t first_ip = update_demand_current_pages(index, offset);

            // Update berti learning on cache hit (prefetch was useful)
            if (cache_hit) {
                int b[7] = {0};
                get_berti_prev_requests(index, offset, m_current_cycle, b);
                for (int i = 0; i < 7 && b[i] != 0; i++) {
                    if (std::abs(b[i]) < static_cast<int>(PAGE_BLOCKS)) {
                        add_berti_current_pages(index, b[i]);
                    }
                }
            }

            // Associate IP with same pointer if different
            if (first_ip != eip) {
                ip_table[eip & (IP_TABLE_ENTRIES - 1)] = ip_table[first_ip & (IP_TABLE_ENTRIES - 1)];
            }
        } else {
            // Not found - add new entry
            uint64_t victim_index = get_lru_current_pages_entry();
            reset_pointer_prev_requests(victim_index);
            record_current_page(victim_index);

            index = victim_index;
            add_current_pages_entry(index, page_addr, eip & (IP_TABLE_ENTRIES - 1), offset);

            // Set pointer in IP table
            uint64_t index_record = get_record_pages_entry(page_addr, offset);
            if (ip_table[eip & (IP_TABLE_ENTRIES - 1)] == RECORD_PAGES_ENTRIES) {
                if (index_record == RECORD_PAGES_ENTRIES) {
                    uint64_t new_pointer = get_lru_record_pages_entry();
                    ip_table[eip & (IP_TABLE_ENTRIES - 1)] = new_pointer;
                } else {
                    ip_table[eip & (IP_TABLE_ENTRIES - 1)] = index_record;
                }
            }
        }

        add_prev_request(index, offset, m_current_cycle);

        // PREDICT
        uint64_t u_vector = 0;
        uint64_t first_offset = current_pages[index].first_offset;
        int berti = 0;
        bool recorded = false;
        unsigned berti_confidence = 0;

        uint64_t ip_pointer = ip_table[eip & (IP_TABLE_ENTRIES - 1)];
        uint64_t pgo_pointer = get_record_pages_entry(page_addr, first_offset);
        uint64_t pg_pointer = get_record_pages_entry(page_addr);
        int current_berti = get_berti_current_pages(index, berti_confidence);

        // Priority matching: page+offset > ip+offset > current berti > page > ip
        if (pgo_pointer != RECORD_PAGES_ENTRIES &&
            (record_pages[pgo_pointer].u_vector | current_pages[index].u_vector) == record_pages[pgo_pointer].u_vector) {
            u_vector = record_pages[pgo_pointer].u_vector;
            berti = record_pages[pgo_pointer].berti;
            recorded = true;
            stats.record_pages_hits++;
        } else if (ip_pointer < RECORD_PAGES_ENTRIES && 
                   record_pages[ip_pointer].first_offset == first_offset &&
                   (record_pages[ip_pointer].u_vector | current_pages[index].u_vector) == record_pages[ip_pointer].u_vector) {
            u_vector = record_pages[ip_pointer].u_vector;
            berti = record_pages[ip_pointer].berti;
            recorded = true;
            stats.record_pages_hits++;
        } else if (current_berti != 0 && berti_confidence >= BERTI_CTR_CONFIDENCE) {
            u_vector = current_pages[index].u_vector;
            berti = current_berti;
        } else if (pg_pointer != RECORD_PAGES_ENTRIES) {
            u_vector = record_pages[pg_pointer].u_vector;
            berti = record_pages[pg_pointer].berti;
            recorded = true;
        } else if (ip_pointer < RECORD_PAGES_ENTRIES && record_pages[ip_pointer].u_vector) {
            u_vector = record_pages[ip_pointer].u_vector;
            berti = record_pages[ip_pointer].berti;
            recorded = true;
        }

        // Generate prefetches based on berti
        if (berti != 0) {
            stats.berti_predictions++;
            
            // Main berti prefetch
            int64_t pf_line = static_cast<int64_t>(line_addr) + berti;
            if (pf_line >= 0) {
                uint64_t pf_page = static_cast<uint64_t>(pf_line) >> PAGE_BLOCKS_BITS;
                uint64_t pf_offset = static_cast<uint64_t>(pf_line) & PAGE_OFFSET_MASK;
                
                if (pf_page == page_addr && !requested_offset_current_pages(index, pf_offset)) {
                    IntPtr pf_address = static_cast<IntPtr>(pf_line) << LOG2_BLOCK_SIZE;
                    pref_addr.push_back(pf_address);
                    stats.prefetches_issued++;
                }
            }

            // Burst prefetching for first access to a page
            if (first_offset == offset && recorded) {
                unsigned bursts = 0;
                if (berti > 0) {
                    for (uint64_t i = offset + 1; i < offset + static_cast<uint64_t>(berti) && bursts < MAX_BURST_PREFETCHES; i++) {
                        if (i >= PAGE_BLOCKS) break;
                        if ((((uint64_t)1 << i) & u_vector) && !requested_offset_current_pages(index, i)) {
                            IntPtr pf_address = ((page_addr << PAGE_BLOCKS_BITS) | i) << LOG2_BLOCK_SIZE;
                            pref_addr.push_back(pf_address);
                            stats.prefetches_issued++;
                            bursts++;
                        }
                    }
                } else {
                    for (int64_t i = static_cast<int64_t>(offset) - 1; 
                         i > static_cast<int64_t>(offset) + berti && bursts < MAX_BURST_PREFETCHES; i--) {
                        if (i < 0) break;
                        if ((((uint64_t)1 << i) & u_vector) && !requested_offset_current_pages(index, static_cast<uint64_t>(i))) {
                            IntPtr pf_address = ((page_addr << PAGE_BLOCKS_BITS) | static_cast<uint64_t>(i)) << LOG2_BLOCK_SIZE;
                            pref_addr.push_back(pf_address);
                            stats.prefetches_issued++;
                            bursts++;
                        }
                    }
                }
            }
        }
    }

    return pref_addr;
}

// =============================================================================
// Current Pages Table Operations
// =============================================================================

void BertiPrefetcher::init_current_pages_table()
{
    for (unsigned i = 0; i < CURRENT_PAGES_ENTRIES; i++) {
        current_pages[i] = CurrentPageEntry();
        current_pages[i].lru = i;
    }
}

uint64_t BertiPrefetcher::get_current_pages_entry(uint64_t page_addr)
{
    for (unsigned i = 0; i < CURRENT_PAGES_ENTRIES; i++) {
        if (current_pages[i].page_addr == page_addr && current_pages[i].u_vector != 0) {
            return i;
        }
    }
    return CURRENT_PAGES_ENTRIES;
}

void BertiPrefetcher::update_lru_current_pages(uint64_t index)
{
    for (unsigned i = 0; i < CURRENT_PAGES_ENTRIES; i++) {
        if (current_pages[i].lru < current_pages[index].lru) {
            current_pages[i].lru++;
        }
    }
    current_pages[index].lru = 0;
}

uint64_t BertiPrefetcher::get_lru_current_pages_entry()
{
    uint64_t lru = CURRENT_PAGES_ENTRIES;
    for (unsigned i = 0; i < CURRENT_PAGES_ENTRIES; i++) {
        current_pages[i].lru++;
        if (current_pages[i].lru == CURRENT_PAGES_ENTRIES) {
            current_pages[i].lru = 0;
            lru = i;
        }
    }
    return lru;
}

int BertiPrefetcher::get_berti_current_pages(uint64_t index, unsigned& confidence)
{
    unsigned max_score = 0;
    int best_berti = 0;
    confidence = 0;
    
    for (unsigned i = 0; i < NUM_BERTI; i++) {
        if (current_pages[index].berti_ctr[i] > max_score) {
            best_berti = current_pages[index].berti[i];
            max_score = current_pages[index].berti_ctr[i];
            confidence = current_pages[index].berti_ctr[i];
        }
    }
    return best_berti;
}

void BertiPrefetcher::add_current_pages_entry(uint64_t index, uint64_t page_addr, 
                                               uint64_t ip, uint64_t offset)
{
    current_pages[index].page_addr = page_addr;
    current_pages[index].ip = ip;
    current_pages[index].u_vector = (uint64_t)1 << offset;
    current_pages[index].first_offset = offset;
    for (unsigned i = 0; i < NUM_BERTI; i++) {
        current_pages[index].berti[i] = 0;
        current_pages[index].berti_ctr[i] = 0;
    }
}

uint64_t BertiPrefetcher::update_demand_current_pages(uint64_t index, uint64_t offset)
{
    current_pages[index].u_vector |= (uint64_t)1 << offset;
    update_lru_current_pages(index);
    return current_pages[index].ip;
}

void BertiPrefetcher::add_berti_current_pages(uint64_t index, int b)
{
    if (b == 0) return;
    
    for (unsigned i = 0; i < NUM_BERTI; i++) {
        if (current_pages[index].berti_ctr[i] == 0) {
            current_pages[index].berti[i] = b;
            current_pages[index].berti_ctr[i] = 1;
            break;
        } else if (current_pages[index].berti[i] == b) {
            current_pages[index].berti_ctr[i]++;
            break;
        }
    }
    update_lru_current_pages(index);
}

bool BertiPrefetcher::requested_offset_current_pages(uint64_t index, uint64_t offset)
{
    return current_pages[index].u_vector & ((uint64_t)1 << offset);
}

void BertiPrefetcher::remove_current_pages_entry(uint64_t index)
{
    current_pages[index].page_addr = 0;
    current_pages[index].u_vector = 0;
    current_pages[index].berti[0] = 0;
}

// =============================================================================
// Previous Requests Table Operations
// =============================================================================

void BertiPrefetcher::init_prev_requests_table()
{
    prev_requests_head = 0;
    for (unsigned i = 0; i < PREV_REQUESTS_ENTRIES; i++) {
        prev_requests[i] = PrevRequestEntry();
    }
}

uint64_t BertiPrefetcher::find_prev_request(uint64_t pointer, uint64_t offset)
{
    for (unsigned i = 0; i < PREV_REQUESTS_ENTRIES; i++) {
        if (prev_requests[i].page_index == pointer && prev_requests[i].offset == offset) {
            return i;
        }
    }
    return PREV_REQUESTS_ENTRIES;
}

void BertiPrefetcher::add_prev_request(uint64_t pointer, uint64_t offset, uint64_t cycle)
{
    if (find_prev_request(pointer, offset) != PREV_REQUESTS_ENTRIES) {
        return;  // Already exists
    }
    
    prev_requests[prev_requests_head].page_index = pointer;
    prev_requests[prev_requests_head].offset = offset;
    prev_requests[prev_requests_head].time = cycle;
    prev_requests_head = (prev_requests_head + 1) & (PREV_REQUESTS_ENTRIES - 1);
}

void BertiPrefetcher::reset_pointer_prev_requests(uint64_t pointer)
{
    for (unsigned i = 0; i < PREV_REQUESTS_ENTRIES; i++) {
        if (prev_requests[i].page_index == pointer) {
            prev_requests[i].page_index = CURRENT_PAGES_ENTRIES;
        }
    }
}

uint64_t BertiPrefetcher::get_latency_prev_requests(uint64_t pointer, uint64_t offset, uint64_t cycle)
{
    uint64_t index = find_prev_request(pointer, offset);
    if (index == PREV_REQUESTS_ENTRIES) {
        return 0;
    }
    return get_latency(cycle, prev_requests[index].time);
}

void BertiPrefetcher::get_berti_prev_requests(uint64_t pointer, uint64_t offset, uint64_t cycle, int* b)
{
    int count = 0;
    for (int i = 0; i < 7; i++) b[i] = 0;
    
    // Find requests that happened around the same time
    for (unsigned i = 0; i < PREV_REQUESTS_ENTRIES && count < 7; i++) {
        if (prev_requests[i].page_index == pointer && 
            prev_requests[i].offset != offset) {
            int stride = calculate_stride(prev_requests[i].offset, offset);
            if (stride != 0 && std::abs(stride) < static_cast<int>(PAGE_BLOCKS)) {
                b[count++] = stride;
            }
        }
    }
}

// =============================================================================
// Record Pages Table Operations
// =============================================================================

void BertiPrefetcher::init_record_pages_table()
{
    for (unsigned i = 0; i < RECORD_PAGES_ENTRIES; i++) {
        record_pages[i] = RecordPageEntry();
        record_pages[i].lru = i;
    }
}

uint64_t BertiPrefetcher::get_lru_record_pages_entry()
{
    uint64_t max_lru = 0;
    uint64_t lru_index = 0;
    
    for (unsigned i = 0; i < RECORD_PAGES_ENTRIES; i++) {
        if (record_pages[i].lru > max_lru) {
            max_lru = record_pages[i].lru;
            lru_index = i;
        }
    }
    return lru_index;
}

void BertiPrefetcher::update_lru_record_pages(uint64_t index)
{
    for (unsigned i = 0; i < RECORD_PAGES_ENTRIES; i++) {
        if (record_pages[i].lru < record_pages[index].lru) {
            record_pages[i].lru++;
        }
    }
    record_pages[index].lru = 0;
}

void BertiPrefetcher::add_record_pages_entry(uint64_t index, uint64_t page_addr, 
                                              uint64_t vector, uint64_t first_offset, int berti)
{
    record_pages[index].page_addr = page_addr;
    record_pages[index].u_vector = vector;
    record_pages[index].first_offset = first_offset;
    record_pages[index].berti = berti;
    update_lru_record_pages(index);
}

uint64_t BertiPrefetcher::get_record_pages_entry(uint64_t page_addr, uint64_t first_offset)
{
    for (unsigned i = 0; i < RECORD_PAGES_ENTRIES; i++) {
        if (record_pages[i].page_addr == page_addr && 
            record_pages[i].first_offset == first_offset &&
            record_pages[i].u_vector != 0) {
            update_lru_record_pages(i);
            return i;
        }
    }
    return RECORD_PAGES_ENTRIES;
}

uint64_t BertiPrefetcher::get_record_pages_entry(uint64_t page_addr)
{
    for (unsigned i = 0; i < RECORD_PAGES_ENTRIES; i++) {
        if (record_pages[i].page_addr == page_addr && record_pages[i].u_vector != 0) {
            update_lru_record_pages(i);
            return i;
        }
    }
    return RECORD_PAGES_ENTRIES;
}

void BertiPrefetcher::copy_record_pages_entry(uint64_t from, uint64_t to)
{
    record_pages[to] = record_pages[from];
    update_lru_record_pages(to);
}

void BertiPrefetcher::record_current_page(uint64_t index)
{
    if (current_pages[index].u_vector == 0) return;
    
    unsigned confidence = 0;
    int berti = get_berti_current_pages(index, confidence);
    
    uint64_t record_index = get_lru_record_pages_entry();
    add_record_pages_entry(record_index, 
                           current_pages[index].page_addr,
                           current_pages[index].u_vector,
                           current_pages[index].first_offset,
                           berti);
}

// =============================================================================
// IP Table Operations
// =============================================================================

void BertiPrefetcher::init_ip_table()
{
    for (unsigned i = 0; i < IP_TABLE_ENTRIES; i++) {
        ip_table[i] = RECORD_PAGES_ENTRIES;  // Null pointer
    }
}

// =============================================================================
// Helper Functions
// =============================================================================

int BertiPrefetcher::calculate_stride(uint64_t prev_offset, uint64_t current_offset)
{
    if (current_offset > prev_offset) {
        return static_cast<int>(current_offset - prev_offset);
    } else {
        return -static_cast<int>(prev_offset - current_offset);
    }
}

uint64_t BertiPrefetcher::get_latency(uint64_t cycle, uint64_t cycle_prev)
{
    return cycle - cycle_prev;
}
