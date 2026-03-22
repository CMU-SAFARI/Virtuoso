/**
 * IPCP (Instruction Pointer Classifier Prefetcher) for Sniper
 * 
 * Ported from ChampSim's IPCP implementation.
 * Original paper: "Bouquet of Instruction Pointers: Instruction Pointer 
 * Classifier-based Spatial Hardware Prefetching" (ISCA'20)
 * 
 * IPCP classifies IPs into four categories and applies different 
 * prefetching strategies:
 * - S_TYPE (Stream): Sequential access pattern
 * - CS_TYPE (Constant Stride): Fixed stride pattern
 * - CPLX_TYPE (Complex Stride): Variable stride pattern
 * - NL_TYPE (Next Line): Default next-line prefetch
 */

#ifndef IPCP_PREFETCHER_H
#define IPCP_PREFETCHER_H

#include "prefetcher.h"
#include "fixed_types.h"
#include "core.h"
#include <vector>
#include <cstdint>

class IPCPPrefetcher : public Prefetcher
{
public:
    IPCPPrefetcher(String configName, core_id_t core_id);
    ~IPCPPrefetcher();

    std::vector<IntPtr> getNextAddress(IntPtr current_address, core_id_t core_id,
                                        Core::mem_op_t mem_op_type, bool cache_hit,
                                        bool prefetch_hit, IntPtr eip) override;

private:
    core_id_t m_core_id;

    // Configuration constants
    static constexpr unsigned LOG2_PAGE_SIZE = 12;
    static constexpr unsigned LOG2_BLOCK_SIZE = 6;
    static constexpr unsigned NUM_IP_TABLE_ENTRIES = 1024;
    static constexpr unsigned NUM_GHB_ENTRIES = 16;
    static constexpr unsigned NUM_IP_INDEX_BITS = 10;
    static constexpr unsigned NUM_IP_TAG_BITS = 6;
    static constexpr unsigned DPT_SIZE = 4096;

    // IP classification types
    static constexpr uint16_t S_TYPE = 1;     // Stream
    static constexpr uint16_t CS_TYPE = 2;    // Constant Stride
    static constexpr uint16_t CPLX_TYPE = 3;  // Complex Stride
    static constexpr uint16_t NL_TYPE = 4;    // Next Line

    // Prefetching parameters
    unsigned m_prefetch_degree;
    unsigned m_spec_nl_threshold;

    // IP Table Entry
    struct IPTableEntry {
        uint64_t ip_tag;
        uint64_t last_page;
        uint64_t last_cl_offset;
        int64_t last_stride;
        uint16_t ip_valid;
        int conf;              // CS confidence
        uint16_t signature;    // CPLX signature
        uint16_t str_dir;      // Stream direction
        uint16_t str_valid;    // Stream valid
        uint16_t str_strength; // Stream strength

        IPTableEntry() : ip_tag(0), last_page(0), last_cl_offset(0), 
                         last_stride(0), ip_valid(0), conf(0), signature(0),
                         str_dir(0), str_valid(0), str_strength(0) {}
    };

    // Delta Prediction Table Entry
    struct DPTEntry {
        int delta;
        int conf;

        DPTEntry() : delta(0), conf(0) {}
    };

    // Tables
    IPTableEntry ip_table[NUM_IP_TABLE_ENTRIES];
    DPTEntry dpt[DPT_SIZE];
    uint64_t ghb[NUM_GHB_ENTRIES];

    // Speculative next-line state
    uint64_t num_misses;
    uint64_t prev_cycle;
    float mpkc;
    int spec_nl;

    // Helper functions
    uint16_t update_signature(uint16_t old_sig, int delta);
    int update_conf(int stride, int pred_stride, int conf);
    void check_for_stream(int index, uint64_t cl_addr);
    void update_ghb(uint64_t cl_addr);

    // Statistics
    struct {
        UInt64 pref_called;
        UInt64 stream_prefetches;
        UInt64 cs_prefetches;
        UInt64 cplx_prefetches;
        UInt64 nl_prefetches;
        UInt64 prefetches_issued;
    } stats;
};

#endif /* IPCP_PREFETCHER_H */
