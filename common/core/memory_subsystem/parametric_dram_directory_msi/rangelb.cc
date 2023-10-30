
#include "cache_base.h"
#include <vector>
#include "stats.h"
#include "rangelb.h"
#include "allocation_manager.h"
#include "simulator.h"
#include "config.hpp"
#include <cmath>
#include <iostream>
#include <utility>
#include "core_manager.h"
#include "cache_set.h"

RLB::RLB(Core *core, String name, ComponentLatency latency, UInt64 entries)
    : m_core(core),
      m_name(name),
      m_latency(latency),
      m_num_sets(entries)
{
    bzero(&stats, sizeof(stats));
    registerStatsMetric(name, core->getId(), "accesses", &stats.accesses);
    registerStatsMetric(name, core->getId(), "misses", &stats.misses);
    registerStatsMetric(name, core->getId(), "hits", &stats.hits);
}

void RLB::insert_entry(Range new_rng)
{

    int candidate;

    candidate = rand() % m_num_sets;

    while (candidate == mru)
        candidate = rand() % m_num_sets;

    if (m_ranges.size() == m_num_sets)
        m_ranges.erase(m_ranges.begin() + candidate - 1);

    m_ranges.push_back(new_rng);
}

std::pair<bool, Range> RLB::access(Core::mem_op_t mem_op_type, IntPtr address, bool count)
{

    int index = 0;
    if (count)
        stats.accesses++;

    for (std::vector<Range>::iterator it = m_ranges.begin(); it != m_ranges.end(); ++it)
    {

        if (address >= (*it).vpn && address <= (*it).bounds)
        {
            if (count)
                stats.hits++;
            mru = index;
            return std::make_pair(true, (*it));
        }

        index++;
    }

    if (count)
        stats.misses++;
    Range miss;
    return std::make_pair(false, miss);
}
