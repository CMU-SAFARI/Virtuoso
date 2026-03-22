/**
 * Berti Prefetcher for Sniper
 * 
 * Ported from ChampSim's Berti implementation.
 * 
 * Berti learns the best "berti" (stride/delta) to prefetch for each page
 * by tracking access patterns and timing information. It uses:
 * - Current Pages Table: Tracks active pages and their access patterns
 * - Previous Requests Table: Tracks recent requests for timing
 * - Record Pages Table: Stores learned patterns for reuse
 * - IP Table: Associates instruction pointers with patterns
 */

#ifndef BERTI_PREFETCHER_H
#define BERTI_PREFETCHER_H

#include "prefetcher.h"
#include "fixed_types.h"
#include "core.h"
#include <vector>
#include <cstdint>

class BertiPrefetcher : public Prefetcher
{
public:
    BertiPrefetcher(String configName, core_id_t core_id);
    ~BertiPrefetcher();

    std::vector<IntPtr> getNextAddress(IntPtr current_address, core_id_t core_id,
                                        Core::mem_op_t mem_op_type, bool cache_hit,
                                        bool prefetch_hit, IntPtr eip) override;

private:
    core_id_t m_core_id;
    uint64_t m_current_cycle;  // Simulated cycle counter

    // Configuration constants
    static constexpr unsigned LOG2_PAGE_SIZE = 12;
    static constexpr unsigned LOG2_BLOCK_SIZE = 6;
    static constexpr unsigned PAGE_BLOCKS_BITS = LOG2_PAGE_SIZE - LOG2_BLOCK_SIZE;
    static constexpr unsigned PAGE_BLOCKS = 1 << PAGE_BLOCKS_BITS;
    static constexpr unsigned PAGE_OFFSET_MASK = PAGE_BLOCKS - 1;
    static constexpr unsigned MAX_BURST_PREFETCHES = 3;
    static constexpr unsigned BERTI_CTR_CONFIDENCE = 2;

    // Table sizes
    static constexpr unsigned CURRENT_PAGES_ENTRIES = 63;
    static constexpr unsigned NUM_BERTI = 10;
    static constexpr unsigned PREV_REQUESTS_ENTRIES = 1024;
    static constexpr unsigned RECORD_PAGES_ENTRIES = 1024;
    static constexpr unsigned IP_TABLE_ENTRIES = 1024;

    // Current Pages Table Entry
    struct CurrentPageEntry {
        uint64_t page_addr;
        uint64_t ip;
        uint64_t u_vector;          // Usage vector (which blocks accessed)
        uint64_t first_offset;
        int berti[NUM_BERTI];       // Learned berti values
        unsigned berti_ctr[NUM_BERTI];  // Confidence counters
        uint64_t lru;

        CurrentPageEntry() : page_addr(0), ip(0), u_vector(0), first_offset(0), lru(0) {
            for (unsigned i = 0; i < NUM_BERTI; i++) {
                berti[i] = 0;
                berti_ctr[i] = 0;
            }
        }
    };

    // Previous Requests Table Entry
    struct PrevRequestEntry {
        uint64_t page_index;
        uint64_t offset;
        uint64_t time;

        PrevRequestEntry() : page_index(CURRENT_PAGES_ENTRIES), offset(0), time(0) {}
    };

    // Record Pages Table Entry
    struct RecordPageEntry {
        uint64_t page_addr;
        uint64_t u_vector;
        uint64_t first_offset;
        int berti;
        uint64_t lru;

        RecordPageEntry() : page_addr(0), u_vector(0), first_offset(0), berti(0), lru(0) {}
    };

    // Tables
    CurrentPageEntry current_pages[CURRENT_PAGES_ENTRIES];
    PrevRequestEntry prev_requests[PREV_REQUESTS_ENTRIES];
    uint64_t prev_requests_head;
    RecordPageEntry record_pages[RECORD_PAGES_ENTRIES];
    uint64_t ip_table[IP_TABLE_ENTRIES];

    // Current Pages Table operations
    void init_current_pages_table();
    uint64_t get_current_pages_entry(uint64_t page_addr);
    void update_lru_current_pages(uint64_t index);
    uint64_t get_lru_current_pages_entry();
    int get_berti_current_pages(uint64_t index, unsigned& confidence);
    void add_current_pages_entry(uint64_t index, uint64_t page_addr, uint64_t ip, uint64_t offset);
    uint64_t update_demand_current_pages(uint64_t index, uint64_t offset);
    void add_berti_current_pages(uint64_t index, int b);
    bool requested_offset_current_pages(uint64_t index, uint64_t offset);
    void remove_current_pages_entry(uint64_t index);

    // Previous Requests Table operations
    void init_prev_requests_table();
    uint64_t find_prev_request(uint64_t pointer, uint64_t offset);
    void add_prev_request(uint64_t pointer, uint64_t offset, uint64_t cycle);
    void reset_pointer_prev_requests(uint64_t pointer);
    uint64_t get_latency_prev_requests(uint64_t pointer, uint64_t offset, uint64_t cycle);
    void get_berti_prev_requests(uint64_t pointer, uint64_t offset, uint64_t cycle, int* b);

    // Record Pages Table operations
    void init_record_pages_table();
    uint64_t get_lru_record_pages_entry();
    void update_lru_record_pages(uint64_t index);
    void add_record_pages_entry(uint64_t index, uint64_t page_addr, uint64_t vector, 
                                uint64_t first_offset, int berti);
    uint64_t get_record_pages_entry(uint64_t page_addr, uint64_t first_offset);
    uint64_t get_record_pages_entry(uint64_t page_addr);
    void copy_record_pages_entry(uint64_t from, uint64_t to);
    void record_current_page(uint64_t index);

    // IP Table operations
    void init_ip_table();

    // Helper
    int calculate_stride(uint64_t prev_offset, uint64_t current_offset);
    uint64_t get_latency(uint64_t cycle, uint64_t cycle_prev);

    // Statistics
    struct {
        UInt64 pref_called;
        UInt64 current_pages_hits;
        UInt64 record_pages_hits;
        UInt64 prefetches_issued;
        UInt64 berti_predictions;
    } stats;
};

#endif /* BERTI_PREFETCHER_H */
