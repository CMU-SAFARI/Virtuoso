#pragma once
#include <fstream>
#include <string>
#include "stats.h"
#include "simulator.h"
#include "config.hpp"

namespace Sniper {
namespace SwapCache {
    struct MetricsPolicy {
        ~MetricsPolicy() {}

        template <typename T>
        void on_init(T* sc) {
            std::string fname = std::string(Sim()->getConfig()->getOutputDirectory().c_str())
                                + "/swap.log";
            sc->log_stream.open(fname);

            auto& s = sc->getStats();
            registerStatsMetric("swap_space", 0, "swap_ins", &s.swap_ins);
            registerStatsMetric("swap_space", 0, "swap_outs", &s.swap_outs);
            registerStatsMetric("swap_space", 0, "failed_swap_outs", &s.failed_swap_outs_space);
        }
    };
} // namespace SwapCache
} // namespace Sniper
