#pragma once
#include <fstream>
#include <string>
#include "stats.h"
#include "simulator.h"
#include "config.hpp"

namespace Sniper {
namespace HugeTLBfs {
    struct MetricsPolicy {
        ~MetricsPolicy() {}

        template <typename T>
        void on_init(T* htlbfs) {
            std::string fname = std::string(Sim()->getConfig()->getOutputDirectory().c_str())
                                + "/hugetlbfs.log";
            htlbfs->log_stream.open(fname);

            auto& s = htlbfs->getStats();
            registerStatsMetric("hugetlbfs", 0, "allocations_2mb", &s.allocations_2mb);
            registerStatsMetric("hugetlbfs", 0, "allocations_1gb", &s.allocations_1gb);
            registerStatsMetric("hugetlbfs", 0, "deallocations_2mb", &s.deallocations_2mb);
            registerStatsMetric("hugetlbfs", 0, "deallocations_1gb", &s.deallocations_1gb);
            registerStatsMetric("hugetlbfs", 0, "allocation_failures", &s.allocation_failures);
            registerStatsMetric("hugetlbfs", 0, "pool_exhausted_events", &s.pool_exhausted_events);
        }
    };
} // namespace HugeTLBfs
} // namespace Sniper
