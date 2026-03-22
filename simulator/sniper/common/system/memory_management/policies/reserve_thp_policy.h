#pragma once
#include "stats.h"
#include "simulator.h"
#include "config.hpp"
#include "sim_log.h"
#include <bitset>
#include <map>
#include <tuple>

#include "fixed_types.h"
#include "stats.h"

namespace Sniper { 
    namespace ReserveTHP {
        struct MetricsPolicy
        {
            mutable SimLog* m_log;

            MetricsPolicy() : m_log(nullptr) {}

            ~MetricsPolicy() {
                delete m_log;
            }

            template <typename Allocator>
            void on_init(const String& name, int memory_size, int kernel_size, int threshold_for_promotion, Allocator* phys_mem_alloc)
            {
                auto& stats = phys_mem_alloc->getStats();
                
                // Initialize SimLog for this component
                m_log = new SimLog("ReserveTHP", -1, DEBUG_RESERVATION_THP);

                std::cout << "[MimicOS] Reservation-based THP Allocator" << std::endl;
                std::cout << "[MimicOS] ReserveTHP: threshold_for_promotion = " << threshold_for_promotion << std::endl;

                if (m_log->isEnabled()) {
                    m_log->info("Creating Reservation-based THP Allocator");
                    m_log->info("Memory size:", memory_size, "Kernel size:", kernel_size);
                    m_log->info("Threshold for promotion:", threshold_for_promotion);
                }

                registerStatsMetric(name, 0, "four_kb_allocated", &stats.four_kb_allocated);
                registerStatsMetric(name, 0, "two_mb_reserved", &stats.two_mb_reserved);
                registerStatsMetric(name, 0, "two_mb_promoted", &stats.two_mb_promoted);
                registerStatsMetric(name, 0, "two_mb_demoted", &stats.two_mb_demoted);
                registerStatsMetric(name, 0, "total_allocations", &stats.total_allocations);
                registerStatsMetric(name, 0, "page_table_pages_used", &stats.kernel_pages_used);
            }

            /* Logging - delegates to SimLog */
            template<typename... Args>
            void log(const Args&... args) const { 
                if (m_log) m_log->debug(args...);
            }

            template<typename... Args>
            void log_trace(const Args&... args) const { 
                if (m_log) m_log->trace(args...);
            }
        };
    }
}