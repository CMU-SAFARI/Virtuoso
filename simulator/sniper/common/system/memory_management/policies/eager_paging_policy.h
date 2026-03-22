#pragma once
#include <fstream>
#include <string>
#include "stats.h"
#include "simulator.h"
#include "config.hpp"

namespace Sniper {
    namespace EagerPaging {
        struct MetricsPolicy
        {
            mutable std::ofstream log_file;

            ~MetricsPolicy() { if (log_file.is_open()) log_file.close(); }

            template <typename Alloc>
            void on_init(const String& name,
                         int           /*mem*/,
                         int           /*kernel*/,
                         Alloc*        alloc)
            {
                std::string fname = std::string(Sim()->getConfig()->getOutputDirectory().c_str())
                                    + "/eager_paging.log";
                log_file.open(fname, std::ios::out | std::ios::app);

                std::cout << "[MimicOS] Creating Eager Paging Allocator" << std::endl;
                if (log_file.is_open())
                    log_file << "[Sniper] EagerPagingAllocator \"" << name << "\" created\n";

                auto& s = alloc->getStats();
                registerStatsMetric(name, 0, "physical_ranges_per_vma",        &s.physical_ranges_per_vma);
                registerStatsMetric(name, 0, "deviation_of_physical_ranges",   &s.deviation_of_physical_ranges);
                registerStatsMetric(name, 0, "total_allocated_vmas",           &s.total_allocated_vmas);
                registerStatsMetric(name, 0, "total_allocated_physical_ranges", &s.total_allocated_physical_ranges);
            }

            void log(const std::string& msg) const
            { if (log_file.is_open()) log_file << msg << '\n'; }
        };
    } // namespace EagerPaging
} // namespace Sniper
