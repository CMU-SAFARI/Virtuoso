#pragma once
#include <fstream>
#include <string>
#include "stats.h"
#include "simulator.h"
#include "config.hpp"

namespace Sniper {
    namespace Baseline {
        struct MetricsPolicy
        {
            mutable std::ofstream log_file;

            ~MetricsPolicy()            { if (log_file.is_open()) log_file.close(); }

            template <typename Alloc>
            void on_init(const String&  name,
                         int            /*mem*/,
                         int            /*kernel*/,
                         Alloc*         alloc)
            {
                /* open log file in the Sniper output directory */
                std::string fname = std::string(Sim()->getConfig()->getOutputDirectory().c_str())
                                    + "/baseline_allocator.log";
                log_file.open(fname, std::ios::out | std::ios::app);
                if (!log_file.is_open())
                    throw std::runtime_error("[BaselineAllocatorPolicy] cannot open log file");

                std::cout << "[MimicOS] Baseline Allocator" << std::endl;
                log_file << "[Sniper] BaselineAllocator \"" << name << "\" created\n";

                /* register stats with Sniper’s Stats subsystem */
                auto& s = alloc->getStats();
                registerStatsMetric(name, 0, "total_allocations", &s.total_allocations);
            }

            void log(const std::string& msg) const
            { log_file << msg << '\n'; }
        };
    } // namespace Baseline
} // namespace Sniper

