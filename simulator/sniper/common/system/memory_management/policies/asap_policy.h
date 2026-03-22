#pragma once
#include <fstream>
#include "simulator.h"
#include "mimicos.h"
#include "debug_config.h"

namespace Sniper {
    namespace ASAP {
        struct MetricsPolicy {
            //
            // === 1. Initialize (Sniper-specific logging setup and stats registration) ===
            //
            template <typename Alloc>
            static void on_init(const String &name,
                                UInt64 memory_size,
                                UInt64 kernel_size,
                                int max_order,
                                const String &frag_type,
                                Alloc *alloc) 
            {
                std::string fname = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + "asap_allocator.log";
                alloc->log_stream.open(fname);   // attach to allocator
                if (!alloc->log_stream.is_open()) {
                    throw std::runtime_error("[ASAP_POLICY] Failed to open log file");
                }

                std::cout << "[MimicOS] ASAP Allocator" << std::endl;
                alloc->log_stream << "[MimicOS] Creating ASAP Allocator" << std::endl;

                // Register stats metrics
                auto& stats = alloc->get_stats();
                registerStatsMetric(name, 0, "four_kb_allocated", &stats.four_kb_allocated);
                registerStatsMetric(name, 0, "two_mb_reserved", &stats.two_mb_reserved);
                registerStatsMetric(name, 0, "two_mb_promoted", &stats.two_mb_promoted);
                registerStatsMetric(name, 0, "two_mb_demoted", &stats.two_mb_demoted);
                registerStatsMetric(name, 0, "total_allocations", &stats.total_allocations);
                registerStatsMetric(name, 0, "kernel_pages_used", &stats.kernel_pages_used);
            }
        };
    }
};