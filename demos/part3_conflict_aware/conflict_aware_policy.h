/**
 * @file conflict_aware_policy.h
 * @brief Sniper-side policy for the Conflict-Aware allocator.
 *
 * ASPLOS 2026 Workshop: Hardware/OS Co-Design for Memory Management
 *
 * This policy provides the Sniper-specific hooks (stats registration,
 * logging, VMA lookup) that the ConflictAwareAllocator template needs.
 * It follows the same pattern as ReserveTHP::MetricsPolicy and
 * Spot::SpotSniperPolicy.
 */

#pragma once

#include "stats.h"
#include "simulator.h"
#include "config.hpp"
#include "sim_log.h"
#include "fixed_types.h"

#include <iostream>

namespace Sniper {
namespace ConflictAware {

    struct MetricsPolicy
    {
        mutable SimLog* m_log;

        MetricsPolicy() : m_log(nullptr) {}

        ~MetricsPolicy() {
            delete m_log;
        }

        /**
         * @brief Initialise the allocator: register stats and set up logging.
         *
         * Called from ConflictAwareAllocator constructor via Policy::on_init().
         */
        template <typename Allocator>
        void on_init(const String& name,
                     int memory_size,
                     int kernel_size,
                     double threshold_for_promotion,
                     Allocator* alloc)
        {
            auto& stats = alloc->getStats();

            m_log = new SimLog("ConflictAware", -1, DEBUG_RESERVATION_THP);

            std::cout << "[MimicOS] Conflict-Aware Allocator (Sniper Policy)" << std::endl;
            std::cout << "[MimicOS] ConflictAware: memory_size=" << memory_size
                      << " kernel_size=" << kernel_size << std::endl;

            if (m_log->isEnabled()) {
                m_log->info("Creating Conflict-Aware Allocator");
                m_log->info("Memory size:", memory_size, "Kernel size:", kernel_size);
            }

            // Register Sniper statistics for this allocator
            registerStatsMetric(name, 0, "four_kb_allocated",    &stats.four_kb_allocated);
            registerStatsMetric(name, 0, "two_mb_reserved",      &stats.two_mb_reserved);
            registerStatsMetric(name, 0, "two_mb_promoted",      &stats.two_mb_promoted);
            registerStatsMetric(name, 0, "two_mb_demoted",       &stats.two_mb_demoted);
            registerStatsMetric(name, 0, "total_allocations",    &stats.total_allocations);
            registerStatsMetric(name, 0, "page_table_pages_used", &stats.kernel_pages_used);

            // Conflict-specific stats
            registerStatsMetric(name, 0, "pt_allocs_total",    &stats.pt_allocs_total);
            registerStatsMetric(name, 0, "pt_allocs_conflict", &stats.pt_allocs_conflict);
            registerStatsMetric(name, 0, "pt_allocs_avoided",  &stats.pt_allocs_avoided);
            registerStatsMetric(name, 0, "pt_allocs_fallback", &stats.pt_allocs_fallback);
        }

        /* Logging delegates */
        template<typename... Args>
        void log(const Args&... args) const {
            if (m_log) m_log->debug(args...);
        }

        template<typename... Args>
        void log_trace(const Args&... args) const {
            if (m_log) m_log->trace(args...);
        }
    };

} // namespace ConflictAware
} // namespace Sniper
