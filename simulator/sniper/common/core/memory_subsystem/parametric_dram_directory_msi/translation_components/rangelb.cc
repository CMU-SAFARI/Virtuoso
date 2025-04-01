
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
#include "vma.h"

#define DEBUG_RLB

RLB::RLB(Core *core, String name, ComponentLatency latency, UInt64 entries)
    : m_latency(latency),
      m_num_sets(entries),
      m_core(core),
      m_name(name)
{

    log_file_name = "rlb.log";
    log_file_name = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + log_file_name;
    log_file.open(log_file_name);


    bzero(&stats, sizeof(stats));
    registerStatsMetric(name, core->getId(), "accesses", &stats.accesses);
    registerStatsMetric(name, core->getId(), "misses", &stats.misses);
    registerStatsMetric(name, core->getId(), "hits", &stats.hits);
}

RLB::~RLB()
{
}

void RLB::insert_entry(Range new_rng)
{
#ifdef DEBUG_RLB
    log_file << "Inserting entry in RLB: " << new_rng.vpn << " " << new_rng.bounds << std::endl;
#endif

    int candidate;

    candidate = rand() % m_num_sets;

    while (candidate == mru)
        candidate = rand() % m_num_sets;

    if (m_ranges.size() == m_num_sets)
        m_ranges.erase(m_ranges.begin() + candidate);

    m_ranges.push_back(new_rng);
}

std::pair<bool, Range> RLB::access(Core::mem_op_t mem_op_type, IntPtr address, bool count)
{

    int index = 0;
    if (count)
        stats.accesses++;
#ifdef DEBUG_RLB
    log_file << "Accessing address in RLB: " << address << std::endl;
#endif

    for (std::vector<Range>::iterator it = m_ranges.begin(); it != m_ranges.end(); ++it)
    {

#ifdef DEBUG_RLB
       log_file << "VPN: " << (*it).vpn << " Bounds: " << (*it).bounds << std::endl;
#endif

        if (address >= (*it).vpn && address < (*it).bounds)
        {
            if (count)
                stats.hits++;
            mru = index;
#ifdef DEBUG_RLB
            log_file << "Hit in RLB" << std::endl;
#endif
            return std::make_pair(true, (*it));
        }

        index++;
    }

#ifdef DEBUG_RLB
    log_file << "Miss in RLB" << std::endl;
#endif

    if (count)
        stats.misses++;
    Range miss;
    return std::make_pair(false, miss);
}
