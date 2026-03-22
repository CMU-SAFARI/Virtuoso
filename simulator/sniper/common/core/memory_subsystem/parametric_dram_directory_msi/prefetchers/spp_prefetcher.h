/**
 * SPP (Signature Path Prefetcher) for Sniper
 * 
 * Ported from ChampSim's spp_dev implementation.
 * Original paper: "Practical Data Value Speculation for Future High-end Processors"
 * 
 * SPP uses signature-based pattern matching to predict prefetch candidates.
 * It maintains:
 * - Signature Table (ST): Tracks page signatures based on delta patterns
 * - Pattern Table (PT): Stores delta patterns associated with signatures
 * - Prefetch Filter (FILTER): Avoids redundant prefetches
 * - Global History Register (GHR): Enables cross-page prefetching
 */

#ifndef SPP_PREFETCHER_H
#define SPP_PREFETCHER_H

#include "prefetcher.h"
#include "fixed_types.h"
#include "core.h"
#include <vector>
#include <cstdint>

class SPPPrefetcher : public Prefetcher
{
public:
    SPPPrefetcher(String configName, core_id_t core_id);
    ~SPPPrefetcher();

    std::vector<IntPtr> getNextAddress(IntPtr current_address, core_id_t core_id, 
                                        Core::mem_op_t mem_op_type, bool cache_hit, 
                                        bool prefetch_hit, IntPtr eip) override;

private:
    core_id_t m_core_id;

    // SPP functional knobs (configurable)
    bool m_lookahead_on;
    bool m_filter_on;
    bool m_ghr_on;

    // Signature table parameters
    static constexpr std::size_t ST_SET = 1;
    static constexpr std::size_t ST_WAY = 256;
    static constexpr unsigned ST_TAG_BIT = 16;
    static constexpr unsigned SIG_SHIFT = 3;
    static constexpr unsigned SIG_BIT = 12;
    static constexpr uint32_t SIG_MASK = ((1 << SIG_BIT) - 1);
    static constexpr unsigned SIG_DELTA_BIT = 7;

    // Pattern table parameters
    static constexpr std::size_t PT_SET = 512;
    static constexpr std::size_t PT_WAY = 4;
    static constexpr unsigned C_SIG_BIT = 4;
    static constexpr unsigned C_DELTA_BIT = 4;
    static constexpr uint32_t C_SIG_MAX = ((1 << C_SIG_BIT) - 1);
    static constexpr uint32_t C_DELTA_MAX = ((1 << C_DELTA_BIT) - 1);

    // Prefetch filter parameters
    static constexpr unsigned QUOTIENT_BIT = 10;
    static constexpr unsigned REMAINDER_BIT = 6;
    static constexpr std::size_t FILTER_SET = (1 << QUOTIENT_BIT);
    static constexpr uint32_t FILL_THRESHOLD = 90;
    static constexpr uint32_t PF_THRESHOLD = 25;

    // Global register parameters
    static constexpr unsigned GLOBAL_COUNTER_BIT = 10;
    static constexpr uint32_t GLOBAL_COUNTER_MAX = ((1 << GLOBAL_COUNTER_BIT) - 1);
    static constexpr std::size_t MAX_GHR_ENTRY = 8;

    // Page/Block constants
    static constexpr IntPtr PAGE_SIZE = 4096;
    static constexpr IntPtr BLOCK_SIZE = 64;
    static constexpr unsigned LOG2_PAGE_SIZE = 12;
    static constexpr unsigned LOG2_BLOCK_SIZE = 6;
    static constexpr unsigned BLOCKS_PER_PAGE = PAGE_SIZE / BLOCK_SIZE;

    // Request types for prefetch filter
    enum FilterRequest { SPP_L2C_PREFETCH, SPP_LLC_PREFETCH, L2C_DEMAND, L2C_EVICT };

    // Signature Table
    struct SignatureTable {
        bool valid[ST_SET][ST_WAY];
        uint64_t tag[ST_SET][ST_WAY];
        uint32_t last_offset[ST_SET][ST_WAY];
        uint32_t sig[ST_SET][ST_WAY];
        uint32_t lru[ST_SET][ST_WAY];

        SignatureTable();
        void read_and_update_sig(IntPtr addr, uint32_t& last_sig, uint32_t& curr_sig, int32_t& delta);
    } ST;

    // Pattern Table
    struct PatternTable {
        int32_t delta[PT_SET][PT_WAY];
        uint32_t c_delta[PT_SET][PT_WAY];
        uint32_t c_sig[PT_SET];

        PatternTable();
        void update_pattern(uint32_t last_sig, int32_t curr_delta);
        void read_pattern(uint32_t curr_sig, std::vector<int32_t>& prefetch_delta, 
                         std::vector<uint32_t>& confidence_q, uint32_t& lookahead_way, 
                         uint32_t& lookahead_conf, uint32_t& pf_q_tail, uint32_t& depth);
    } PT;

    // Prefetch Filter
    struct PrefetchFilter {
        uint64_t remainder_tag[FILTER_SET];
        bool valid[FILTER_SET];
        bool useful[FILTER_SET];

        PrefetchFilter();
        bool check(IntPtr pf_addr, FilterRequest filter_request, uint32_t& pf_useful, uint32_t& pf_issued);
    } FILTER;

    // Global History Register
    struct GlobalRegister {
        uint32_t pf_useful;
        uint32_t pf_issued;
        uint32_t global_accuracy;

        uint8_t valid[MAX_GHR_ENTRY];
        uint32_t sig[MAX_GHR_ENTRY];
        uint32_t confidence[MAX_GHR_ENTRY];
        uint32_t offset[MAX_GHR_ENTRY];
        int32_t delta[MAX_GHR_ENTRY];

        GlobalRegister();
        void update_entry(uint32_t pf_sig, uint32_t pf_confidence, uint32_t pf_offset, int32_t pf_delta);
        uint32_t check_entry(uint32_t page_offset);
    } GHR;

    // Helper functions
    static uint64_t get_hash(uint64_t key);

    // Statistics
    struct {
        UInt64 pref_called;
        UInt64 prefetches_issued;
        UInt64 st_hits;
        UInt64 st_misses;
        UInt64 filter_hits;
    } stats;
};

#endif /* SPP_PREFETCHER_H */
