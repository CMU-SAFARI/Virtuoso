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
    namespace NumaReserveTHP {
        struct NumaReserveTHPSniperPolicy
        {
            mutable SimLog* m_log;

            NumaReserveTHPSniperPolicy() : m_log(nullptr) {}

            ~NumaReserveTHPSniperPolicy() {
                delete m_log;
            }

            template <typename Allocator>
            void on_init(const String& name, int memory_size, int kernel_size, 
                         int threshold_for_promotion, Allocator* phys_mem_alloc)
            {
                auto& stats = phys_mem_alloc->getStats();
                
                m_log = new SimLog("NumaReserveTHP", -1, DEBUG_RESERVATION_THP);

                std::cout << "[MimicOS] NUMA Reservation-based THP Allocator" << std::endl;
                std::cout << "[MimicOS] NumaReserveTHP: threshold_for_promotion = " 
                          << threshold_for_promotion << std::endl;

                if (m_log->isEnabled()) {
                    m_log->info("Creating NUMA Reservation-based THP Allocator");
                    m_log->info("Memory size:", memory_size, "Kernel size:", kernel_size);
                    m_log->info("Threshold for promotion:", threshold_for_promotion);
                }

                registerStatsMetric(name, 0, "four_kb_allocated", &stats.four_kb_allocated);
                registerStatsMetric(name, 0, "two_mb_reserved", &stats.two_mb_reserved);
                registerStatsMetric(name, 0, "two_mb_promoted", &stats.two_mb_promoted);
                registerStatsMetric(name, 0, "two_mb_demoted", &stats.two_mb_demoted);
                registerStatsMetric(name, 0, "total_allocations", &stats.total_allocations);
                registerStatsMetric(name, 0, "page_table_pages_used", &stats.kernel_pages_used);

                // NUMA-specific stats
                registerStatsMetric(name, 0, "local_allocs", &stats.local_allocs);
                registerStatsMetric(name, 0, "bind_allocs", &stats.bind_allocs);
                registerStatsMetric(name, 0, "interleave_allocs", &stats.interleave_allocs);

                UInt32 num_nodes = phys_mem_alloc->getNumNumaNodes();
                for (UInt32 n = 0; n < num_nodes; ++n)
                {
                    if (stats.per_node_allocs)
                        registerStatsMetric(name, 0, "node" + itostr(n) + "_allocs",
                                            &stats.per_node_allocs[n]);
                    if (stats.per_node_2mb_reserved)
                        registerStatsMetric(name, 0, "node" + itostr(n) + "_2mb_reserved",
                                            &stats.per_node_2mb_reserved[n]);
                    if (stats.per_node_2mb_promoted)
                        registerStatsMetric(name, 0, "node" + itostr(n) + "_2mb_promoted",
                                            &stats.per_node_2mb_promoted[n]);
                    if (stats.per_node_spills)
                        registerStatsMetric(name, 0, "node" + itostr(n) + "_spills",
                                            &stats.per_node_spills[n]);
                }
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
