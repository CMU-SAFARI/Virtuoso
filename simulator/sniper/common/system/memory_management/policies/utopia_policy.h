#pragma once
/*
 * ================================================================================
 * UTOPIA ALLOCATOR - Sniper-Specific Policy
 * ================================================================================
 * 
 * This policy provides Sniper simulator integration for the template-based
 * UtopiaAllocator. It handles:
 *   - Configuration file reading
 *   - Statistics registration  
 *   - Debug logging
 *   - Core count access
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
namespace Utopia {

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
        // Initialize SimLog for Utopia allocator
        if (sim_log) delete sim_log;
        sim_log = new SimLog("UTOPIA", -1, DEBUG_UTOPIA);
        
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
        
        // Per-RestSeg fingerprint shadow statistics and region way preference (4KB only)
        for (int i = 0; i < alloc->getNumRestSegs(); i++) {
            auto* rs = alloc->getRestSeg(i);
            if (rs) {
                String rs_prefix = "restseg" + itostr(i) + "_";
                registerStatsMetric(name, 0, rs_prefix + "fingerprint_checks", rs->getFingerprintChecksPtr());
                registerStatsMetric(name, 0, rs_prefix + "fingerprint_matches", rs->getFingerprintMatchesPtr());
                registerStatsMetric(name, 0, rs_prefix + "fingerprint_false_positives", rs->getFingerprintFalsePositivesPtr());
                registerStatsMetric(name, 0, rs_prefix + "fingerprint_true_positives", rs->getFingerprintTruePositivesPtr());
                
                // Region way preference stats (only meaningful for 4KB RestSeg)
                registerStatsMetric(name, 0, rs_prefix + "region_way_attempts", rs->getRegionWayAttemptsPtr());
                registerStatsMetric(name, 0, rs_prefix + "region_way_hits", rs->getRegionWayHitsPtr());
                registerStatsMetric(name, 0, rs_prefix + "region_way_relearns", rs->getRegionWayRelearnsPtr());
            }
        }
    }

    // Overload for ReserveTHP (FlexSeg) initialization
    template <typename AllocatorT>
    void on_init(String name, int memory_size, int kernel_size, double threshold, AllocatorT* alloc)
    {
        // Initialize SimLog for FlexSeg
        if (sim_log) delete sim_log;
        sim_log = new SimLog("UTOPIA-FLEX", -1, DEBUG_UTOPIA);

        sim_log->info("Init FlexSeg:", name.c_str(), "threshold=", threshold);
        // Note: We skip regestering internal FlexSeg stats to avoid duplication or type mismatches.
        // UtopiaAllocator accumulates relevant stats.
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
     * 
     * When a page migrates from FlexSeg to RestSeg, its page table entry
     * should be removed since RestSeg translation doesn't use page tables.
     * This prevents stale PTW results.
     * 
     * @param address Virtual address of the migrated page
     * @param app_id Application/process ID
     */
    void deletePageTableEntry(IntPtr address, int app_id) const {
        MimicOS* mimicos = Sim()->getMimicOS();
        if (mimicos) {
            mimicos->deletePageTableEntry(address, app_id);
            if (sim_log) sim_log->debug("Deleted PT entry for VA=", SimLog::hex(address), " app=", app_id);
        }
    }
};

} // namespace Utopia
} // namespace Sniper
