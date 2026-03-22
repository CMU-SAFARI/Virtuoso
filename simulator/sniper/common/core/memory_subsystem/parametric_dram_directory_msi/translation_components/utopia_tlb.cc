#include "utopia_tlb.h"
#include "stats.h"
#include "simulator.h"
#include "config.hpp"
#include "debug_config.h"
#include <cmath>
#include <iostream>
#include <algorithm>

namespace ParametricDramDirectoryMSI
{

UtopiaTLB::UtopiaTLB(String name, core_id_t core_id, UInt32 num_entries,
                     UInt32 associativity, SubsecondTime access_latency)
    : m_name(name)
    , m_core_id(core_id)
    , m_num_entries(num_entries)
    , m_associativity(associativity)
    , m_num_sets(num_entries / associativity)
    , m_access_latency(access_latency)
{
    // Calculate set index bits
    m_set_index_bits = 0;
    UInt32 temp = m_num_sets;
    while (temp > 1) {
        temp >>= 1;
        m_set_index_bits++;
    }
    
    // Initialize the sets
    m_sets.resize(m_num_sets);
    m_lru_stack.resize(m_num_sets);
    
    for (UInt32 i = 0; i < m_num_sets; i++) {
        m_sets[i].resize(m_associativity);
        m_lru_stack[i].resize(m_associativity);
        // Initialize LRU stack: way 0 is MRU, way N-1 is LRU
        for (UInt32 j = 0; j < m_associativity; j++) {
            m_lru_stack[i][j] = j;
        }
    }
    
    // Initialize stats
    m_stats.accesses = 0;
    m_stats.hits = 0;
    m_stats.misses = 0;
    m_stats.evictions = 0;
    m_stats.allocations = 0;
    
    // Register stats
    registerStatsMetric(name, core_id, "accesses", &m_stats.accesses);
    registerStatsMetric(name, core_id, "hits", &m_stats.hits);
    registerStatsMetric(name, core_id, "misses", &m_stats.misses);
    registerStatsMetric(name, core_id, "evictions", &m_stats.evictions);
    registerStatsMetric(name, core_id, "allocations", &m_stats.allocations);
    
    // Initialize logging
    m_log = new SimLog(name.c_str(), core_id, DEBUG_TLB);
    
    // Print initialization info
    std::cout << "[UTLB] Initializing Utopia TLB: " << m_name << std::endl;
    std::cout << "[UTLB]   Core ID: " << m_core_id << std::endl;
    std::cout << "[UTLB]   Entries: " << m_num_entries << std::endl;
    std::cout << "[UTLB]   Associativity: " << m_associativity << std::endl;
    std::cout << "[UTLB]   Sets: " << m_num_sets << std::endl;
    std::cout << "[UTLB]   Set index bits: " << m_set_index_bits << std::endl;
    std::cout << "[UTLB]   Bits per entry: " << getBitsPerEntry() << std::endl;
    std::cout << "[UTLB]   Capacity ratio vs standard TLB: " << getCapacityRatio() << "x" << std::endl;
    std::cout << "[UTLB]   Access latency: " << m_access_latency.getNS() << " ns" << std::endl;
}

UtopiaTLB::~UtopiaTLB()
{
    delete m_log;
    
    std::cout << "[UTLB] " << m_name << " final stats:" << std::endl;
    std::cout << "[UTLB]   Accesses: " << m_stats.accesses << std::endl;
    std::cout << "[UTLB]   Hits: " << m_stats.hits << std::endl;
    std::cout << "[UTLB]   Misses: " << m_stats.misses << std::endl;
    std::cout << "[UTLB]   Hit rate: " << (m_stats.accesses > 0 ? 
        (100.0 * m_stats.hits / m_stats.accesses) : 0.0) << "%" << std::endl;
}

UInt64 UtopiaTLB::getSetIndex(IntPtr address, int page_size_bits) const
{
    // VPN = address >> page_size_bits
    // Set index = VPN & ((1 << set_index_bits) - 1)
    IntPtr vpn = address >> page_size_bits;
    return vpn & ((1ULL << m_set_index_bits) - 1);
}

UInt64 UtopiaTLB::getVPNTag(IntPtr address, int page_size_bits) const
{
    // VPN = address >> page_size_bits
    // Tag = VPN >> set_index_bits
    IntPtr vpn = address >> page_size_bits;
    return vpn >> m_set_index_bits;
}

void UtopiaTLB::updateLRU(UInt32 set_idx, UInt32 way_idx)
{
    // Move the accessed way to MRU position (front of stack)
    auto& stack = m_lru_stack[set_idx];
    
    // Find and remove the way from its current position
    auto it = std::find(stack.begin(), stack.end(), static_cast<uint8_t>(way_idx));
    if (it != stack.end()) {
        stack.erase(it);
    }
    
    // Insert at front (MRU position)
    stack.insert(stack.begin(), static_cast<uint8_t>(way_idx));
}

UInt32 UtopiaTLB::getLRUVictim(UInt32 set_idx) const
{
    // First, look for an invalid entry
    for (UInt32 i = 0; i < m_associativity; i++) {
        if (!m_sets[set_idx][i].valid) {
            return i;
        }
    }
    
    // All entries valid, return LRU (last in stack)
    return m_lru_stack[set_idx].back();
}

UTLBLookupResult UtopiaTLB::lookup(IntPtr address, int page_size_bits, bool count)
{
    if (count) {
        m_stats.accesses++;
    }
    
    UInt64 set_idx = getSetIndex(address, page_size_bits);
    UInt64 vpn_tag = getVPNTag(address, page_size_bits);
    uint8_t page_size_bit = (page_size_bits == 21) ? 1 : 0;
    
    m_log->debug("UTLB lookup: addr=", SimLog::hex(address), 
                 " set=", set_idx, " tag=", SimLog::hex(vpn_tag));
    
    // Search all ways in the set
    for (UInt32 way = 0; way < m_associativity; way++) {
        const UTLBEntry& entry = m_sets[set_idx][way];
        
        if (entry.valid && 
            entry.vpn_tag == vpn_tag && 
            entry.page_size_bit == page_size_bit) {
            
            // Hit!
            if (count) {
                m_stats.hits++;
            }
            
            // Update LRU
            updateLRU(set_idx, way);
            
            m_log->debug("UTLB HIT: seg_id=", (int)entry.seg_id, 
                        " way_idx=", (int)entry.way_idx);
            
            return UTLBLookupResult(true, entry.seg_id, entry.way_idx, 
                                    entry.getPageSizeBits());
        }
    }
    
    // Miss
    if (count) {
        m_stats.misses++;
    }
    
    m_log->debug("UTLB MISS");
    
    return UTLBLookupResult();
}

bool UtopiaTLB::allocate(IntPtr address, int page_size_bits, uint8_t seg_id,
                         uint8_t way_idx, bool count)
{
    UInt64 set_idx = getSetIndex(address, page_size_bits);
    UInt64 vpn_tag = getVPNTag(address, page_size_bits);
    uint8_t page_size_bit = (page_size_bits == 21) ? 1 : 0;
    
    m_log->debug("UTLB allocate: addr=", SimLog::hex(address),
                 " set=", set_idx, " tag=", SimLog::hex(vpn_tag),
                 " seg_id=", (int)seg_id, " way_idx=", (int)way_idx);
    
    // Check if already present (update in place)
    for (UInt32 way = 0; way < m_associativity; way++) {
        UTLBEntry& entry = m_sets[set_idx][way];
        
        if (entry.valid && 
            entry.vpn_tag == vpn_tag && 
            entry.page_size_bit == page_size_bit) {
            
            // Update existing entry
            entry.seg_id = seg_id;
            entry.way_idx = way_idx;
            updateLRU(set_idx, way);
            
            m_log->debug("UTLB update existing: way=", way);
            return false;  // No eviction
        }
    }
    
    // Need to allocate new entry
    if (count) {
        m_stats.allocations++;
    }
    
    // Find victim way
    UInt32 victim_way = getLRUVictim(set_idx);
    bool eviction = m_sets[set_idx][victim_way].valid;
    
    if (eviction && count) {
        m_stats.evictions++;
    }
    
    // Allocate
    UTLBEntry& entry = m_sets[set_idx][victim_way];
    entry.vpn_tag = vpn_tag;
    entry.seg_id = seg_id;
    entry.way_idx = way_idx;
    entry.page_size_bit = page_size_bit;
    entry.valid = true;
    
    // Update LRU
    updateLRU(set_idx, victim_way);
    
    m_log->debug("UTLB allocated: victim_way=", victim_way, " evicted=", eviction);
    
    return eviction;
}

bool UtopiaTLB::invalidate(IntPtr address, int page_size_bits)
{
    UInt64 set_idx = getSetIndex(address, page_size_bits);
    UInt64 vpn_tag = getVPNTag(address, page_size_bits);
    uint8_t page_size_bit = (page_size_bits == 21) ? 1 : 0;
    
    for (UInt32 way = 0; way < m_associativity; way++) {
        UTLBEntry& entry = m_sets[set_idx][way];
        
        if (entry.valid && 
            entry.vpn_tag == vpn_tag && 
            entry.page_size_bit == page_size_bit) {
            
            entry.valid = false;
            m_log->debug("UTLB invalidate: addr=", SimLog::hex(address), " way=", way);
            return true;
        }
    }
    
    return false;
}

void UtopiaTLB::flush()
{
    m_log->debug("UTLB flush");
    
    for (UInt32 set = 0; set < m_num_sets; set++) {
        for (UInt32 way = 0; way < m_associativity; way++) {
            m_sets[set][way].valid = false;
        }
        // Reset LRU
        for (UInt32 j = 0; j < m_associativity; j++) {
            m_lru_stack[set][j] = j;
        }
    }
}

} // namespace ParametricDramDirectoryMSI
