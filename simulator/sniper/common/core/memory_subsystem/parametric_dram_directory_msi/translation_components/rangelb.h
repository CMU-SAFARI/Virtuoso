#pragma once
#include <vector>
#include "fixed_types.h"
#include "core.h"
#include "vma.h"


class RLB
{
public:
        ComponentLatency m_latency;
        const UInt64 m_num_sets;
        int mru;
        String repl_policy;
        Core *m_core;
        String m_name;
        std::vector<Range> m_ranges;


        std::ofstream log_file;
        std::string log_file_name;
        
        struct Stats
        {
                UInt64 accesses;
                UInt64 misses;
                UInt64 hits;
        } stats;

        RLB(Core *core, String name, ComponentLatency latency, UInt64 entries);
        ~RLB();
        void insert_entry(Range new_rng);
        std::pair<bool, Range> access(Core::mem_op_t mem_op_type, IntPtr vpn, bool count);
        ComponentLatency get_latency() { return m_latency; };
};
