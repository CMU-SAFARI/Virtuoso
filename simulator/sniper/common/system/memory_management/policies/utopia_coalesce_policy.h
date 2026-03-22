#pragma once
/*
 * ================================================================================
 * UTOPIA COALESCE ALLOCATOR - Sniper-Specific Policy
 * ================================================================================
 * 
 * This policy provides Sniper simulator integration for the template-based
 * UtopiaHashCoalesce allocator. It handles:
 *   - Configuration file reading
 *   - Statistics registration (including coalescing-specific stats)
 *   - Debug logging
 *   - Core count access
 * 
 * Mirrors utopia_policy.h but registers additional stats for:
 *   - Coalescing promotions (L0 2MB pages created via bitmap intersection)
 *   - Coalescing fast-path hits (already-coalesced region re-accesses)
 *   - 2MB radix tree lookups/hits/misses
 * 
 * ================================================================================
 */

#include "stats.h"
#include "simulator.h"
#include "config.hpp"
#include "sim_log.h"
#include "debug_config.h"
#include "mimicos.h"
#include <iostream>
#include <string>

namespace Sniper {
namespace UtopiaCoalesce {

struct MetricsPolicy
{
    mutable SimLog* sim_log;
    
    MetricsPolicy() : sim_log(nullptr) {}

    ~MetricsPolicy() {
        if (sim_log) delete sim_log;
    }
    
    // ============ Configuration Interface ============
    
    static int read_config_int(const char* key) {
        return Sim()->getCfg()->getInt(key);
    }
    
    static int read_config_int_array(const char* key, int index) {
        return Sim()->getCfg()->getIntArray(key, index);
    }
    
    static String read_config_string(const char* key) {
        return Sim()->getCfg()->getString(key);
    }
    
    static int get_num_cores() {
        return Sim()->getConfig()->getApplicationCores();
    }
    
    static std::string get_output_directory() {
        return std::string(Sim()->getConfig()->getOutputDirectory().c_str());
    }
    
    // ============ Initialization ============
    
    template <typename AllocatorT>
    void on_init(String name, int memory_size, int kernel_size, AllocatorT* alloc)
    {
        // Initialize SimLog for UtopiaCoalesce allocator
        if (sim_log) delete sim_log;
        sim_log = new SimLog("UTOPIA_COALESCE", -1, DEBUG_UTOPIA);
        
        sim_log->info("Init:", name.c_str(), "mem_size=", memory_size, "KB kernel_size=", kernel_size, "KB");
        sim_log->info("RestSegs configured:", alloc->getNumRestSegs(),
                    "2MB_idx=", alloc->getRestSeg2MBIndex(),
                    "4KB_idx=", alloc->getRestSeg4KBIndex());
        sim_log->info("THP promotion threshold:", alloc->getTHPPromotionThreshold());
        
        // Log RestSeg configuration
        for (int i = 0; i < alloc->getNumRestSegs(); i++) {
            auto* rs = alloc->getRestSeg(i);
            if (rs) {
                sim_log->info("RestSeg", i, ": size=", rs->getSizeMB(), "MB page_bits=",
                            rs->getPageSizeBits(), "assoc=", rs->getAssociativity(),
                            "sets=", rs->getNumSets(), " fingerprint_bits=", rs->getFingerprintBits());
            }
        }
        
        // Register stats using the global template function from stats.h
        auto& stats = alloc->getStats();
        
        // New detailed stats for 4-level hierarchy
        registerStatsMetric(name, 0, "restseg_2mb_allocations", &stats.restseg_2mb_allocations);
        registerStatsMetric(name, 0, "restseg_4kb_allocations", &stats.restseg_4kb_allocations);
        registerStatsMetric(name, 0, "flexseg_thp_allocations", &stats.flexseg_thp_allocations);
        registerStatsMetric(name, 0, "flexseg_4kb_allocations", &stats.flexseg_4kb_allocations);
        
        // Legacy/aggregate stats
        registerStatsMetric(name, 0, "restseg_allocations", &stats.restseg_allocations);
        registerStatsMetric(name, 0, "flexseg_allocations", &stats.flexseg_allocations);
        registerStatsMetric(name, 0, "restseg_evictions", &stats.restseg_evictions);
        registerStatsMetric(name, 0, "total_allocations", &stats.total_allocations);
        registerStatsMetric(name, 0, "migrations", &stats.migrations);
        
        // ============ Coalescing-specific stats ============
        registerStatsMetric(name, 0, "coalescing_promotions", &stats.coalescing_promotions);
        registerStatsMetric(name, 0, "coalescing_hits", &stats.coalescing_hits);
        
        // Per-RestSeg fingerprint shadow statistics and region way preference
        for (int i = 0; i < alloc->getNumRestSegs(); i++) {
            auto* rs = alloc->getRestSeg(i);
            if (rs) {
                String rs_prefix = "restseg" + itostr(i) + "_";
                registerStatsMetric(name, 0, rs_prefix + "fingerprint_checks", rs->getFingerprintChecksPtr());
                registerStatsMetric(name, 0, rs_prefix + "fingerprint_matches", rs->getFingerprintMatchesPtr());
                registerStatsMetric(name, 0, rs_prefix + "fingerprint_false_positives", rs->getFingerprintFalsePositivesPtr());
                registerStatsMetric(name, 0, rs_prefix + "fingerprint_true_positives", rs->getFingerprintTruePositivesPtr());
                
                // Region way preference stats
                registerStatsMetric(name, 0, rs_prefix + "region_way_attempts", rs->getRegionWayAttemptsPtr());
                registerStatsMetric(name, 0, rs_prefix + "region_way_hits", rs->getRegionWayHitsPtr());
                registerStatsMetric(name, 0, rs_prefix + "region_way_relearns", rs->getRegionWayRelearnsPtr());
                
                // 2MB coalesced radix tree stats (per RestSeg)
                registerStatsMetric(name, 0, rs_prefix + "coalesced_2mb_lookups", rs->getCoalesced2MBLookupsPtr());
                registerStatsMetric(name, 0, rs_prefix + "coalesced_2mb_hits", rs->getCoalesced2MBHitsPtr());
                registerStatsMetric(name, 0, rs_prefix + "coalesced_2mb_misses", rs->getCoalesced2MBMissesPtr());
            }
        }
    }

    // Overload for ReserveTHP (FlexSeg) initialization
    template <typename AllocatorT>
    void on_init(String name, int memory_size, int kernel_size, double threshold, AllocatorT* alloc)
    {
        // Initialize SimLog for FlexSeg
        if (sim_log) delete sim_log;
        sim_log = new SimLog("UTOPIA_COALESCE-FLEX", -1, DEBUG_UTOPIA);

        sim_log->info("Init FlexSeg:", name.c_str(), "threshold=", threshold);
    }
    
    // ============ Logging ============
    
    template<typename... Args>
    void log(const Args&... args) const {
        if (sim_log) sim_log->info(args...);
    }
    
    template<typename... Args>
    void log_debug(const Args&... args) const {
        if (sim_log) sim_log->debug(args...);
    }
    
    template<typename... Args>
    void log_trace(const Args&... args) const {
        if (sim_log) sim_log->trace(args...);
    }
    
    void on_out_of_memory(UInt64 bytes, UInt64 addr, UInt64 core) const {
        if (sim_log) sim_log->info("Out of memory for", bytes, "bytes addr=", SimLog::hex(addr), "core=", core);
    }
    
    // ============ Page Table Management ============
    
    /**
     * @brief Delete a page table entry after migration to RestSeg
     */
    void deletePageTableEntry(IntPtr address, int app_id) const {
        MimicOS* mimicos = Sim()->getMimicOS();
        if (mimicos) {
            mimicos->deletePageTableEntry(address, app_id);
            if (sim_log) sim_log->debug("Deleted PT entry for VA=", SimLog::hex(address), " app=", app_id);
        }
    }
};

} // namespace UtopiaCoalesce
} // namespace Sniper
