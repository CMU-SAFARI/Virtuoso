/**
 * SMS (Spatial Memory Streaming) Prefetcher for Sniper
 * 
 * Ported from ChampSim's SMS implementation.
 * Original paper: "Spatial Memory Streaming" (ISCA'06)
 * 
 * SMS learns spatial access patterns within memory regions and
 * replays those patterns when the same trigger access is seen again.
 * 
 * Key structures:
 * - Filter Table (FT): Detects first access to a region
 * - Accumulation Table (AT): Records access patterns within active regions
 * - Pattern History Table (PHT): Stores learned patterns indexed by (PC, offset)
 */

#ifndef SMS_PREFETCHER_H
#define SMS_PREFETCHER_H

#include "prefetcher.h"
#include "fixed_types.h"
#include "core.h"
#include <vector>
#include <deque>
#include <bitset>
#include <cstdint>

// Bitmap for tracking accessed blocks within a region
static constexpr uint32_t BITMAP_MAX_SIZE = 64;
using Bitmap = std::bitset<BITMAP_MAX_SIZE>;

class SMSPrefetcher : public Prefetcher
{
public:
    SMSPrefetcher(String configName, core_id_t core_id);
    ~SMSPrefetcher();

    std::vector<IntPtr> getNextAddress(IntPtr current_address, core_id_t core_id,
                                        Core::mem_op_t mem_op_type, bool cache_hit,
                                        bool prefetch_hit, IntPtr eip) override;

private:
    core_id_t m_core_id;

    // Configuration parameters
    uint32_t m_at_size;         // Accumulation Table size
    uint32_t m_ft_size;         // Filter Table size
    uint32_t m_pht_size;        // Pattern History Table size
    uint32_t m_pht_assoc;       // PHT associativity
    uint32_t m_pref_degree;     // Max prefetches per access
    uint32_t m_region_size;     // Region size in bytes
    uint32_t m_region_size_log; // log2(region_size)
    uint32_t m_pref_buffer_size;

    // Constants
    static constexpr IntPtr PAGE_SIZE = 4096;
    static constexpr IntPtr BLOCK_SIZE = 64;
    static constexpr unsigned LOG2_BLOCK_SIZE = 6;

    // Filter Table Entry
    struct FTEntry {
        uint64_t page;
        uint64_t pc;
        uint32_t trigger_offset;

        FTEntry() : page(0), pc(0), trigger_offset(0) {}
    };

    // Accumulation Table Entry
    struct ATEntry {
        uint64_t page;
        uint64_t pc;
        uint32_t trigger_offset;
        Bitmap pattern;
        uint32_t age;

        ATEntry() : page(0), pc(0), trigger_offset(0), pattern(0), age(0) {}
    };

    // Pattern History Table Entry
    struct PHTEntry {
        uint64_t signature;
        Bitmap pattern;
        uint32_t age;

        PHTEntry() : signature(0), pattern(0), age(0) {}
    };

    // Data structures
    std::deque<FTEntry*> filter_table;
    std::deque<ATEntry*> acc_table;
    std::vector<std::deque<PHTEntry*>> pht;
    std::deque<IntPtr> pref_buffer;

    // Filter Table operations
    std::deque<FTEntry*>::iterator search_filter_table(uint64_t page);
    void insert_filter_table(uint64_t pc, uint64_t page, uint32_t offset);
    std::deque<FTEntry*>::iterator search_victim_filter_table();
    void evict_filter_table(std::deque<FTEntry*>::iterator victim);

    // Accumulation Table operations
    std::deque<ATEntry*>::iterator search_acc_table(uint64_t page);
    void insert_acc_table(FTEntry* ftentry, uint32_t offset);
    std::deque<ATEntry*>::iterator search_victim_acc_table();
    void evict_acc_table(std::deque<ATEntry*>::iterator victim);
    void update_age_acc_table(std::deque<ATEntry*>::iterator current);

    // Pattern History Table operations
    void insert_pht_table(ATEntry* atentry);
    std::deque<PHTEntry*>::iterator search_pht(uint64_t signature, uint32_t& set);
    std::deque<PHTEntry*>::iterator search_victim_pht(int32_t set);
    void evict_pht(int32_t set, std::deque<PHTEntry*>::iterator victim);
    void update_age_pht(int32_t set, std::deque<PHTEntry*>::iterator current);

    // Helper functions
    uint64_t create_signature(uint64_t pc, uint32_t offset);
    std::size_t generate_prefetch(uint64_t pc, uint64_t address, uint64_t page, 
                                  uint32_t offset, std::vector<IntPtr>& pref_addr);
    void buffer_prefetch(const std::vector<IntPtr>& pref_addr);
    std::vector<IntPtr> issue_prefetch();

    // Statistics
    struct {
        UInt64 pref_called;
        UInt64 ft_hits;
        UInt64 at_hits;
        UInt64 pht_hits;
        UInt64 prefetches_generated;
        UInt64 prefetches_issued;
    } stats;
};

#endif /* SMS_PREFETCHER_H */
