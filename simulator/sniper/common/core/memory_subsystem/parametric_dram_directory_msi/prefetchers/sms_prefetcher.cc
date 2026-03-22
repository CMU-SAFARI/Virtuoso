/**
 * SMS (Spatial Memory Streaming) Prefetcher for Sniper
 * 
 * Ported from ChampSim's SMS implementation.
 * Original paper: "Spatial Memory Streaming" (ISCA'06)
 */

#include "sms_prefetcher.h"
#include "simulator.h"
#include "config.hpp"
#include "stats.h"
#include <algorithm>
#include <cmath>

// =============================================================================
// Constructor / Destructor
// =============================================================================

SMSPrefetcher::SMSPrefetcher(String configName, core_id_t core_id)
    : m_core_id(core_id)
    , m_at_size(32)
    , m_ft_size(64)
    , m_pht_size(2048)
    , m_pht_assoc(16)
    , m_pref_degree(4)
    , m_region_size(2048)
    , m_region_size_log(11)  // log2(2048)
    , m_pref_buffer_size(256)
{
    // Initialize PHT with proper number of sets
    uint32_t pht_sets = m_pht_size / m_pht_assoc;
    pht.resize(pht_sets);

    bzero(&stats, sizeof(stats));
    registerStatsMetric("sms", m_core_id, "pref_called", &stats.pref_called);
    registerStatsMetric("sms", m_core_id, "ft_hits", &stats.ft_hits);
    registerStatsMetric("sms", m_core_id, "at_hits", &stats.at_hits);
    registerStatsMetric("sms", m_core_id, "pht_hits", &stats.pht_hits);
    registerStatsMetric("sms", m_core_id, "prefetches_generated", &stats.prefetches_generated);
    registerStatsMetric("sms", m_core_id, "prefetches_issued", &stats.prefetches_issued);
}

SMSPrefetcher::~SMSPrefetcher()
{
    // Clean up filter table
    for (auto* entry : filter_table) {
        delete entry;
    }
    
    // Clean up accumulation table
    for (auto* entry : acc_table) {
        delete entry;
    }
    
    // Clean up PHT
    for (auto& set : pht) {
        for (auto* entry : set) {
            delete entry;
        }
    }
}

// =============================================================================
// Main Prefetch Interface
// =============================================================================

std::vector<IntPtr> SMSPrefetcher::getNextAddress(IntPtr current_address, core_id_t core_id,
                                                   Core::mem_op_t mem_op_type, bool cache_hit,
                                                   bool prefetch_hit, IntPtr eip)
{
    std::vector<IntPtr> pref_addr;
    
    // Only prefetch on read operations
    if (mem_op_type == Core::WRITE) {
        return issue_prefetch(); // Still issue buffered prefetches
    }

    stats.pref_called++;

    uint64_t page = current_address >> m_region_size_log;
    uint32_t offset = static_cast<uint32_t>((current_address >> LOG2_BLOCK_SIZE) & 
                      ((1ULL << (m_region_size_log - LOG2_BLOCK_SIZE)) - 1));

    // Search accumulation table first
    auto at_index = search_acc_table(page);
    if (at_index != acc_table.end()) {
        // Accumulation table hit - update pattern
        stats.at_hits++;
        (*at_index)->pattern[offset] = 1;
        update_age_acc_table(at_index);
    } else {
        // Search filter table
        auto ft_index = search_filter_table(page);
        if (ft_index != filter_table.end()) {
            // Filter table hit - move to accumulation table
            stats.ft_hits++;
            insert_acc_table(*ft_index, offset);
            evict_filter_table(ft_index);
        } else {
            // Filter table miss - new region access
            // Insert into filter table and generate prefetches from PHT
            insert_filter_table(eip, page, offset);
            generate_prefetch(eip, current_address, page, offset, pref_addr);
            buffer_prefetch(pref_addr);
        }
    }

    // Return buffered prefetches (rate-limited)
    return issue_prefetch();
}

// =============================================================================
// Filter Table Operations
// =============================================================================

std::deque<SMSPrefetcher::FTEntry*>::iterator SMSPrefetcher::search_filter_table(uint64_t page)
{
    return std::find_if(filter_table.begin(), filter_table.end(),
        [page](FTEntry* entry) { return entry->page == page; });
}

void SMSPrefetcher::insert_filter_table(uint64_t pc, uint64_t page, uint32_t offset)
{
    if (filter_table.size() >= m_ft_size) {
        auto victim = search_victim_filter_table();
        evict_filter_table(victim);
    }

    FTEntry* entry = new FTEntry();
    entry->page = page;
    entry->pc = pc;
    entry->trigger_offset = offset;
    filter_table.push_back(entry);
}

std::deque<SMSPrefetcher::FTEntry*>::iterator SMSPrefetcher::search_victim_filter_table()
{
    // FIFO replacement
    return filter_table.begin();
}

void SMSPrefetcher::evict_filter_table(std::deque<FTEntry*>::iterator victim)
{
    FTEntry* entry = *victim;
    filter_table.erase(victim);
    delete entry;
}

// =============================================================================
// Accumulation Table Operations
// =============================================================================

std::deque<SMSPrefetcher::ATEntry*>::iterator SMSPrefetcher::search_acc_table(uint64_t page)
{
    return std::find_if(acc_table.begin(), acc_table.end(),
        [page](ATEntry* entry) { return entry->page == page; });
}

void SMSPrefetcher::insert_acc_table(FTEntry* ftentry, uint32_t offset)
{
    if (acc_table.size() >= m_at_size) {
        auto victim = search_victim_acc_table();
        evict_acc_table(victim);
    }

    ATEntry* entry = new ATEntry();
    entry->pc = ftentry->pc;
    entry->page = ftentry->page;
    entry->trigger_offset = ftentry->trigger_offset;
    entry->pattern.reset();
    entry->pattern[ftentry->trigger_offset] = 1;
    entry->pattern[offset] = 1;
    entry->age = 0;

    // Age all existing entries
    for (auto* e : acc_table) {
        e->age++;
    }
    acc_table.push_back(entry);
}

std::deque<SMSPrefetcher::ATEntry*>::iterator SMSPrefetcher::search_victim_acc_table()
{
    // LRU replacement
    uint32_t max_age = 0;
    auto victim = acc_table.begin();
    
    for (auto it = acc_table.begin(); it != acc_table.end(); ++it) {
        if ((*it)->age >= max_age) {
            max_age = (*it)->age;
            victim = it;
        }
    }
    return victim;
}

void SMSPrefetcher::evict_acc_table(std::deque<ATEntry*>::iterator victim)
{
    ATEntry* entry = *victim;
    // Transfer pattern to PHT before eviction
    insert_pht_table(entry);
    acc_table.erase(victim);
    delete entry;
}

void SMSPrefetcher::update_age_acc_table(std::deque<ATEntry*>::iterator current)
{
    for (auto* entry : acc_table) {
        entry->age++;
    }
    (*current)->age = 0;
}

// =============================================================================
// Pattern History Table Operations
// =============================================================================

void SMSPrefetcher::insert_pht_table(ATEntry* atentry)
{
    uint64_t signature = create_signature(atentry->pc, atentry->trigger_offset);
    uint32_t set = 0;
    
    auto pht_index = search_pht(signature, set);
    if (pht_index != pht[set].end()) {
        // PHT hit - update pattern
        stats.pht_hits++;
        (*pht_index)->pattern = atentry->pattern;
        update_age_pht(set, pht_index);
    } else {
        // PHT miss - insert new entry
        if (pht[set].size() >= m_pht_assoc) {
            auto victim = search_victim_pht(set);
            evict_pht(set, victim);
        }

        PHTEntry* entry = new PHTEntry();
        entry->signature = signature;
        entry->pattern = atentry->pattern;
        entry->age = 0;
        
        for (auto* e : pht[set]) {
            e->age++;
        }
        pht[set].push_back(entry);
    }
}

std::deque<SMSPrefetcher::PHTEntry*>::iterator SMSPrefetcher::search_pht(uint64_t signature, uint32_t& set)
{
    set = static_cast<uint32_t>(signature % pht.size());
    return std::find_if(pht[set].begin(), pht[set].end(),
        [signature](PHTEntry* entry) { return entry->signature == signature; });
}

std::deque<SMSPrefetcher::PHTEntry*>::iterator SMSPrefetcher::search_victim_pht(int32_t set)
{
    uint32_t max_age = 0;
    auto victim = pht[set].begin();
    
    for (auto it = pht[set].begin(); it != pht[set].end(); ++it) {
        if ((*it)->age >= max_age) {
            max_age = (*it)->age;
            victim = it;
        }
    }
    return victim;
}

void SMSPrefetcher::evict_pht(int32_t set, std::deque<PHTEntry*>::iterator victim)
{
    PHTEntry* entry = *victim;
    pht[set].erase(victim);
    delete entry;
}

void SMSPrefetcher::update_age_pht(int32_t set, std::deque<PHTEntry*>::iterator current)
{
    for (auto* entry : pht[set]) {
        entry->age++;
    }
    (*current)->age = 0;
}

// =============================================================================
// Helper Functions
// =============================================================================

uint64_t SMSPrefetcher::create_signature(uint64_t pc, uint32_t offset)
{
    uint64_t signature = pc;
    signature = (signature << (m_region_size_log - LOG2_BLOCK_SIZE));
    signature += static_cast<uint64_t>(offset);
    return signature;
}

std::size_t SMSPrefetcher::generate_prefetch(uint64_t pc, uint64_t address, uint64_t page,
                                              uint32_t offset, std::vector<IntPtr>& pref_addr)
{
    uint64_t signature = create_signature(pc, offset);
    uint32_t set = 0;
    
    auto pht_index = search_pht(signature, set);
    if (pht_index == pht[set].end()) {
        return 0; // No pattern found
    }

    stats.pht_hits++;
    PHTEntry* entry = *pht_index;
    
    // Generate prefetches based on stored pattern
    for (uint32_t i = 0; i < BITMAP_MAX_SIZE; ++i) {
        if (entry->pattern[i] && i != offset) {
            IntPtr addr = (page << m_region_size_log) + (i << LOG2_BLOCK_SIZE);
            pref_addr.push_back(addr);
            stats.prefetches_generated++;
        }
    }
    
    update_age_pht(set, pht_index);
    return pref_addr.size();
}

void SMSPrefetcher::buffer_prefetch(const std::vector<IntPtr>& pref_addr)
{
    for (const auto& addr : pref_addr) {
        if (pref_buffer.size() >= m_pref_buffer_size) {
            break;
        }
        pref_buffer.push_back(addr);
    }
}

std::vector<IntPtr> SMSPrefetcher::issue_prefetch()
{
    std::vector<IntPtr> issued;
    uint32_t count = 0;
    
    while (!pref_buffer.empty() && count < m_pref_degree) {
        issued.push_back(pref_buffer.front());
        pref_buffer.pop_front();
        count++;
        stats.prefetches_issued++;
    }
    
    return issued;
}
