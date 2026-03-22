// ============================================================================
// @kanellok: MMU Utopia Implementation - Memory Management Unit with RestSeg
// ============================================================================
//
// This file implements the Utopia Memory Management Unit for the Sniper
// multi-core simulator. Utopia extends standard translation with:
//
//   1. RestSeg (Restricted Segments):
//      - Direct-mapped regions for hot pages
//      - Permission and tag caches for fast translation
//      - No page table walk needed for RestSeg pages
//
//   2. FlexSeg (Flexible Segments):
//      - Traditional page table backed regions
//      - Standard PTW for translation
//
//   3. UTLB (Utopia TLB):
//      - Compact TLB for RestSeg translations
//      - Stores seg_id + way_idx instead of full PPN
//      - ~2.8x more entries for same SRAM budget
//
//   4. Migration:
//      - Automatic migration from FlexSeg to RestSeg for hot pages
//      - Policy-driven migration decisions
//
// ============================================================================

#include "mmu_utopia.h"
#include "mmu_base.h"
#include "memory_manager.h"
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "pagetable_factory.h"
#include "core.h"
#include "thread.h"
#include "utopia_cache_template.h"
#include "memory_management/physical_memory_allocators/utopia.h"
#include "misc/exception_handler_base.h"
#include "mimicos.h"
#include "instruction.h"
#include "mplru_controller_impl.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include "filter_factory.h"

using namespace std;

namespace ParametricDramDirectoryMSI
{

// ============================================================================
// Construction / Destruction
// ============================================================================

	MemoryManagementUnitUtopia::MemoryManagementUnitUtopia(Core *_core, MemoryManagerBase *_memory_manager, ShmemPerfModel *_shmem_perf_model, String _name, MemoryManagementUnitBase *_nested_mmu)
		: MemoryManagementUnitBase(_core, _memory_manager, _shmem_perf_model, _name, _nested_mmu)
	{
		// Initialize centralized logging (uses DEBUG_MMU level from debug_config.h)
		mmu_log = new SimLog("MMU_UTOPIA", core->getId(), DEBUG_MMU);
		mmu_log->log("Initializing Utopia MMU for core " + std::to_string(core->getId()));

		// Build and initialize the page table walker, TLB subsystem, and RestSeg walker.
		instantiatePageTableWalker();
		instantiateTLBSubsystem();
		instantiateRestSegWalker();
		instantiateUTLB();

		// Set up internal data structures and counters for statistics.
		registerMMUStats();

		if (mmu_log->isEnabled() && core->getId() == 0)
		{
			mmu_log->log("========================================");
			mmu_log->log("   UTOPIA MMU DEBUG MODE ENABLED");
			mmu_log->log("========================================");
			mmu_log->log("UTLB: ", (utlb ? "enabled" : "disabled"));
			if (utlb)
			{
				mmu_log->log("  Entries: ", utlb->getNumEntries());
				mmu_log->log("  Associativity: ", utlb->getAssociativity());
				mmu_log->log("  Latency: ", utlb->getLatency().getNS(), " ns");
				mmu_log->log("  Bits/Entry: ", utlb->getBitsPerEntry(), " (vs ~96 for standard TLB)");
				mmu_log->log("  Capacity Ratio: ", utlb->getCapacityRatio(), "x");
			}
			mmu_log->log("========================================");
		}
	}

	MemoryManagementUnitUtopia::~MemoryManagementUnitUtopia()
	{
		// Print final statistics via SimLog
		if (mmu_log->isEnabled())
		{
			mmu_log->log("╔═════════════════════════════════════════════════════════════╗");
			mmu_log->log("║           UTOPIA MMU FINAL STATISTICS (Core ", core->getId(), ")             ║");
			mmu_log->log("╠═════════════════════════════════════════════════════════════╣");
			mmu_log->log("║ Total Translations: ", translation_stats.num_translations);
			mmu_log->log("║ TLB Hits:          ", translation_stats.data_in_tlb);
			mmu_log->log("║ UTLB Hits:         ", translation_stats.data_in_utlb);
			mmu_log->log("║ RestSeg Hits:      ", translation_stats.data_in_restseg_no_fault);
			mmu_log->log("║ FlexSeg (PTW):     ", translation_stats.data_in_flexseg_no_fault);
			mmu_log->log("╠═════════════════════════════════════════════════════════════╣");
			if (utlb)
			{
				auto utlb_stats = utlb->getStats();
				double utlb_hit_rate = utlb_stats.accesses > 0 ? 
					(double)utlb_stats.hits / utlb_stats.accesses * 100.0 : 0.0;
				mmu_log->log("║ UTLB Stats:");
				mmu_log->log("║   Accesses:    ", utlb_stats.accesses);
				mmu_log->log("║   Hits:        ", utlb_stats.hits);
				mmu_log->log("║   Misses:      ", utlb_stats.misses);
				mmu_log->log("║   Hit Rate:    ", utlb_hit_rate, "%");
				mmu_log->log("║   Evictions:   ", utlb_stats.evictions);
			}
			mmu_log->log("╚═════════════════════════════════════════════════════════════╝");
		}

		// Print sanity check summary
		if (sanity_checks_enabled)
		{
			std::cout << "[MMU Sanity] Core " << core->getId() 
			          << " checked " << va_to_pa_map.size() << " unique VA->PA mappings, "
			          << pa_to_va_map.size() << " unique PA->VA mappings, "
			          << sanity_check_violations << " violations detected" << std::endl;
		}

		delete mmu_log;
		if (utlb) delete utlb;
		delete tlb_subsystem;
		delete pt_walkers;
		delete[] translation_stats.tlb_latency_per_level;
		
		// Close access pattern log file
		if (access_pattern_log)
		{
			if (access_pattern_log->is_open())
			{
				access_pattern_log->close();
			}
			delete access_pattern_log;
			access_pattern_log = nullptr;
		}
		
		// Close PTW access log file
		if (ptw_access_log)
		{
			if (ptw_access_log->is_open())
			{
				ptw_access_log->close();
			}
			delete ptw_access_log;
			ptw_access_log = nullptr;
		}
	}

// ============================================================================
// Initialization Methods
// ============================================================================

	void MemoryManagementUnitUtopia::instantiatePageTableWalker()
	{
		ptw_migration_threshold = Sim()->getCfg()->getInt("perf_model/utopia/ptw_migration_threshold");
		dram_accesses_migration_threshold = Sim()->getCfg()->getInt("perf_model/utopia/dram_accesses_migration_threshold");
		legacy_migration_enabled = Sim()->getCfg()->getInt("perf_model/utopia/legacy_migration_enabled") != 0;

		// Create PTW filter (for speculative engines, PWC, etc.)
		String filter_type = Sim()->getCfg()->getString("perf_model/" + name + "/ptw_filter_type");
		ptw_filter = FilterPTWFactory::createFilterPTWBase(filter_type, name, core);

		// pt_walkers is an MSHR system that tracks the number of parallel page table walkers.
		pt_walkers = new MSHR(Sim()->getCfg()->getInt("perf_model/" + name + "/page_table_walkers"));
	}

	void MemoryManagementUnitUtopia::instantiateTLBSubsystem()
	{
		tlb_subsystem = new TLBHierarchy(name, core, memory_manager, shmem_perf_model);
	}

	void MemoryManagementUnitUtopia::instantiateRestSegWalker()
	{
		// Fingerprint Array Cache parameters (replaces permission filter cache)
		int fpa_cache_size = Sim()->getCfg()->getInt("perf_model/utopia/fpacache/size");
		int fpa_cache_assoc = Sim()->getCfg()->getInt("perf_model/utopia/fpacache/assoc");
		ComponentLatency fpa_cache_access_latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/utopia/fpacache/access_penalty"));
		ComponentLatency fpa_cache_miss_latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/utopia/fpacache/miss_penalty"));

		fpa_cache = new UtopiaCache("fpacache",
								   "perf_model/utopia/fpacache",
								   core->getId(),
								   64,
								   fpa_cache_size,
								   fpa_cache_assoc,
								   fpa_cache_access_latency,
								   fpa_cache_miss_latency);

		// Tag Cache parameters
		int tag_cache_size = Sim()->getCfg()->getInt("perf_model/utopia/tagcache/size");
		int tag_cache_assoc = Sim()->getCfg()->getInt("perf_model/utopia/tagcache/assoc");
		ComponentLatency tag_cache_access_latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/utopia/tagcache/access_penalty"));
		ComponentLatency tag_cache_miss_latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/utopia/tagcache/miss_penalty"));

		tar_cache = new UtopiaCache("tarcache",
									"perf_model/utopia/tarcache",
									core->getId(),
									64,
									tag_cache_size,
									tag_cache_assoc,
									tag_cache_access_latency,
									tag_cache_miss_latency);
		
		// NOTE: Legacy fp_speculation removed - CATS replaces it
		
		// Adaptive RestSeg access config (sequential vs parallel based on hit rate)
		try {
			adaptive_rsw_enabled = Sim()->getCfg()->getBool("perf_model/utopia/adaptive_rsw_enabled");
		} catch (...) {
			adaptive_rsw_enabled = true;  // Default to enabled
		}
		
		try {
			rsw_sequential_threshold = Sim()->getCfg()->getFloat("perf_model/utopia/rsw_sequential_threshold");
		} catch (...) {
			rsw_sequential_threshold = 0.90;  // 90% threshold by default
		}
		
		try {
			rsw_warmup_accesses = Sim()->getCfg()->getInt("perf_model/utopia/rsw_warmup_accesses");
		} catch (...) {
			rsw_warmup_accesses = 1000;  // 1000 accesses warmup by default
		}
		
		// Initialize hit tracking
		rsw_hits_per_restseg[0] = 0;
		rsw_hits_per_restseg[1] = 0;
		rsw_total_hits = 0;
		rsw_dominant_restseg = -1;  // Start with parallel access
		
		if (adaptive_rsw_enabled && core->getId() == 0) {
			std::cout << "[MMU::Utopia] Adaptive RSW ENABLED - threshold=" << rsw_sequential_threshold 
			          << ", warmup=" << rsw_warmup_accesses << std::endl;
		}
		
		// FPA/TAR access pattern logging initialization
		access_pattern_log = nullptr;
		try {
			access_pattern_logging_enabled = Sim()->getCfg()->getBool("perf_model/utopia/access_pattern_logging");
		} catch (...) {
			access_pattern_logging_enabled = false;  // Default to disabled
		}
		
		try {
			access_pattern_sample_rate = Sim()->getCfg()->getInt("perf_model/utopia/access_pattern_sample_rate");
		} catch (...) {
			access_pattern_sample_rate = 1;  // Log every access by default
		}
		
		access_pattern_counter = 0;  // Initialize counter
		ptw_access_log = nullptr;    // Initialize PTW log
		ptw_access_counter = 0;      // Initialize PTW counter
		
		if (access_pattern_logging_enabled) {
			std::string log_path = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) 
			                      + "/fpa_tar_access_log_core" + std::to_string(core->getId()) + ".csv";
			access_pattern_log = new std::ofstream(log_path);
			if (access_pattern_log->is_open()) {
				// Write CSV header - using cache line addresses (addr >> 6)
				*access_pattern_log << "access_num,vpn,access_type,restseg_id,cache_line_addr,"
				                    << "utopia_cache_hit,l2_hit,nuca_hit,dram_hit,latency_ns" << std::endl;
				if (core->getId() == 0) {
					std::cout << "[MMU::Utopia] FPA/TAR access pattern logging ENABLED (sample_rate=" 
					          << access_pattern_sample_rate << ") -> " << log_path << std::endl;
				}
			} else {
				delete access_pattern_log;
				access_pattern_log = nullptr;
				access_pattern_logging_enabled = false;
				std::cerr << "[MMU::Utopia] WARNING: Could not open access pattern log file" << std::endl;
			}
			
			// Also open PTW access log
			std::string ptw_log_path = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) 
			                          + "/ptw_access_log_core" + std::to_string(core->getId()) + ".csv";
			ptw_access_log = new std::ofstream(ptw_log_path);
			if (ptw_access_log->is_open()) {
				// Write CSV header for PTW log - using cache line addresses
				*ptw_access_log << "ptw_num,vpn,level,table,cache_line_addr,"
				                << "l1_hit,l2_hit,nuca_hit,dram_hit,latency_ns,is_pte" << std::endl;
				if (core->getId() == 0) {
					std::cout << "[MMU::Utopia] PTW access pattern logging ENABLED -> " << ptw_log_path << std::endl;
				}
			} else {
				delete ptw_access_log;
				ptw_access_log = nullptr;
				std::cerr << "[MMU::Utopia] WARNING: Could not open PTW access log file" << std::endl;
			}
		}
		
		// ====================================================================
		// CATS (Confidence-Aware Throttled Speculation) Configuration
		// ====================================================================
		try { spec.enabled = Sim()->getCfg()->getBool("perf_model/utopia/speculation/enabled"); } catch (...) { spec.enabled = false; }
		try { spec.wmax = Sim()->getCfg()->getInt("perf_model/utopia/speculation/wmax"); } catch (...) { spec.wmax = 2; }
		
		// Confidence table config
		try { spec.conf_bits = Sim()->getCfg()->getInt("perf_model/utopia/speculation/conf_bits"); } catch (...) { spec.conf_bits = 3; }
		try { spec.conf_threshold = Sim()->getCfg()->getInt("perf_model/utopia/speculation/conf_threshold"); } catch (...) { spec.conf_threshold = 3; }
		try { spec.conf_table_entries = Sim()->getCfg()->getInt("perf_model/utopia/speculation/conf_table_entries"); } catch (...) { spec.conf_table_entries = 16384; }
		
		// Way predictor config
		try { spec.way_predictor_enabled = Sim()->getCfg()->getBool("perf_model/utopia/speculation/way_predictor_enabled"); } catch (...) { spec.way_predictor_enabled = true; }
		try { spec.lastway_table_entries = Sim()->getCfg()->getInt("perf_model/utopia/speculation/lastway_table_entries"); } catch (...) { spec.lastway_table_entries = 8192; }
		
		// Instruction speculation
		try { spec.enable_instruction_speculation = Sim()->getCfg()->getBool("perf_model/utopia/speculation/enable_instruction_speculation"); } catch (...) { spec.enable_instruction_speculation = false; }
		
		// Initialize runtime state
		spec.spec_issued_epoch = 0;
		
		// Initialize CATS tables
		if (spec.enabled) {
			spec.conf_table.resize(spec.conf_table_entries);
			spec.way_table.resize(spec.lastway_table_entries);
			
			if (core->getId() == 0) {
				std::cout << "[MMU::Utopia] CATS speculation ENABLED (prefetch-only):"
				          << " wmax=" << spec.wmax
				          << " conf_threshold=" << spec.conf_threshold << "/" << ((1 << spec.conf_bits) - 1)
				          << std::endl;
			}
		}
		
		// NOTE: MetadataFetchConfig removed (Phase 0 cleanup)
		// Only baseline FPA+TAR path is supported now.
		
		// ====================================================================
		// Cost-based Top-K Migration Configuration
		// ====================================================================
		// Read config from allocator (if it exists) and cache locally
		cost_topk_migration_enabled = false;
		cost_score_bits = 10;
		cost_ptw_base_inc = 1;
		cost_dram_inc_cap = 3;
		
		Utopia* utopia = dynamic_cast<Utopia*>(Sim()->getMimicOS()->getMemoryAllocator());
		if (utopia && utopia->isCostTopKEnabled()) {
			cost_topk_migration_enabled = true;
			const auto& cfg = utopia->getCostTopKConfig();
			cost_score_bits = cfg.score_bits;
			cost_ptw_base_inc = cfg.ptw_base_inc;
			cost_dram_inc_cap = cfg.dram_inc_cap;
			
			if (core->getId() == 0) {
				std::cout << "[MMU::Utopia] Cost-based Top-K migration ENABLED (from allocator):"
				          << " score_bits=" << cost_score_bits
				          << " ptw_base_inc=" << cost_ptw_base_inc
				          << " dram_inc_cap=" << cost_dram_inc_cap
				          << std::endl;
			}
		}
		
		// ====================================================================
		// Radix Way Table Configuration
		// ====================================================================
		// Replaces fingerprint + tag array lookup with direct radix walk
		try {
			radix_lookup_enabled = Sim()->getCfg()->getBool("perf_model/utopia/radix_lookup_enabled");
		} catch (...) {
			radix_lookup_enabled = true;  // Default to enabled (new behavior)
		}
		
		if (core->getId() == 0) {
			if (radix_lookup_enabled) {
				std::cout << "[MMU::Utopia] Radix way table lookup ENABLED (replaces FPA+TAR)" << std::endl;
			} else {
				std::cout << "[MMU::Utopia] Radix way table lookup DISABLED (using legacy FPA+TAR)" << std::endl;
			}
		}
		
		// ====================================================================
		// FP/TAR Cache Bypass Configuration
		// ====================================================================
		// When enabled, metadata accesses bypass dedicated Utopia caches and
		// instead route through the PTW cache hierarchy (paging-structure caches)
		try {
			disable_fp_tar_caches = Sim()->getCfg()->getBool("perf_model/utopia/mmu/disable_fp_tar_caches");
		} catch (...) {
			disable_fp_tar_caches = false;  // Default: use dedicated Utopia caches
		}
		
		try {
			disable_fp_cache_only = Sim()->getCfg()->getBool("perf_model/utopia/mmu/disable_fp_cache");
		} catch (...) {
			disable_fp_cache_only = false;
		}
		
		try {
			disable_tar_cache_only = Sim()->getCfg()->getBool("perf_model/utopia/mmu/disable_tar_cache");
		} catch (...) {
			disable_tar_cache_only = false;
		}
		
		// Combined knob overrides individual knobs
		if (disable_fp_tar_caches) {
			disable_fp_cache_only = true;
			disable_tar_cache_only = true;
		}
		
		if (core->getId() == 0) {
			if (disable_fp_tar_caches) {
				std::cout << "[MMU::Utopia] FP/TAR caches DISABLED - routing metadata to PTW cache path" << std::endl;
			} else if (disable_fp_cache_only || disable_tar_cache_only) {
				std::cout << "[MMU::Utopia] Partial cache bypass: FP_disabled=" << disable_fp_cache_only 
				          << " TAR_disabled=" << disable_tar_cache_only << std::endl;
			}
		}
	}

	void MemoryManagementUnitUtopia::instantiateUTLB()
	{
		// Check if UTLB is enabled in config
		try {
			utlb_enabled = Sim()->getCfg()->getBool("perf_model/utopia/utlb/enabled");
		} catch (...) {
			utlb_enabled = true;  // Default to enabled
		}

		if (!utlb_enabled) {
			utlb = nullptr;
			std::cout << "[MMU::Utopia] UTLB disabled" << std::endl;
			return;
		}

		// Read UTLB configuration
		int utlb_entries = 2048;
		int utlb_assoc = 8;
		int utlb_latency = 1;

		try { utlb_entries = Sim()->getCfg()->getInt("perf_model/utopia/utlb/entries"); } catch (...) {}
		try { utlb_assoc = Sim()->getCfg()->getInt("perf_model/utopia/utlb/assoc"); } catch (...) {}
		try { utlb_latency = Sim()->getCfg()->getInt("perf_model/utopia/utlb/latency"); } catch (...) {}

		ComponentLatency access_latency(core->getDvfsDomain(), utlb_latency);

		utlb = new UtopiaTLB(
			"utlb",
			core->getId(),
			utlb_entries,
			utlb_assoc,
			access_latency.getLatency()
		);

		std::cout << "[MMU::Utopia] UTLB initialized: " << utlb_entries << " entries, " 
		          << utlb_assoc << "-way, " << utlb->getBitsPerEntry() << " bits/entry" << std::endl;
	}

// ============================================================================
// Statistics Registration
// ============================================================================

	void MemoryManagementUnitUtopia::registerMMUStats()
	{
		bzero(&translation_stats, sizeof(translation_stats));

		// Initialize sanity check state
		try { sanity_checks_enabled = Sim()->getCfg()->getBool("perf_model/mmu/sanity_checks_enabled"); } 
		catch (...) { sanity_checks_enabled = false; }
		sanity_check_violations = 0;

		registerStatsMetric(name, core->getId(), "sanity_check_violations", &sanity_check_violations);
		registerStatsMetric(name, core->getId(), "page_faults", &translation_stats.page_faults);
		registerStatsMetric(name, core->getId(), "total_fault_latency", &translation_stats.total_fault_latency);
		registerStatsMetric(name, core->getId(), "total_walk_latency", &translation_stats.total_walk_latency);
		registerStatsMetric(name, core->getId(), "total_rsw_latency", &translation_stats.total_rsw_latency);
		registerStatsMetric(name, core->getId(), "total_tlb_latency", &translation_stats.total_tlb_latency);

		registerStatsMetric(name, core->getId(), "total_walk_latency_flexseg_no_fault", &translation_stats.total_walk_latency_flexseg_no_fault);
		registerStatsMetric(name, core->getId(), "total_rsw_latency_restseg_no_fault", &translation_stats.total_rsw_latency_restseg_no_fault);
		registerStatsMetric(name, core->getId(), "total_walk_latency_flexseg", &translation_stats.total_walk_latency_flexseg);
		registerStatsMetric(name, core->getId(), "total_rsw_latency_restseg", &translation_stats.total_rsw_latency_restseg);
		registerStatsMetric(name, core->getId(), "total_tlb_latency_on_tlb_hit", &translation_stats.total_tlb_latency_on_tlb_hit);

		registerStatsMetric(name, core->getId(), "data_in_utlb", &translation_stats.data_in_utlb);
		registerStatsMetric(name, core->getId(), "total_utlb_latency", &translation_stats.total_utlb_latency);
		registerStatsMetric(name, core->getId(), "total_rsw_memory_requests", &translation_stats.total_rsw_memory_requests);

		registerStatsMetric(name, core->getId(), "data_in_flexseg", &translation_stats.data_in_flexseg);
		registerStatsMetric(name, core->getId(), "data_in_restseg", &translation_stats.data_in_restseg);
		registerStatsMetric(name, core->getId(), "data_in_tlb", &translation_stats.data_in_tlb);
		registerStatsMetric(name, core->getId(), "data_in_flexseg_no_fault", &translation_stats.data_in_flexseg_no_fault);
		registerStatsMetric(name, core->getId(), "data_in_restseg_no_fault", &translation_stats.data_in_restseg_no_fault);
		registerStatsMetric(name, core->getId(), "total_translation_latency", &translation_stats.total_translation_latency);

		registerStatsMetric(name, core->getId(), "flextorest_migrations", &translation_stats.flextorest_migrations);
		registerStatsMetric(name, core->getId(), "tlb_shootdowns", &translation_stats.tlb_shootdowns);
		registerStatsMetric(name, core->getId(), "requests_affected_by_migration", &translation_stats.requests_affected_by_migration);
		registerStatsMetric(name, core->getId(), "migration_stall_cycles", &translation_stats.migration_stall_cycles);
		registerStatsMetric(name, core->getId(), "num_translations", &translation_stats.num_translations);
		registerStatsMetric(name, core->getId(), "l2_tlb_misses", &translation_stats.l2_tlb_misses);
		
		// NOTE: Legacy fp_speculation stats removed - CATS replaces it
		
		// Adaptive RestSeg access stats
		registerStatsMetric(name, core->getId(), "rsw_sequential_accesses", &translation_stats.rsw_sequential_accesses);
		registerStatsMetric(name, core->getId(), "rsw_parallel_accesses", &translation_stats.rsw_parallel_accesses);
		registerStatsMetric(name, core->getId(), "rsw_hits_restseg0", &translation_stats.rsw_hits_restseg0);
		registerStatsMetric(name, core->getId(), "rsw_hits_restseg1", &translation_stats.rsw_hits_restseg1);

		// Per-RestSeg detailed latency stats
		registerStatsMetric(name, core->getId(), "fpa_latency_restseg0", &translation_stats.fpa_latency_restseg0);
		registerStatsMetric(name, core->getId(), "fpa_latency_restseg1", &translation_stats.fpa_latency_restseg1);
		registerStatsMetric(name, core->getId(), "tar_latency_restseg0", &translation_stats.tar_latency_restseg0);
		registerStatsMetric(name, core->getId(), "tar_latency_restseg1", &translation_stats.tar_latency_restseg1);
		registerStatsMetric(name, core->getId(), "fpa_accesses_restseg0", &translation_stats.fpa_accesses_restseg0);
		registerStatsMetric(name, core->getId(), "fpa_accesses_restseg1", &translation_stats.fpa_accesses_restseg1);
		registerStatsMetric(name, core->getId(), "tar_accesses_restseg0", &translation_stats.tar_accesses_restseg0);
		registerStatsMetric(name, core->getId(), "tar_accesses_restseg1", &translation_stats.tar_accesses_restseg1);
		registerStatsMetric(name, core->getId(), "fpa_cache_hits_restseg0", &translation_stats.fpa_cache_hits_restseg0);
		registerStatsMetric(name, core->getId(), "fpa_cache_hits_restseg1", &translation_stats.fpa_cache_hits_restseg1);
		registerStatsMetric(name, core->getId(), "tar_cache_hits_restseg0", &translation_stats.tar_cache_hits_restseg0);
		registerStatsMetric(name, core->getId(), "tar_cache_hits_restseg1", &translation_stats.tar_cache_hits_restseg1);
		registerStatsMetric(name, core->getId(), "fp_filtered_restseg0", &translation_stats.fp_filtered_restseg0);
		registerStatsMetric(name, core->getId(), "fp_filtered_restseg1", &translation_stats.fp_filtered_restseg1);

		// FPA/TAR memory hierarchy hit stats
		registerStatsMetric(name, core->getId(), "fpa_hit_l2_restseg0", &translation_stats.fpa_hit_l2_restseg0);
		registerStatsMetric(name, core->getId(), "fpa_hit_l2_restseg1", &translation_stats.fpa_hit_l2_restseg1);
		registerStatsMetric(name, core->getId(), "fpa_hit_nuca_restseg0", &translation_stats.fpa_hit_nuca_restseg0);
		registerStatsMetric(name, core->getId(), "fpa_hit_nuca_restseg1", &translation_stats.fpa_hit_nuca_restseg1);
		registerStatsMetric(name, core->getId(), "fpa_hit_dram_restseg0", &translation_stats.fpa_hit_dram_restseg0);
		registerStatsMetric(name, core->getId(), "fpa_hit_dram_restseg1", &translation_stats.fpa_hit_dram_restseg1);
		registerStatsMetric(name, core->getId(), "tar_hit_l2_restseg0", &translation_stats.tar_hit_l2_restseg0);
		registerStatsMetric(name, core->getId(), "tar_hit_l2_restseg1", &translation_stats.tar_hit_l2_restseg1);
		registerStatsMetric(name, core->getId(), "tar_hit_nuca_restseg0", &translation_stats.tar_hit_nuca_restseg0);
		registerStatsMetric(name, core->getId(), "tar_hit_nuca_restseg1", &translation_stats.tar_hit_nuca_restseg1);
		registerStatsMetric(name, core->getId(), "tar_hit_dram_restseg0", &translation_stats.tar_hit_dram_restseg0);
		registerStatsMetric(name, core->getId(), "tar_hit_dram_restseg1", &translation_stats.tar_hit_dram_restseg1);

		// Speculation correctness stats
		registerStatsMetric(name, core->getId(), "spec_prefetch_correct", &translation_stats.spec_prefetch_correct);
		registerStatsMetric(name, core->getId(), "spec_prefetch_wrong", &translation_stats.spec_prefetch_wrong);
		registerStatsMetric(name, core->getId(), "spec_prefetch_unused", &translation_stats.spec_prefetch_unused);

		// ================================================================
		// CATS (Confidence-Aware Throttled Speculation) Stats
		// ================================================================
		registerStatsMetric(name, core->getId(), "cats_spec_attempts", &translation_stats.cats_spec_attempts);
		registerStatsMetric(name, core->getId(), "cats_spec_correct", &translation_stats.cats_spec_correct);
		registerStatsMetric(name, core->getId(), "cats_spec_wrong_way", &translation_stats.cats_spec_wrong_way);
		registerStatsMetric(name, core->getId(), "cats_spec_miss", &translation_stats.cats_spec_miss);
		registerStatsMetric(name, core->getId(), "cats_gated_ambiguity", &translation_stats.cats_gated_ambiguity);
		registerStatsMetric(name, core->getId(), "cats_gated_confidence", &translation_stats.cats_gated_confidence);
		registerStatsMetric(name, core->getId(), "cats_gated_instruction", &translation_stats.cats_gated_instruction);
		registerStatsMetric(name, core->getId(), "cats_prefetch_issued", &translation_stats.cats_prefetch_issued);
		registerStatsMetric(name, core->getId(), "cats_conf_updates", &translation_stats.cats_conf_updates);
		registerStatsMetric(name, core->getId(), "cats_way_predictions", &translation_stats.cats_way_predictions);
		registerStatsMetric(name, core->getId(), "cats_way_prediction_hits", &translation_stats.cats_way_prediction_hits);
		registerStatsMetric(name, core->getId(), "cats_considered", &translation_stats.cats_considered);
		registerStatsMetric(name, core->getId(), "cats_cold_spec_issued", &translation_stats.cats_cold_spec_issued);

		// ================================================================
		// Cost-based Top-K Migration Stats
		// ================================================================
		registerStatsMetric(name, core->getId(), "cost_topk_score_updates", &translation_stats.cost_topk_score_updates);
		registerStatsMetric(name, core->getId(), "cost_topk_migration_attempts", &translation_stats.cost_topk_migration_attempts);
		registerStatsMetric(name, core->getId(), "cost_topk_migrations_issued", &translation_stats.cost_topk_migrations_issued);
		
		// Also register allocator's global CostTopK stats (only on core 0)
		if (core->getId() == 0 && cost_topk_migration_enabled) {
			Utopia* utopia = dynamic_cast<Utopia*>(Sim()->getMimicOS()->getMemoryAllocator());
			if (utopia) {
				auto& state = utopia->getCostTopKState();
				registerStatsMetric(name + "_global", 0, "cost_topk_score_updates_total", &state.score_updates);
				registerStatsMetric(name + "_global", 0, "cost_topk_topk_inserts", &state.topk_inserts);
				registerStatsMetric(name + "_global", 0, "cost_topk_topk_replacements", &state.topk_replacements);
				registerStatsMetric(name + "_global", 0, "cost_topk_migration_attempts_total", &state.migration_attempts);
				registerStatsMetric(name + "_global", 0, "cost_topk_migrations_issued_total", &state.migrations_issued);
				registerStatsMetric(name + "_global", 0, "cost_topk_gated_credits", &state.gated_credits);
				registerStatsMetric(name + "_global", 0, "cost_topk_gated_threshold", &state.gated_threshold);
				registerStatsMetric(name + "_global", 0, "cost_topk_gated_sticky", &state.gated_sticky);
				registerStatsMetric(name + "_global", 0, "cost_topk_gated_cooldown", &state.gated_cooldown);
			}
		}

		// ================================================================
		// Radix Way Table Stats (per-core view)
		// ================================================================
		registerStatsMetric(name, core->getId(), "radix_lookups_restseg0", &translation_stats.radix_lookups_restseg0);
		registerStatsMetric(name, core->getId(), "radix_lookups_restseg1", &translation_stats.radix_lookups_restseg1);
		registerStatsMetric(name, core->getId(), "radix_hits_restseg0", &translation_stats.radix_hits_restseg0);
		registerStatsMetric(name, core->getId(), "radix_hits_restseg1", &translation_stats.radix_hits_restseg1);
		registerStatsMetric(name, core->getId(), "radix_misses_restseg0", &translation_stats.radix_misses_restseg0);
		registerStatsMetric(name, core->getId(), "radix_misses_restseg1", &translation_stats.radix_misses_restseg1);
		registerStatsMetric(name, core->getId(), "radix_latency_restseg0", &translation_stats.radix_latency_restseg0);
		registerStatsMetric(name, core->getId(), "radix_latency_restseg1", &translation_stats.radix_latency_restseg1);

		// Radix PWC (page walk cache) hit stats
		registerStatsMetric(name, core->getId(), "radix_pwc_hits", &translation_stats.radix_pwc_hits);
		registerStatsMetric(name, core->getId(), "radix_pwc_misses", &translation_stats.radix_pwc_misses);

		// Radix internal node memory hierarchy hit stats (on PWC miss)
		registerStatsMetric(name, core->getId(), "radix_internal_hit_l2", &translation_stats.radix_internal_hit_l2);
		registerStatsMetric(name, core->getId(), "radix_internal_hit_nuca", &translation_stats.radix_internal_hit_nuca);
		registerStatsMetric(name, core->getId(), "radix_internal_hit_dram", &translation_stats.radix_internal_hit_dram);
		registerStatsMetric(name, core->getId(), "radix_internal_accesses", &translation_stats.radix_internal_accesses);

		// Radix leaf node memory hierarchy hit stats (on PWC miss)
		registerStatsMetric(name, core->getId(), "radix_leaf_hit_l2", &translation_stats.radix_leaf_hit_l2);
		registerStatsMetric(name, core->getId(), "radix_leaf_hit_nuca", &translation_stats.radix_leaf_hit_nuca);
		registerStatsMetric(name, core->getId(), "radix_leaf_hit_dram", &translation_stats.radix_leaf_hit_dram);
		registerStatsMetric(name, core->getId(), "radix_leaf_accesses", &translation_stats.radix_leaf_accesses);

		// ================================================================
		// Metadata routing stats (FP/TAR bypass to PTW caches)
		// ================================================================
		registerStatsMetric(name, core->getId(), "meta_accesses_routed_to_ptw", &translation_stats.meta_accesses_routed_to_ptw);
		registerStatsMetric(name, core->getId(), "meta_accesses_routed_to_fp_tar", &translation_stats.meta_accesses_routed_to_fp_tar);

		// NOTE: Metadata co-fetch stats removed (Phase 0 cleanup)
		// Only baseline FPA+TAR path is supported now.

		translation_stats.tlb_latency_per_level = new SubsecondTime[tlb_subsystem->getTLBSubsystem().size()];
		for (UInt32 i = 0; i < tlb_subsystem->getTLBSubsystem().size(); i++)
			registerStatsMetric(name, core->getId(), "tlb_latency_" + itostr(i), &translation_stats.tlb_latency_per_level[i]);
	}

// ============================================================================
// Address Translation - Core MMU Operation
// ============================================================================

	IntPtr MemoryManagementUnitUtopia::performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count)
	{
		dram_accesses_during_last_walk = 0;

		if (count)
			translation_stats.num_translations++;

		// Capture current translation latency to compute delta at the end
		SubsecondTime prev_total_translation_latency = translation_stats.total_translation_latency;

		IntPtr ppn_result = 0;
		int page_size = -1;

		mmu_log->debug("Address Translation: VA=", mmu_log->hex(address));

		// ====================================================================
		// Handle ongoing migrations
		// ====================================================================
		// migration_queue is keyed by 4KB VPN (address >> 12) for consistency.
		// This ensures both CostTopK and legacy migration paths use the same key.
		// ====================================================================
		SubsecondTime migration_latency = SubsecondTime::Zero();
		UInt64 vpn_4kb_check = address >> 12;
		
		auto mig_it = migration_queue.find(vpn_4kb_check);
		if (mig_it != migration_queue.end())
		{
			if (mig_it->second > shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD))
			{
				migration_latency = mig_it->second - shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
				translation_stats.migration_stall_cycles += migration_latency;
				if (count)
					translation_stats.requests_affected_by_migration++;
			}
		}

		if (count)
			translation_stats.total_translation_latency += migration_latency;

		// ====================================================================
		// TLB Hierarchy Lookup
		// ====================================================================
		const TLBSubsystem& tlbs = tlb_subsystem->getTLBSubsystem();
		SubsecondTime time = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

		bool hit = false;
		TLB *hit_tlb = NULL;
		CacheBlockInfo *tlb_block_info_hit = NULL;
		CacheBlockInfo *tlb_block_info = NULL;
		int hit_level = -1;

		for (UInt32 i = 0; i < tlbs.size(); i++)
		{
			for (UInt32 j = 0; j < tlbs[i].size(); j++)
			{
				bool tlb_stores_instructions = (tlbs[i][j]->getType() == TLBtype::Instruction) || (tlbs[i][j]->getType() == TLBtype::Unified);

				if (tlb_stores_instructions && instruction)
				{
					tlb_block_info = tlbs[i][j]->lookup(address, time, count, lock, eip, modeled, count, NULL);
					if (tlb_block_info != NULL)
					{
						tlb_block_info_hit = tlb_block_info;
						hit_tlb = tlbs[i][j];
						hit_level = i;
						hit = true;
					}
				}
				else if (!instruction)
				{
					bool tlb_stores_data = !(tlbs[i][j]->getType() == TLBtype::Instruction);
					if (tlb_stores_data)
					{
						tlb_block_info = tlbs[i][j]->lookup(address, time, count, lock, eip, modeled, count, NULL);
						if (tlb_block_info != NULL)
						{
							tlb_block_info_hit = tlb_block_info;
							hit_tlb = tlbs[i][j];
							hit_level = i;
							hit = true;
						}
					}
				}
			}
			if (hit)
				break;
		}

		SubsecondTime charged_tlb_latency = SubsecondTime::Zero();

		// ====================================================================
		// TLB Hit - Charge latency
		// ====================================================================
		if (hit)
		{
			mmu_log->debug("TLB Hit at level ", hit_level, " TLB: ", hit_tlb->getName().c_str());

			const TLBSubsystem& tlb_path = instruction ?
				tlb_subsystem->getInstructionPath() : tlb_subsystem->getDataPath();

			SubsecondTime tlb_latency[hit_level + 1];
			for (int i = 0; i < hit_level; i++)
			{
				for (UInt32 j = 0; j < tlb_path[i].size(); j++)
				{
					tlb_latency[i] = max(tlb_path[i][j]->getLatency(), tlb_latency[i]);
				}
				translation_stats.total_tlb_latency += tlb_latency[i];
				translation_stats.tlb_latency_per_level[i] += tlb_latency[i];
				charged_tlb_latency += tlb_latency[i];
			}

			for (UInt32 j = 0; j < tlb_path[hit_level].size(); j++)
			{
				if (tlb_path[hit_level][j] == hit_tlb)
				{
					translation_stats.total_tlb_latency += hit_tlb->getLatency();
					charged_tlb_latency += hit_tlb->getLatency();
					translation_stats.tlb_latency_per_level[hit_level] += hit_tlb->getLatency();
				}
			}

			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time + charged_tlb_latency);

			if (count)
			{
				translation_stats.total_translation_latency += charged_tlb_latency;
				translation_stats.total_tlb_latency_on_tlb_hit += charged_tlb_latency;
				translation_stats.data_in_tlb++;
			}
		}
		else
		{
			// TLB Miss - charge full hierarchy latency
			mmu_log->debug("TLB Miss");

			SubsecondTime tlb_latency[tlbs.size()];
			for (UInt32 i = 0; i < tlbs.size(); i++)
			{
				for (UInt32 j = 0; j < tlbs[i].size(); j++)
				{
					tlb_latency[i] = max(tlbs[i][j]->getLatency(), tlb_latency[i]);
				}
				translation_stats.total_tlb_latency += tlb_latency[i];
				charged_tlb_latency += tlb_latency[i];
			}
			
			// Track L2 TLB misses for MPKI calculation
			if (count)
			{
				translation_stats.l2_tlb_misses++;
				
				// Update MimicOS per-core stats (for adaptive policies like MPLRU)
				MimicOS* mimicos = Sim()->getMimicOS();
				if (mimicos && mimicos->isPerCoreStatsInitialized())
				{
					// This is an L2 TLB miss - update with the translation latency
					// Note: walk_latency will be added later when PTW completes
					mimicos->updateTranslationStats(core->getId(), 
					                                SubsecondTime::Zero(),  // Will update with full latency later
					                                true,  // is_l2_miss
					                                SubsecondTime::Zero());
				}
				
				// Note: MPLRU epoch processing is now triggered by the replacement policy
				// when it queries getMetaLevel(), not from MMUs
			}
		}

		// ====================================================================
		// UTLB Check + RestSeg Walk (parallel with L2 TLB on L1 TLB miss)
		// ====================================================================
		// On L1 TLB miss, we speculatively start:
		//   1. L2 TLB lookup
		//   2. UTLB lookup  
		//   3. RestSeg Walk
		// All happen in parallel - we take the result from whichever hits first
		// ====================================================================
		
		bool utlb_hit = false;
		SubsecondTime utlb_latency = SubsecondTime::Zero();
		uint8_t utlb_seg_id = 0;
		uint8_t utlb_way_idx = 0;
		int utlb_page_size = -1;

		bool rsw_hit = false;
		SubsecondTime rsw_latency = SubsecondTime::Zero();
		IntPtr rsw_ppn_result = 0;
		int rsw_page_size = -1;

		// Start UTLB and RSW in parallel with L2 TLB (only if L1 TLB missed)
		if (!hit)
		{
			// UTLB lookup (parallel with L2 TLB and RSW)
			if (utlb_enabled && utlb != nullptr)
			{
				// Try 2MB first, then 4KB
				UTLBLookupResult utlb_result = utlb->lookup(address, 21, count);
				if (!utlb_result.hit)
					utlb_result = utlb->lookup(address, 12, count);

				if (utlb_result.hit)
				{
					utlb_hit = true;
					utlb_seg_id = utlb_result.seg_id;
					utlb_way_idx = utlb_result.way_idx;
					utlb_page_size = utlb_result.page_size;
					utlb_latency = utlb->getLatency();

					mmu_log->debug("UTLB Hit: seg_id=", (int)utlb_seg_id, " way_idx=", (int)utlb_way_idx);

					if (count)
					{
						translation_stats.data_in_utlb++;
						translation_stats.total_utlb_latency += utlb_latency;
					}
				}
			}

			// RestSeg Walk (parallel with L2 TLB and UTLB)
			// Always perform RSW to model parallel access, but only use result if UTLB misses
			mmu_log->debug("RestSeg Walk (parallel)");

			// ================================================================
			// CATS Speculation: Issue data prefetches before metadata access
			// ================================================================
			RSWLookupResult cats_lookup;
			SpecDecision cats_decision;
			bool cats_ran = false;  // Fix D: Track whether functionalRSWLookup was called
			SubsecondTime cats_t_start = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
			
			if (spec.enabled && !instruction)
			{
				// Do functional lookup to get candidates
				cats_lookup = functionalRSWLookup(address);
				cats_ran = true;  // Fix D: Mark that lookup was performed
				
				// Phase 5 fix: Speculate based on num_candidates, not ground truth hit
				// The hit field tells us if address is in RestSeg, but we want to
				// speculate whenever we have fingerprint matches (candidates)
				if (cats_lookup.num_candidates > 0)
				{
					// Track that CATS was considered (had candidates to speculate on)
					if (count)
						translation_stats.cats_considered++;
					
					// Decide whether to speculate
					cats_decision = decideSpeculation(cats_lookup, instruction, cats_t_start, count);
					
					// Execute speculation (issue prefetches) before metadata access
					if (cats_decision.do_speculate)
					{
						executeSpeculation(cats_decision, eip, cats_t_start, count);
					}
				}
			}

			auto rsw_result = RestSegWalk(address, instruction, eip, lock, modeled, count);
			rsw_hit = (get<0>(rsw_result) != -1);
			rsw_page_size = get<0>(rsw_result);
			rsw_ppn_result = get<1>(rsw_result);
			rsw_latency = get<2>(rsw_result);
			
			if (count)
				translation_stats.total_rsw_latency += rsw_latency;
			
			// ================================================================
			// CATS Outcome: Update confidence and way predictor
			// ================================================================
			// Fix D: Only update if functionalRSWLookup ran and produced candidates
			if (spec.enabled && cats_ran && cats_lookup.num_candidates > 0)
			{
				// Determine if metadata was fast (cache hit)
				bool metadata_fast = (rsw_latency.getNS() < 10);  // Threshold for "fast"
				
				// Issue #3 fix: Compute correct_way by checking which RestSeg actually contains the VA
				// Don't blindly trust cats_lookup.seg_id - verify by searching all RestSegs
				Utopia *m_utopia = (Utopia *)Sim()->getMimicOS()->getMemoryAllocator();
				int app_id = core->getThread() ? core->getThread()->getAppId() : 0;
				int correct_way = -1;
				[[maybe_unused]] int correct_seg_id = -1;
				
				if (rsw_hit)
				{
					// Search all RestSegs to find which one actually contains the VA
					int num_restsegs = m_utopia->getNumRestSegs();
					for (int i = 0; i < num_restsegs; i++)
					{
						auto restseg = m_utopia->getRestSeg(i);
						if (!restseg) continue;
						
						if (restseg->inRestSeg(address, app_id, false /* count_stats */))
						{
							correct_seg_id = i;
							correct_way = restseg->getWayForAddress(address, app_id);
							break;
						}
					}
				}
				
				updateCATSOutcome(cats_lookup, cats_decision, rsw_hit, correct_way, metadata_fast, count);
			}
		}

		// ====================================================================
		// UTLB Hit - Compute PA directly
		// ====================================================================
		if (!hit && utlb_hit)
		{
			mmu_log->debug("UTLB Hit - computing PA directly");

			Utopia *m_utopia = (Utopia *)Sim()->getMimicOS()->getMemoryAllocator();
			int app_id = core->getThread() ? core->getThread()->getAppId() : 0;

			auto restseg = m_utopia->getRestSeg(utlb_seg_id);
			ppn_result = restseg->calculatePhysicalAddress(address, app_id);
			page_size = utlb_page_size;
			rsw_hit = true;

			if (count)
				translation_stats.total_translation_latency += max(charged_tlb_latency, utlb_latency);

			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, max(charged_tlb_latency, utlb_latency) + time);
		}

		// ====================================================================
		// RSW Hit (not via UTLB) - Allocate in UTLB
		// ====================================================================
		if (!hit && rsw_hit && !utlb_hit)
		{
			mmu_log->debug("RSW Hit - allocating to UTLB");

			// Use RSW result
			ppn_result = rsw_ppn_result;
			page_size = rsw_page_size;

			if (count)
			{
				translation_stats.total_translation_latency += max(charged_tlb_latency, rsw_latency);
				translation_stats.total_rsw_latency_restseg_no_fault += rsw_latency;
				translation_stats.data_in_restseg_no_fault++;
			}

			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, max(charged_tlb_latency, rsw_latency) + time);

			// Allocate in UTLB
			if (utlb)
			{
				Utopia *m_utopia = (Utopia *)Sim()->getMimicOS()->getMemoryAllocator();
				int app_id = core->getThread() ? core->getThread()->getAppId() : 0;

				for (int seg_id = 0; seg_id < 2; seg_id++)
				{
					auto restseg = m_utopia->getRestSeg(seg_id);
					if (restseg)
					{
						int way_idx = restseg->lookupWay(address, app_id);
						if (way_idx >= 0)
						{
							// allocate(address, page_size_bits, seg_id, way_idx)
							utlb->allocate(address, page_size, seg_id, way_idx);
							break;
						}
					}
				}
			}
		}

		// ====================================================================
		// Page Table Walk (TLB miss + RSW miss)
		// ====================================================================
		// 
		// FUNCTIONAL-FIRST TRANSLATION FLOW:
		// -------------------------------------------------------------------------
		// 1. Functional RSW lookup → if hit, charge RSW latency immediately → done
		// 2. If RSW miss: Functional PTW → if no fault, charge PTW latency immediately → done
		// 3. If fault: Handle fault, then:
		//    - If page placed in RestSeg → do full RSW (with latency)
		//    - If page placed in FlexSeg → do full PTW (with latency)
		//
		SubsecondTime total_walk_latency = SubsecondTime::Zero();
		SubsecondTime total_fault_latency = SubsecondTime::Zero();
		bool was_serviced_by_flexseg = false;

		if (!hit && !rsw_hit)
		{
			if (count)
				translation_stats.page_table_walks++;

			mmu_log->debug("TLB Miss + RSW Miss -> Starting functional-first translation");

			int app_id = core->getThread()->getAppId();
			PageTable *page_table = Sim()->getMimicOS()->getPageTable(app_id);

			// Get PT walker slot
			SubsecondTime time_for_pt = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
			struct MSHREntry pt_walker_entry;
			pt_walker_entry.request_time = time_for_pt;
			SubsecondTime delay = pt_walkers->getSlotAllocationDelay(pt_walker_entry.request_time);

			if (count)
				translation_stats.total_translation_latency += delay;

			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time_for_pt + delay);

			// ================================================================
			// Step 1: Functional RSW lookup
			// ================================================================
			RSWLookupResult rsw_lookup = functionalRSWLookup(address);
			
			if (rsw_lookup.hit)
			{
				// RSW Hit (no fault) - charge RSW latency immediately
				mmu_log->debug("Functional RSW hit - charging latency");
				
				total_walk_latency = chargeRSWLatency(rsw_lookup, instruction, eip, lock, modeled, count);
				page_size = rsw_lookup.page_size_bits;
				ppn_result = rsw_lookup.ppn;

				// Phase 2 fix: Update hit counters here (single location for functional-first path)
				rsw_total_hits++;
				if (rsw_lookup.seg_id >= 0 && rsw_lookup.seg_id < 2)
					rsw_hits_per_restseg[rsw_lookup.seg_id]++;
				
				if (count)
				{
					translation_stats.total_rsw_latency += total_walk_latency;
					translation_stats.total_translation_latency += total_walk_latency;
					translation_stats.total_rsw_latency_restseg_no_fault += total_walk_latency;
					translation_stats.data_in_restseg_no_fault++;
					
					// Phase 2 fix: Hit tracking moved here from chargeRSWLatency
					if (rsw_lookup.seg_id == 0)
						translation_stats.rsw_hits_restseg0++;
					else if (rsw_lookup.seg_id == 1)
						translation_stats.rsw_hits_restseg1++;
				}

				pt_walker_entry.completion_time = time_for_pt + delay + total_walk_latency;
				pt_walkers->allocate(pt_walker_entry);
				shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, pt_walker_entry.completion_time);
			}
			else
			{
				// ============================================================
				// Step 2: RSW miss - try functional PTW
				// ============================================================
				mmu_log->debug("Functional RSW miss, trying functional PTW");
				
				PTWResult ptw_result = page_table->initializeWalk(address, count, false /* is_prefetch */, 
				                                                  false /* restart_walk_after_fault */);
				
				if (!ptw_result.fault_happened)
				{
					// PTW success (no fault) - filter and charge PTW latency immediately
					mmu_log->debug("Functional PTW success - charging latency");
					was_serviced_by_flexseg = true;
					
					// ========================================================
					// Phase 1: Cold speculation - speculate after PTW success
					// ========================================================
					// RSW missed but PTW succeeded (no fault). This is a cold access
					// to a valid page. We can speculate using fingerprint candidates
					// computed during functionalRSWLookup (even though hit=false).
					if (spec.enabled && !instruction && rsw_lookup.num_candidates > 0)
					{
						SubsecondTime cold_spec_time = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
						
						// Track cold speculation consideration
						if (count)
							translation_stats.cats_considered++;
						
						// Decide whether to speculate on cold access
						SpecDecision cold_decision = decideSpeculation(rsw_lookup, instruction, cold_spec_time, count);
						
						if (cold_decision.do_speculate)
						{
							mmu_log->debug("Cold speculation: issuing prefetches for ", rsw_lookup.num_candidates, " candidates");
							executeSpeculation(cold_decision, eip, cold_spec_time, count);
							
							// Track cold speculation issued
							if (count)
								translation_stats.cats_cold_spec_issued++;
							
							// Update CATS outcome - for cold access, hit=false, correct_way=-1
							// This will decrement confidence (wasteful speculation on miss)
							// but that's expected - cold speculation is speculative
							updateCATSOutcome(rsw_lookup, cold_decision, false, -1, false, count);
						}
					}
					
					// Apply PTW filter (e.g., PWC filtering)
					ptw_result = filterPTWResult(address, ptw_result, page_table, count);
					
					total_walk_latency = calculatePTWCycles(ptw_result, count, modeled, eip, lock, address);
					page_size = ptw_result.page_size;
					ppn_result = ptw_result.ppn;
					
					// PTW logging now happens via logPTWCacheAccess hook in calculatePTWCycles

					if (count)
					{
						translation_stats.total_walk_latency += total_walk_latency;
						translation_stats.total_translation_latency += total_walk_latency;
						translation_stats.total_walk_latency_flexseg_no_fault += total_walk_latency;
						translation_stats.data_in_flexseg_no_fault++;
					}

					pt_walker_entry.completion_time = time_for_pt + delay + total_walk_latency;
					pt_walkers->allocate(pt_walker_entry);
					shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, pt_walker_entry.completion_time);
				}
				else
				{
					// ========================================================
					// Step 3: Page fault - handle fault, then do full walk
					// ========================================================
					mmu_log->debug("Page fault detected - handling fault");
					
					if (count)
						translation_stats.page_faults++;

					// Handle the fault via base-class exception handler interface
					ExceptionHandlerBase *handler = getCore()->getExceptionHandler();
					ExceptionHandlerBase::FaultCtx fault_ctx{};
					fault_ctx.vpn = address >> 12;
					fault_ctx.page_table = page_table;
					fault_ctx.alloc_in.metadata_frames = ptw_result.requested_frames;
					handler->handle_page_fault(fault_ctx);

					// Charge page fault latency if configured
					if (count_page_fault_latency_enabled)
					{
						SubsecondTime m_page_fault_latency = Sim()->getMimicOS()->getPageFaultLatency();
						if (count)
							translation_stats.total_fault_latency += m_page_fault_latency;
						total_fault_latency = m_page_fault_latency;
					}

					// Check where Utopia allocated the page
					Utopia *utopia = dynamic_cast<Utopia *>(Sim()->getMimicOS()->getMemoryAllocator());
					bool page_in_restseg = utopia->getLastAllocatedInRestSeg();

					if (page_in_restseg)
					{
						// Page placed in RestSeg - do full RSW with latency
						mmu_log->debug("Page allocated in RestSeg - performing RSW");
						
						auto rsw_result = RestSegWalk(address, instruction, eip, lock, modeled, count);
						page_size = get<0>(rsw_result);
						ppn_result = get<1>(rsw_result);
						total_walk_latency = get<2>(rsw_result);

						if (count)
						{
							translation_stats.total_rsw_latency += total_walk_latency;
							translation_stats.total_translation_latency += total_walk_latency;
							translation_stats.total_rsw_latency_restseg += total_walk_latency;
							translation_stats.data_in_restseg++;
						}
					}
					else
					{
						// Page placed in FlexSeg - do full PTW with latency
						mmu_log->debug("Page allocated in FlexSeg - performing PTW");
						was_serviced_by_flexseg = true;
						
						PTWResult post_fault_ptw = page_table->initializeWalk(address, count, false /* is_prefetch */, 
						                                                       false /* restart_walk_after_fault */);
						assert(!post_fault_ptw.fault_happened && "PTW should succeed after fault handling");
						
						// Apply PTW filter (e.g., PWC filtering)
						post_fault_ptw = filterPTWResult(address, post_fault_ptw, page_table, count);
						
						total_walk_latency = calculatePTWCycles(post_fault_ptw, count, modeled, eip, lock, address);
						page_size = post_fault_ptw.page_size;
						ppn_result = post_fault_ptw.ppn;
						
						// PTW logging now happens via logPTWCacheAccess hook in calculatePTWCycles

						if (count)
						{
							translation_stats.total_walk_latency += total_walk_latency;
							translation_stats.total_translation_latency += total_walk_latency;
							translation_stats.total_walk_latency_flexseg += total_walk_latency;
							translation_stats.data_in_flexseg++;
						}
					}

					pt_walker_entry.completion_time = time_for_pt + delay + total_walk_latency + total_fault_latency;
					pt_walkers->allocate(pt_walker_entry);

					// Charge fault latency via pseudo-instruction
					if (count_page_fault_latency_enabled)
					{
						PseudoInstruction *i = new PageFaultRoutineInstruction(total_fault_latency);
						getCore()->getPerformanceModel()->queuePseudoInstruction(i);
					}

					shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, pt_walker_entry.completion_time);
				}
			}

			// Migration decision for FlexSeg pages
			if (was_serviced_by_flexseg)
			{
				// ================================================================
				// DESIGN NOTE: Epoch and migration accounting
				// ================================================================
				// Epochs advance only on PTW-serviced FlexSeg translations, NOT on
				// TLB hits or RestSeg hits. This is intentional: we want the cost
				// policy to focus on "expensive-to-translate" pages (those requiring
				// full page table walks). Credit refill and ranking decay happen at
				// epoch boundaries, so this ties migration budget to actual PTW load.
				// ================================================================
				
				// Always use 4KB VPN granularity for consistent tracking across all paths
				// This ensures migration_queue key matches between enqueue and check.
				UInt64 vpn_4kb = address >> 12;
				
				Utopia *utopia = dynamic_cast<Utopia *>(Sim()->getMimicOS()->getMemoryAllocator());
				
				// Track whether cost-based migration handled this page
				bool migrated_by_cost_policy = false;
				
				// ================================================================
				// Cost-based Top-K Migration: Update score and attempt migration
				// ================================================================
				// Only for 4KB pages (page_size == 12), since RestSeg handles larger pages
				// ================================================================
				if (cost_topk_migration_enabled && page_size == 12)
				{
					// Compute score increment: base + min(dram_accesses, cap)
					int dram_accesses_occurred = getDramAccessesDuringLastWalk();
					int dram_cap = std::min(dram_accesses_occurred, cost_dram_inc_cap);
					int inc = cost_ptw_base_inc + dram_cap;  // Typical: 1..4
					
					// Update score in allocator's store
					UInt16 new_score = utopia->bumpCostScore(vpn_4kb, inc);
					
					// Update Top-K table
					utopia->updateCostTopK(vpn_4kb, new_score, count);
					
					// Process epoch boundaries (ranking decay, credit refill)
					// Note: epochs advance per PTW, not per all translations (see design note above)
					utopia->processEpochs(count);
					
					if (count)
						translation_stats.cost_topk_score_updates++;
					
					// Count every candidate we evaluate for migration
					if (count)
						translation_stats.cost_topk_migration_attempts++;
					
					// Attempt cost-based migration for this VPN
					auto [migrated, new_ppn] = utopia->considerCostBasedMigration(
						address, ppn_result, page_size,
						core->getThread()->getAppId(), vpn_4kb, count);
					
					if (migrated) {
						IntPtr old_ppn = ppn_result;  // Save before update
						ppn_result = new_ppn;
						migrated_by_cost_policy = true;
						
						// TLB shootdown: invalidate stale FlexSeg entries
						performTLBShootdown(address, page_size);
						
						// CRITICAL: If migration caused an eviction, invalidate evicted page's mapping FIRST
						// before updating the migrating page's mapping (they share the same RestSeg slot/PPN)
						IntPtr evicted_va = 0;
						bool had_eviction = utopia->didLastCauseEviction();
						if (had_eviction) {
							evicted_va = utopia->getLastEvictedAddress();
							int evicted_page_size = utopia->getLastEvictedPageSizeBits();  // Use correct RestSeg page size!
							
							mmu_log->debug("[SANITY] Evicted page: VA=", mmu_log->hex(evicted_va),
							              " page_size=", evicted_page_size,
							              " - will page fault on next access");
							
							// Invalidate old mapping (RestSeg PPN) BEFORE updating new mapping
							invalidateSanityCheckMaps(evicted_va, evicted_page_size);
							
							// TLB shootdown for evicted page - use EVICTED page's page size, not migrating page's
							performTLBShootdown(evicted_va, evicted_page_size);
							
							// VERIFICATION: Check that TLB entries are truly invalidated using contains()
							if (sanity_checks_enabled) {
								bool still_in_tlb = false;
								const TLBSubsystem& data_tlbs = tlb_subsystem->getDataPath();
								for (size_t level = 0; level < data_tlbs.size() && !still_in_tlb; level++) {
									for (size_t tlb_idx = 0; tlb_idx < data_tlbs[level].size() && !still_in_tlb; tlb_idx++) {
										TLB* tlb = data_tlbs[level][tlb_idx];
										// contains() returns true if entry exists - should be false after shootdown
										if (tlb && tlb->contains(evicted_va, evicted_page_size)) {
											still_in_tlb = true;
											std::cerr << "[TLB SHOOTDOWN ERROR] Evicted VA=0x" << std::hex << evicted_va
											          << " STILL in TLB after shootdown! Level=" << std::dec << level
											          << " TLB=" << tlb_idx << " page_size=" << evicted_page_size << std::endl;
										}
									}
								}
								if (still_in_tlb) {
									LOG_ASSERT_ERROR(false, "TLB shootdown failed for evicted page!");
								}
							}
						}
						
						// ================================================================
						// SANITY CHECK: Verify RestSeg state after migration
						// ================================================================
						int app_id = core->getThread()->getAppId();
						bool migrated_in_restseg = utopia->checkIsInRestSeg(address, app_id);
						
						if (!migrated_in_restseg) {
							std::cout << "[MIGRATION_SANITY_ERROR] Migrated page NOT in RestSeg!" << std::endl;
							std::cout << "  Migrated VA: 0x" << std::hex << address << std::dec << std::endl;
							std::cout << "  New PPN: 0x" << std::hex << new_ppn << std::dec << std::endl;
							LOG_ASSERT_ERROR(false, "Migration sanity check failed: migrated page not in RestSeg");
						}
						
						if (had_eviction) {
							bool evicted_in_restseg = utopia->checkIsInRestSeg(evicted_va, app_id);
							if (evicted_in_restseg) {
								std::cout << "[MIGRATION_SANITY_ERROR] Evicted page STILL in RestSeg!" << std::endl;
								std::cout << "  Evicted VA: 0x" << std::hex << evicted_va << std::dec << std::endl;
								std::cout << "  Migrated VA: 0x" << std::hex << address << std::dec << std::endl;
								LOG_ASSERT_ERROR(false, "Migration sanity check failed: evicted page still in RestSeg");
							} 
						}

						// ================================================================
						
						// Now update sanity check maps for the new VA->PA mapping
						updateSanityCheckMaps(address, old_ppn, new_ppn, page_size);
						
						// Model migration latency (same as legacy path)
						// Key is vpn_4kb for consistency with migration_queue check at top
						migration_queue[vpn_4kb] = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) + 
						                           ComponentLatency(core->getDvfsDomain(), 200).getLatency();
						
						if (count) {
							translation_stats.cost_topk_migrations_issued++;
							translation_stats.flextorest_migrations++;
						}
						
						mmu_log->debug("[COST-MIG] Migrated VA=", mmu_log->hex(address),
						              " VPN4K=", vpn_4kb,
						              " score=", new_score, " new_ppn=", mmu_log->hex(new_ppn));
					}
				}
				
				// Legacy threshold-based migration (only when CostTopK didn't handle it and legacy is enabled)
				if (!migrated_by_cost_policy && 
				    legacy_migration_enabled &&
				    utopia->getMigrationPolicyType() == Utopia::MigrationPolicyType::None)
				{
					// Legacy stats tracking (uses vpn_4kb for consistency with migration_queue)
					if (ptw_stats.find(vpn_4kb) == ptw_stats.end())
						ptw_stats[vpn_4kb] = make_pair(1, 0);
					else
						ptw_stats[vpn_4kb].first++;

					int dram_accesses_occurred = getDramAccessesDuringLastWalk();
					ptw_stats[vpn_4kb].second += dram_accesses_occurred;
					
					bool should_migrate = (ptw_stats[vpn_4kb].first >= ptw_migration_threshold) &&
					                      (ptw_stats[vpn_4kb].second >= dram_accesses_migration_threshold);

					if (should_migrate)
					{
						mmu_log->debug("[LEGACY-MIG] Migrating VA=", mmu_log->hex(address),
						              " VPN4K=", vpn_4kb, " to RestSeg");

						IntPtr old_ppn = ppn_result;  // Save before migration
						IntPtr new_ppn = utopia->migratePage(address, ppn_result, page_size, core->getThread()->getAppId());
						ppn_result = new_ppn;
						
						// TLB shootdown: invalidate stale FlexSeg entries
						performTLBShootdown(address, page_size);
						
						// CRITICAL: If migration caused an eviction, invalidate evicted page's mapping FIRST
						// before updating the migrating page's mapping (they share the same RestSeg slot/PPN)
						if (utopia->didLastCauseEviction()) {
							IntPtr evicted_va = utopia->getLastEvictedAddress();
							int evicted_page_size = utopia->getLastEvictedPageSizeBits();  // Use correct RestSeg page size!
							
							mmu_log->debug("[SANITY] Evicted page: VA=", mmu_log->hex(evicted_va),
							              " page_size=", evicted_page_size,
							              " - will page fault on next access");
							
							// Invalidate old mapping (RestSeg PPN) BEFORE updating new mapping
							invalidateSanityCheckMaps(evicted_va, evicted_page_size);
							
							// TLB shootdown for evicted page - use EVICTED page's page size
							performTLBShootdown(evicted_va, evicted_page_size);
							
							// VERIFICATION: Check that TLB entries are truly invalidated using contains()
							if (sanity_checks_enabled) {
								bool still_in_tlb = false;
								const TLBSubsystem& data_tlbs = tlb_subsystem->getDataPath();
								for (size_t level = 0; level < data_tlbs.size() && !still_in_tlb; level++) {
									for (size_t tlb_idx = 0; tlb_idx < data_tlbs[level].size() && !still_in_tlb; tlb_idx++) {
										TLB* tlb = data_tlbs[level][tlb_idx];
										// contains() returns true if entry exists - should be false after shootdown
										if (tlb && tlb->contains(evicted_va, evicted_page_size)) {
											still_in_tlb = true;
											std::cerr << "[TLB SHOOTDOWN ERROR] Evicted VA=0x" << std::hex << evicted_va
											          << " STILL in TLB after shootdown! Level=" << std::dec << level
											          << " TLB=" << tlb_idx << " page_size=" << evicted_page_size << std::endl;
										}
									}
								}
								if (still_in_tlb) {
									LOG_ASSERT_ERROR(false, "TLB shootdown failed for evicted page!");
								}
							}
						}
						
						// Now update sanity check maps for the new VA->PA mapping
						updateSanityCheckMaps(address, old_ppn, new_ppn, page_size);

						// Use vpn_4kb for migration_queue consistency
						migration_queue[vpn_4kb] = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) + 
						                           ComponentLatency(core->getDvfsDomain(), 200).getLatency();

						if (count)
							translation_stats.flextorest_migrations++;
					}
				}
			}
		}
		else if (hit)
		{
			// TLB Hit - get page size and PPN from TLB
			page_size = tlb_block_info_hit->getPageSize();
			ppn_result = tlb_block_info_hit->getPPN();

			mmu_log->debug("TLB Hit: page_size=", page_size, " PPN=", ppn_result);
		}

		// ====================================================================
		// TLB Allocation
		// ====================================================================
		const TLBSubsystem& alloc_tlbs = instruction ?
			tlb_subsystem->getInstructionPath() : tlb_subsystem->getDataPath();

		// Evicted translations: (virtual_address, page_size, ppn)
		// We must track the PPN of evicted entries to correctly propagate them to lower-level TLBs
		std::map<int, vector<tuple<IntPtr, int, IntPtr>>> evicted_translations;
		int tlb_levels = alloc_tlbs.size();

		if (tlb_subsystem->isPrefetchEnabled())
			tlb_levels = alloc_tlbs.size() - 1;

		for (int i = 0; i < tlb_levels; i++)
		{
			for (UInt32 j = 0; j < alloc_tlbs[i].size(); j++)
			{
				// Handle evicted translations from previous level
				if ((i > 0) && (evicted_translations[i - 1].size() != 0))
				{
					for (UInt32 k = 0; k < evicted_translations[i - 1].size(); k++)
					{
						IntPtr evicted_va = get<0>(evicted_translations[i - 1][k]);
						int evicted_page_size = get<1>(evicted_translations[i - 1][k]);
						IntPtr evicted_ppn = get<2>(evicted_translations[i - 1][k]);

						if (alloc_tlbs[i][j]->supportsPageSize(evicted_page_size))
						{
							TLBAllocResult alloc_result = alloc_tlbs[i][j]->allocate(
								evicted_va, time, count, lock,
								evicted_page_size, evicted_ppn);

							if (alloc_result.evicted)
							{
								// Defensive assertion: evicted PPN should be valid
								LOG_ASSERT_ERROR(alloc_result.ppn != 0, 
									"TLB eviction returned invalid PPN=0 for VA=0x%lx", alloc_result.address);
								evicted_translations[i].push_back(make_tuple(alloc_result.address, alloc_result.page_size, alloc_result.ppn));
							}
						}
					}
				}

				// Allocate current translation
				if (alloc_tlbs[i][j]->supportsPageSize(page_size) && 
				    alloc_tlbs[i][j]->getAllocateOnMiss() && 
				    (!hit || (hit && hit_level > i)))
				{
					TLBAllocResult alloc_result = alloc_tlbs[i][j]->allocate(address, time, count, lock, page_size, ppn_result);
					if (alloc_result.evicted)
					{
						// Defensive assertion: evicted PPN should be valid
						LOG_ASSERT_ERROR(alloc_result.ppn != 0, 
							"TLB eviction returned invalid PPN=0 for VA=0x%lx", alloc_result.address);
						evicted_translations[i].push_back(make_tuple(alloc_result.address, alloc_result.page_size, alloc_result.ppn));
					}
				}
			}
		}

		// ====================================================================
		// Calculate Physical Address
		// ====================================================================
		constexpr IntPtr base_page_size_in_bytes = 1ULL << 12;  // 4KB
		IntPtr page_size_in_bytes = 1ULL << page_size;
		IntPtr offset = address & (page_size_in_bytes - 1);
		IntPtr final_physical_address = (ppn_result * base_page_size_in_bytes) + offset;

		// ====================================================================
		// Update MimicOS Per-Core Stats (for adaptive policies like MPLRU)
		// ====================================================================
		if (count)
		{
			MimicOS* mimicos = Sim()->getMimicOS();
			if (mimicos && mimicos->isPerCoreStatsInitialized())
			{
				// Update with the total translation latency for this access
				// Note: l2_miss was already tracked earlier, this adds the latency
				SubsecondTime this_translation_latency = translation_stats.total_translation_latency - 
				                                         prev_total_translation_latency;
				if (this_translation_latency > SubsecondTime::Zero())
				{
					// Add the latency delta (not marking as L2 miss since we did that earlier)
					PerCoreStats* stats = mimicos->getPerCoreStatsMutable(core->getId());
					if (stats)
					{
						stats->translation_latency += this_translation_latency;
						stats->num_translations++;
					}
				}
			}
		}

		mmu_log->debug("Translation complete: VA=", mmu_log->hex(address), " -> PA=", mmu_log->hex(final_physical_address));

		// ====================================================================
		// Sanity Checks: Verify VA-PA mapping consistency
		// ====================================================================
		if (sanity_checks_enabled)
		{
			// Align to page boundary for consistency check
			IntPtr page_size_bytes = 1ULL << page_size;
			IntPtr va_page = address & ~(page_size_bytes - 1);
			IntPtr pa_page = final_physical_address & ~(page_size_bytes - 1);

			// Determine translation path for debugging
			const char* path = "UNKNOWN";
			if (hit) path = "TLB_HIT";
			else if (utlb_hit) path = "UTLB_HIT";
			else if (rsw_hit) path = "RSW_HIT";
			else path = "PTW";

			// Check 1: Same VA should always map to same PA
			auto va_it = va_to_pa_map.find(va_page);
			if (va_it != va_to_pa_map.end())
			{
				if (va_it->second != pa_page)
				{
					sanity_check_violations++;
					std::cerr << "[MMU SANITY ERROR] Core " << core->getId()
					          << " VA=0x" << std::hex << va_page
					          << " mapped to different PAs! Previous=0x" << va_it->second
					          << " Current=0x" << pa_page << std::dec 
					          << " Path=" << path
					          << " page_size=" << page_size
					          << " ppn_result=0x" << std::hex << ppn_result << std::dec
					          << std::endl;
					assert(false && "VA-PA mapping inconsistency detected!");
				}
			}
			else
			{
				// First time seeing this VA, record the mapping
				va_to_pa_map[va_page] = pa_page;
			}

			// Check 2: Each PA should be assigned to only one VA
			auto pa_it = pa_to_va_map.find(pa_page);
			if (pa_it != pa_to_va_map.end())
			{
				if (pa_it->second != va_page)
				{
					sanity_check_violations++;
					std::cerr << "[MMU SANITY ERROR] Core " << core->getId()
					          << " PA=0x" << std::hex << pa_page
					          << " assigned to multiple VAs! Previous=0x" << pa_it->second
					          << " Current=0x" << va_page << std::dec << std::endl;
					assert(false && "PA assigned to multiple VAs detected!");
				}
			}
			else
			{
				// First time seeing this PA, record the mapping
				pa_to_va_map[pa_page] = va_page;
			}
		}

		return final_physical_address;
	}

// ============================================================================
// RestSeg Walk
// ============================================================================

	std::tuple<int, IntPtr, SubsecondTime> MemoryManagementUnitUtopia::RestSegWalk(IntPtr address, bool instruction, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count)
	{
		SubsecondTime t_start = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
		IntPtr final_address = 0;
		Utopia *m_utopia = (Utopia *)Sim()->getMimicOS()->getMemoryAllocator();
		int page_size = -1;
		int app_id = core->getThread() ? core->getThread()->getAppId() : 0;
		int num_restsegs = m_utopia->getNumRestSegs();

		// ====================================================================
		// ADAPTIVE ACCESS: Determine whether to use sequential or parallel
		// If one RestSeg dominates hits (>threshold), access it first (sequential)
		// Otherwise, access both in parallel
		// NOTE: Sequential/parallel only applies when num_restsegs > 1
		// ====================================================================
		bool use_sequential = false;
		int first_restseg = 0;  // Which RestSeg to check first in sequential mode
		
		if (num_restsegs > 1 && adaptive_rsw_enabled && rsw_total_hits >= rsw_warmup_accesses)
		{
			// Check if one RestSeg dominates
			double hit_rate_0 = (rsw_total_hits > 0) ? (double)rsw_hits_per_restseg[0] / rsw_total_hits : 0.5;
			double hit_rate_1 = 1.0 - hit_rate_0;
			
			if (hit_rate_0 >= rsw_sequential_threshold)
			{
				use_sequential = true;
				first_restseg = 0;
				rsw_dominant_restseg = 0;
			}
			else if (hit_rate_1 >= rsw_sequential_threshold)
			{
				use_sequential = true;
				first_restseg = 1;
				rsw_dominant_restseg = 1;
			}
			else
			{
				rsw_dominant_restseg = -1;  // No dominant, use parallel
			}
		}

		// ====================================================================
		// FINGERPRINT-FIRST FLOW:
		// 1. Read FPA (Fingerprint Array) for RestSeg(s)
		// 2. Compare fingerprints - identify candidate ways
		// 3. Only read TAR (Tag Array) for ways with matching fingerprints
		// ====================================================================
		
		// Store per-RestSeg info
		struct RestSegAccess {
			bool hit = false;                  // Data found in this RestSeg
			bool fp_filtered = false;          // No fingerprint matches (skip TAR entirely)
			bool accessed = false;             // Whether this RestSeg was accessed
			bool fpa_cache_hit = false;        // FPA cache hit
			bool tar_cache_hit = false;        // TAR cache hit
			SubsecondTime fpa_latency = SubsecondTime::Zero();
			SubsecondTime tar_latency = SubsecondTime::Zero();
			int page_size_bits = 0;
			IntPtr physical_address = 0;
			std::vector<int> matching_ways;    // Ways with matching fingerprints
			int correct_way = -1;              // The way that actually hit (for speculation tracking)
			// For access pattern logging
			IntPtr fpa_address = 0;            // FPA address accessed
			IntPtr tar_address = 0;            // TAR address accessed
			HitWhere::where_t fpa_hit_where = HitWhere::UNKNOWN;  // Where FPA was found
			HitWhere::where_t tar_hit_where = HitWhere::UNKNOWN;  // Where TAR was found
		};
		
		std::vector<RestSegAccess> rs_access(num_restsegs);
		SubsecondTime result_latency = SubsecondTime::Zero();
		int hit_restseg = -1;
		
		// ====================================================================
		// Lambda to access a single RestSeg with PARALLEL FPA+TAR model
		// ====================================================================
		// Model: FPA and TAR both start at access_start (parallel/speculative)
		// After FPA completes, if fingerprint doesn't match → TAR is squashed
		// Critical path latency:
		//   - If TAR squashed: fpa_latency
		//   - If TAR needed: max(fpa_latency, tar_latency)
		// ====================================================================
		auto accessRestSeg = [&](int i, SubsecondTime access_start) {
			auto restseg = m_utopia->getRestSeg(i);
			rs_access[i].page_size_bits = restseg->getPageSizeBits();
			rs_access[i].accessed = true;

			// Check if data is in this RestSeg (ground truth for stats)
			// Also get the correct way for speculation tracking
			if (restseg->inRestSeg(address, app_id, count))
			{
				rs_access[i].hit = true;
				rs_access[i].physical_address = restseg->calculatePhysicalAddress(address, app_id);
				rs_access[i].correct_way = restseg->getWayForAddress(address, app_id);
			}

			// ================================================================
			// PARALLEL MODEL: Launch both FPA and TAR at access_start
			// ================================================================
			
			// --- FPA (Fingerprint Array) access ---
			UInt64 fpa_address = restseg->calculateFingerprintAddress(address, core->getId());
			rs_access[i].fpa_address = fpa_address;
			
			// Determine if FPA cache is bypassed
			bool bypass_fp_cache = disable_fp_tar_caches || disable_fp_cache_only;
			bool fpa_need_memory_access = false;
			
			if (!bypass_fp_cache)
			{
				// Use dedicated FPA cache
				UtopiaCache::where_t fpacache_hitwhere = fpa_cache->lookup((IntPtr)fpa_address, access_start, true, count);
				fpa_need_memory_access = (fpacache_hitwhere == UtopiaCache::where_t::MISS);
				if (!fpa_need_memory_access)
				{
					rs_access[i].fpa_cache_hit = true;
					rs_access[i].fpa_hit_where = HitWhere::where_t::L1_OWN;
					if (count)
					{
						translation_stats.meta_accesses_routed_to_fp_tar++;
						if (i == 0) translation_stats.fpa_cache_hits_restseg0++;
						else translation_stats.fpa_cache_hits_restseg1++;
					}
					if (access_pattern_logging_enabled)
						logMetadataAccess(address >> 12, "FPA", i, fpa_address, true, HitWhere::where_t::L1_OWN, SubsecondTime::Zero());
				}
				else
				{
					if (count) translation_stats.meta_accesses_routed_to_fp_tar++;
				}
			}
			else
			{
				// Bypass: always need memory access
				fpa_need_memory_access = true;
				if (count) translation_stats.meta_accesses_routed_to_ptw++;
			}

			// Track FPA access stats
			if (count)
			{
				if (i == 0)
					translation_stats.fpa_accesses_restseg0++;
				else
					translation_stats.fpa_accesses_restseg1++;
			}

			if (fpa_need_memory_access)
			{
				translationPacket packet;
				packet.eip = eip;
				packet.address = fpa_address;
				packet.instruction = instruction;
				packet.lock_signal = lock;
				packet.modeled = modeled;
				packet.count = count;
				// When bypassing, use PAGE_TABLE_DATA to route through PTW caches
				packet.type = bypass_fp_cache ? CacheBlockInfo::block_type_t::PAGE_TABLE_DATA 
				                               : CacheBlockInfo::block_type_t::UTOPIA_FP;

				HitWhere::where_t hit_where;
				rs_access[i].fpa_latency = accessCache(packet, access_start, false, hit_where);
				rs_access[i].fpa_hit_where = hit_where;
				
				// Log FPA access
				if (access_pattern_logging_enabled)
					logMetadataAccess(address >> 12, "FPA", i, fpa_address, false, hit_where, rs_access[i].fpa_latency);
				
				if (count)
				{
					translation_stats.total_rsw_memory_requests++;
					
					// Track where FPA access went in memory hierarchy
					if (i == 0)
					{
						if (hit_where == HitWhere::L2_OWN)
							translation_stats.fpa_hit_l2_restseg0++;
						else if (hit_where == HitWhere::NUCA_CACHE)
							translation_stats.fpa_hit_nuca_restseg0++;
						else if (hit_where == HitWhere::DRAM || hit_where == HitWhere::DRAM_LOCAL || hit_where == HitWhere::DRAM_REMOTE)
							translation_stats.fpa_hit_dram_restseg0++;
					}
					else
					{
						if (hit_where == HitWhere::L2_OWN)
							translation_stats.fpa_hit_l2_restseg1++;
						else if (hit_where == HitWhere::NUCA_CACHE)
							translation_stats.fpa_hit_nuca_restseg1++;
						else if (hit_where == HitWhere::DRAM || hit_where == HitWhere::DRAM_LOCAL || hit_where == HitWhere::DRAM_REMOTE)
							translation_stats.fpa_hit_dram_restseg1++;
					}
				}
			}

			// Track FPA latency per RestSeg
			if (count)
			{
				if (i == 0)
					translation_stats.fpa_latency_restseg0 += rs_access[i].fpa_latency;
				else
					translation_stats.fpa_latency_restseg1 += rs_access[i].fpa_latency;
			}

			// Find ways with matching fingerprints (determines if TAR is needed)
			uint8_t lookup_fp = restseg->computeLookupFingerprint(address);
			rs_access[i].matching_ways = restseg->findMatchingFingerprints(address, lookup_fp);
			rs_access[i].fp_filtered = rs_access[i].matching_ways.empty();

			// Track fingerprint filtering stats
			if (count && rs_access[i].fp_filtered)
			{
				if (i == 0)
					translation_stats.fp_filtered_restseg0++;
				else
					translation_stats.fp_filtered_restseg1++;
			}

			// ================================================================
			// TAR (Tag Array) access - PARALLEL with FPA, squashed if no FP match
			// ================================================================
			// TAR is launched speculatively at access_start (same time as FPA).
			// If fingerprint doesn't match, TAR is squashed (latency not counted).
			// If fingerprint matches, TAR completes and latency = max(fpa, tar).
			// ================================================================
			if (!rs_access[i].fp_filtered)
			{
				// Track TAR access stats
				if (count)
				{
					if (i == 0)
						translation_stats.tar_accesses_restseg0++;
					else
						translation_stats.tar_accesses_restseg1++;
				}

				UInt64 tag = restseg->calculateTagAddress(address, core->getId());
				rs_access[i].tar_address = tag;
				
				// Determine if TAR cache is bypassed
				bool bypass_tar_cache = disable_fp_tar_caches || disable_tar_cache_only;
				bool tar_need_memory_access = false;
				
				if (!bypass_tar_cache)
				{
					// Use dedicated TAR cache
					UtopiaCache::where_t tagcache_hitwhere = tar_cache->lookup((IntPtr)tag, access_start, true, count);
					tar_need_memory_access = (tagcache_hitwhere == UtopiaCache::where_t::MISS);
					if (!tar_need_memory_access)
					{
						rs_access[i].tar_cache_hit = true;
						rs_access[i].tar_hit_where = HitWhere::where_t::L1_OWN;
						if (count)
						{
							translation_stats.meta_accesses_routed_to_fp_tar++;
							if (i == 0) translation_stats.tar_cache_hits_restseg0++;
							else translation_stats.tar_cache_hits_restseg1++;
						}
						if (access_pattern_logging_enabled)
							logMetadataAccess(address >> 12, "TAR", i, tag, true, HitWhere::where_t::L1_OWN, SubsecondTime::Zero());
					}
					else
					{
						if (count) translation_stats.meta_accesses_routed_to_fp_tar++;
					}
				}
				else
				{
					// Bypass: always need memory access
					tar_need_memory_access = true;
					if (count) translation_stats.meta_accesses_routed_to_ptw++;
				}

				if (tar_need_memory_access)
				{
					translationPacket packet;
					packet.eip = eip;
					packet.address = tag;
					packet.instruction = instruction;
					packet.lock_signal = lock;
					packet.modeled = modeled;
					packet.count = count;
					// When bypassing, use PAGE_TABLE_DATA to route through PTW caches
					packet.type = bypass_tar_cache ? CacheBlockInfo::block_type_t::PAGE_TABLE_DATA 
					                                : CacheBlockInfo::block_type_t::UTOPIA_TAR;

					HitWhere::where_t hit_where;
					// TAR memory access also starts at access_start (parallel)
					rs_access[i].tar_latency = accessCache(packet, access_start, false, hit_where);
					rs_access[i].tar_hit_where = hit_where;
					
					if (access_pattern_logging_enabled)
						logMetadataAccess(address >> 12, "TAR", i, tag, false, hit_where, rs_access[i].tar_latency);
					
					if (count)
					{
						translation_stats.total_rsw_memory_requests++;
						
						if (i == 0)
						{
							if (hit_where == HitWhere::L2_OWN)
								translation_stats.tar_hit_l2_restseg0++;
							else if (hit_where == HitWhere::NUCA_CACHE)
								translation_stats.tar_hit_nuca_restseg0++;
							else if (hit_where == HitWhere::DRAM || hit_where == HitWhere::DRAM_LOCAL || hit_where == HitWhere::DRAM_REMOTE)
								translation_stats.tar_hit_dram_restseg0++;
						}
						else
						{
							if (hit_where == HitWhere::L2_OWN)
								translation_stats.tar_hit_l2_restseg1++;
							else if (hit_where == HitWhere::NUCA_CACHE)
								translation_stats.tar_hit_nuca_restseg1++;
							else if (hit_where == HitWhere::DRAM || hit_where == HitWhere::DRAM_LOCAL || hit_where == HitWhere::DRAM_REMOTE)
								translation_stats.tar_hit_dram_restseg1++;
						}
					}
				}

				// Track TAR latency per RestSeg
				if (count)
				{
					if (i == 0)
						translation_stats.tar_latency_restseg0 += rs_access[i].tar_latency;
					else
						translation_stats.tar_latency_restseg1 += rs_access[i].tar_latency;
				}
			}
			
			// ================================================================
			// Compute critical path latency for this RestSeg (PARALLEL model)
			// ================================================================
			// - If TAR squashed (fp_filtered): latency = fpa_latency
			// - If TAR needed: latency = max(fpa_latency, tar_latency)
			// ================================================================
			SubsecondTime rs_latency;
			if (rs_access[i].fp_filtered)
			{
				// TAR squashed - only FPA on critical path
				rs_latency = rs_access[i].fpa_latency;
			}
			else
			{
				// TAR needed - parallel critical path
				rs_latency = max(rs_access[i].fpa_latency, rs_access[i].tar_latency);
			}
			return rs_latency;
		};

		// ====================================================================
		// Lambda to access a single RestSeg using RADIX WAY TABLE
		// ====================================================================
		// NEW: Replaces FPA+TAR with radix tree walk for VPN → {valid, way}
		// Model: Walk radix tree levels (typically 3-4 memory accesses)
		// Each level is one cache line fetch, potentially cached
		// ====================================================================
		auto accessRestSegRadix = [&](int i, SubsecondTime access_start) {
			auto restseg = m_utopia->getRestSeg(i);
			rs_access[i].page_size_bits = restseg->getPageSizeBits();
			rs_access[i].accessed = true;

			// Radix lookup: directly get way from radix table
			int way_out = -1;
			bool radix_hit = restseg->lookupWayRadix(address, app_id, way_out);
			
			if (radix_hit && way_out >= 0)
			{
				rs_access[i].hit = true;
				rs_access[i].correct_way = way_out;
				rs_access[i].physical_address = restseg->calculatePPNFromWay(address, way_out);
				
				// Touch LRU for this way
				restseg->touchWayLRU(address, way_out);
			}

			// Track radix stats
			if (count)
			{
				if (i == 0)
				{
					translation_stats.radix_lookups_restseg0++;
					if (radix_hit) translation_stats.radix_hits_restseg0++;
					else translation_stats.radix_misses_restseg0++;
				}
				else
				{
					translation_stats.radix_lookups_restseg1++;
					if (radix_hit) translation_stats.radix_hits_restseg1++;
					else translation_stats.radix_misses_restseg1++;
				}
			}

			// ================================================================
			// Model radix walk latency: one cache line fetch per level
			// Uses PWC (page walk cache) as filter, then cache hierarchy on miss
			// ================================================================
			SubsecondTime radix_latency = SubsecondTime::Zero();
			
			// Get addresses for each radix level access (internal nodes + leaf)
			// Uses actual simulated physical addresses from radix node allocation
			std::vector<std::pair<IntPtr, bool>> level_addresses;  // (addr, is_leaf)
			restseg->calculateRadixLevelAddresses(address, app_id, level_addresses);
			
			// Access each level (sequential: each level depends on previous)
			SubsecondTime current_time = access_start;
			for (size_t level_idx = 0; level_idx < level_addresses.size(); level_idx++)
			{
				IntPtr level_addr = level_addresses[level_idx].first;
				bool is_leaf = level_addresses[level_idx].second;
				
				// First, check PWC (page walk cache) - reuses PTW filter infrastructure
				bool pwc_hit = ptw_filter->lookupPWC(level_addr, current_time, level_idx, count);
				
				// Track PWC hits/misses
				if (count)
				{
					if (pwc_hit)
						translation_stats.radix_pwc_hits++;
					else
						translation_stats.radix_pwc_misses++;
				}
				
				if (!pwc_hit)
				{
					// PWC miss: access cache hierarchy
					translationPacket packet;
					packet.eip = eip;
					packet.address = level_addr;
					packet.instruction = instruction;
					packet.lock_signal = lock;
					packet.modeled = modeled;
					packet.count = count;
					
					// Use UTOPIA_RADIX_INTERNAL/LEAF for cache tracking
					packet.type = is_leaf ? CacheBlockInfo::block_type_t::UTOPIA_RADIX_LEAF : 
					                        CacheBlockInfo::block_type_t::UTOPIA_RADIX_INTERNAL;

					HitWhere::where_t hit_where;
					SubsecondTime level_latency = accessCache(packet, current_time, false, hit_where);
					radix_latency += level_latency;
					current_time = current_time + level_latency;
					
					if (count) translation_stats.total_rsw_memory_requests++;
					
					// Track radix internal/leaf memory hierarchy hits
					if (count)
					{
						if (is_leaf)
						{
							translation_stats.radix_leaf_accesses++;
							if (hit_where == HitWhere::L2_OWN)
								translation_stats.radix_leaf_hit_l2++;
							else if (hit_where == HitWhere::NUCA_CACHE || hit_where == HitWhere::L3_OWN)
								translation_stats.radix_leaf_hit_nuca++;
							else if (hit_where == HitWhere::DRAM || hit_where == HitWhere::DRAM_LOCAL || 
							         hit_where == HitWhere::DRAM_REMOTE || hit_where == HitWhere::MISS)
								translation_stats.radix_leaf_hit_dram++;
						}
						else
						{
							translation_stats.radix_internal_accesses++;
							if (hit_where == HitWhere::L2_OWN)
								translation_stats.radix_internal_hit_l2++;
							else if (hit_where == HitWhere::NUCA_CACHE || hit_where == HitWhere::L3_OWN)
								translation_stats.radix_internal_hit_nuca++;
							else if (hit_where == HitWhere::DRAM || hit_where == HitWhere::DRAM_LOCAL || 
							         hit_where == HitWhere::DRAM_REMOTE || hit_where == HitWhere::MISS)
								translation_stats.radix_internal_hit_dram++;
						}
					}
				}
				// PWC hit: no additional latency (handled by PWC access penalty if any)
			}
			
			// Track radix latency per RestSeg
			if (count)
			{
				if (i == 0)
					translation_stats.radix_latency_restseg0 += radix_latency;
				else
					translation_stats.radix_latency_restseg1 += radix_latency;
			}

			return radix_latency;
		};

		// ====================================================================
		// Choose access method based on radix_lookup_enabled
		// ====================================================================
		auto accessRestSegDispatch = [&](int i, SubsecondTime access_start) {
			if (radix_lookup_enabled) {
				return accessRestSegRadix(i, access_start);
			} else {
				return accessRestSeg(i, access_start);
			}
		};

		// ====================================================================
		// SINGLE RESTSEG MODE: Direct access, no sequential/parallel logic
		// ====================================================================
		if (num_restsegs == 1)
		{
			result_latency = accessRestSegDispatch(0, t_start);
			if (rs_access[0].hit)
			{
				hit_restseg = 0;
				page_size = rs_access[0].page_size_bits;
				final_address = rs_access[0].physical_address;
			}
		}
		// ====================================================================
		// SEQUENTIAL ACCESS MODE: Check dominant RestSeg first (num_restsegs > 1)
		// ====================================================================
		else if (use_sequential)
		{
			if (count)
				translation_stats.rsw_sequential_accesses++;
			
			int second_restseg = (first_restseg == 0) ? 1 : 0;
			
			// Access dominant RestSeg first
			SubsecondTime first_latency = accessRestSegDispatch(first_restseg, t_start);
			
			if (rs_access[first_restseg].hit)
			{
				// Hit in dominant RestSeg - we're done!
				hit_restseg = first_restseg;
				page_size = rs_access[first_restseg].page_size_bits;
				final_address = rs_access[first_restseg].physical_address;
				result_latency = first_latency;
			}
			else
			{
				// Miss in dominant RestSeg - need to check the other one
				SubsecondTime t_second = t_start + first_latency;
				SubsecondTime second_latency = accessRestSegDispatch(second_restseg, t_second);
				
				if (rs_access[second_restseg].hit)
				{
					hit_restseg = second_restseg;
					page_size = rs_access[second_restseg].page_size_bits;
					final_address = rs_access[second_restseg].physical_address;
				}
				// Total latency = first + second (sequential)
				result_latency = first_latency + second_latency;
			}
		}
		// ====================================================================
		// PARALLEL ACCESS MODE: Access all RestSegs simultaneously (num_restsegs > 1)
		// ====================================================================
		else
		{
			if (count)
				translation_stats.rsw_parallel_accesses++;
			
			// Launch ALL RestSeg accesses in parallel (simulate by computing all latencies from t_start)
			// Note: With radix, this means radix walks for both RestSegs start simultaneously
			std::vector<SubsecondTime> rs_lat(num_restsegs);
			for (int i = 0; i < num_restsegs; i++)
			{
				rs_lat[i] = accessRestSegDispatch(i, t_start);
			}

			// For FPA+TAR mode (radix_lookup_enabled = false), recompute latency based on FPA/TAR model
			// For radix mode, rs_lat already contains the correct latency from radix walk
			if (!radix_lookup_enabled)
			{
				// F1) Compute per-RestSeg completion latency (PARALLEL FPA+TAR model)
				// - If TAR squashed: rs_lat = fpa_latency
				// - If TAR needed: rs_lat = max(fpa_latency, tar_latency)
				for (int i = 0; i < num_restsegs; i++)
				{
					if (rs_access[i].fp_filtered)
					{
						// TAR squashed - only FPA on critical path
						rs_lat[i] = rs_access[i].fpa_latency;
					}
					else
					{
						// TAR needed - parallel critical path
						rs_lat[i] = max(rs_access[i].fpa_latency, rs_access[i].tar_latency);
					}
				}
			}

			// F2) Find which RestSeg (if any) has a hit
			for (int i = 0; i < num_restsegs; i++)
			{
				if (rs_access[i].hit)
				{
					hit_restseg = i;
					page_size = rs_access[i].page_size_bits;
					final_address = rs_access[i].physical_address;
					break;
				}
			}

			// F3) Model A: Translation completes when hitting segment completes
			// (don't wait for other segments)
			if (hit_restseg >= 0)
			{
				// Hit case: latency = hitting segment's latency only
				result_latency = rs_lat[hit_restseg];
			}
			else
			{
				// Miss case: wait for all RestSegs (max latency)
				for (int i = 0; i < num_restsegs; i++)
				{
					result_latency = max(result_latency, rs_lat[i]);
				}
			}
		}

		// ====================================================================
		// Update hit tracking for adaptive access (used for next access decision)
		// ====================================================================
		if (hit_restseg >= 0)
		{
			rsw_total_hits++;
			rsw_hits_per_restseg[hit_restseg]++;
			
			if (count)
			{
				if (hit_restseg == 0)
					translation_stats.rsw_hits_restseg0++;
				else
					translation_stats.rsw_hits_restseg1++;
			}
			
			// NOTE: Legacy fp_speculation tracking removed - CATS handles speculation stats now
		}

		// Logging now happens inside accessRestSeg() via logMetadataAccess()
		// which uses a consistent schema for all FPA/TAR accesses

		return make_tuple(page_size, final_address, result_latency);
	}

// ============================================================================
// Functional RestSeg Lookup (no cache accesses, no latency)
// ============================================================================

	RSWLookupResult MemoryManagementUnitUtopia::functionalRSWLookup(IntPtr address)
	{
		RSWLookupResult result;
		result.virtual_address = address;  // Phase 4: Store original VA for logging
		
		Utopia *m_utopia = (Utopia *)Sim()->getMimicOS()->getMemoryAllocator();
		int app_id = core->getThread() ? core->getThread()->getAppId() : 0;
		int num_restsegs = m_utopia->getNumRestSegs();

		// Check each RestSeg functionally (no cache accesses)
		for (int i = 0; i < num_restsegs; i++)
		{
			auto restseg = m_utopia->getRestSeg(i);
			if (!restseg) continue;

			// Functional check - does NOT access cache, does NOT update stats
			if (restseg->inRestSeg(address, app_id, false /* count_stats */))
			{
				result.hit = true;
				result.seg_id = i;
				result.way = restseg->getWayForAddress(address, app_id);
				result.page_size_bits = restseg->getPageSizeBits();
				result.physical_address = restseg->calculatePhysicalAddress(address, app_id);
				
				// Calculate PPN from physical address
				constexpr IntPtr base_page_size = 4096;
				result.ppn = result.physical_address / base_page_size;
				
				// Store metadata addresses for later access
				result.fpa_address = restseg->calculateFingerprintAddress(address, core->getId());
				result.tar_address = restseg->calculateTagAddress(address, core->getId());
				
				// Compute set index for CATS table lookups
				result.set_index = (address >> result.page_size_bits) % restseg->getNumSets();
				
				// For CATS: compute candidate ways (fingerprint-matching ways)
				// This doesn't access cache, just simulates FP matching
				uint8_t lookup_fp = restseg->computeLookupFingerprint(address);
				std::vector<int> matching_ways = restseg->findMatchingFingerprints(address, lookup_fp);
				
				result.num_candidates = std::min((int)matching_ways.size(), RSWLookupResult::MAX_CANDIDATES);
				for (int c = 0; c < result.num_candidates; c++)
				{
					result.candidate_ways[c] = matching_ways[c];
					result.candidate_pas[c] = restseg->calculatePhysicalAddressForWay(address, matching_ways[c]);
				}
				
				mmu_log->debug("Functional RSW lookup HIT: seg_id=", i, " way=", result.way, 
				               " num_candidates=", result.num_candidates);
				return result;
			}
		}

		// ================================================================
		// Phase 1: Cold speculation support - compute candidates on MISS
		// ================================================================
		// Even though the page isn't in RestSeg, we can still find fingerprint
		// matches. These candidates can be used for cold speculation after PTW.
		// Fix C: Loop over all RestSegs and pick the one with max candidates
		// to reduce noise in the predictor/confidence tables.
		int best_seg = -1;
		int best_num_candidates = 0;
		std::vector<int> best_matching_ways;
		
		for (int i = 0; i < num_restsegs; i++)
		{
			auto restseg = m_utopia->getRestSeg(i);
			if (!restseg) continue;
			
			// Compute fingerprint for this address
			uint8_t lookup_fp = restseg->computeLookupFingerprint(address);
			std::vector<int> matching_ways = restseg->findMatchingFingerprints(address, lookup_fp);
			
			int num_cands = (int)matching_ways.size();
			if (num_cands > best_num_candidates)
			{
				best_seg = i;
				best_num_candidates = num_cands;
				best_matching_ways = std::move(matching_ways);
			}
			// Tie-break: prefer smaller seg_id (already handled by > instead of >=)
		}
		
		// Fill result from best RestSeg
		if (best_seg >= 0)
		{
			auto restseg = m_utopia->getRestSeg(best_seg);
			
			result.seg_id = best_seg;
			result.page_size_bits = restseg->getPageSizeBits();
			result.set_index = (address >> result.page_size_bits) % restseg->getNumSets();
			
			// Store metadata addresses (for potential future use)
			result.fpa_address = restseg->calculateFingerprintAddress(address, core->getId());
			result.tar_address = restseg->calculateTagAddress(address, core->getId());
			
			result.num_candidates = std::min(best_num_candidates, RSWLookupResult::MAX_CANDIDATES);
			for (int c = 0; c < result.num_candidates; c++)
			{
				result.candidate_ways[c] = best_matching_ways[c];
				result.candidate_pas[c] = restseg->calculatePhysicalAddressForWay(address, best_matching_ways[c]);
			}
			
			mmu_log->debug("Functional RSW lookup MISS but found ", result.num_candidates, 
			               " candidates in RestSeg ", best_seg, " (best of all)");
		}

		mmu_log->debug("Functional RSW lookup MISS, num_candidates=", result.num_candidates);
		return result;  // hit = false, but may have candidates for cold speculation
	}

// ============================================================================
// Charge RestSeg Walk Latency (performs cache accesses)
// ============================================================================

	SubsecondTime MemoryManagementUnitUtopia::chargeRSWLatency(const RSWLookupResult& lookup_result, 
	                                                           bool instruction, IntPtr eip, 
	                                                           Core::lock_signal_t lock, 
	                                                           bool modeled, bool count)
	{
		SubsecondTime t_start = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
		SubsecondTime total_latency = SubsecondTime::Zero();
		
		// Phase 1 fix: chargeRSWLatency should only be called when we know there's a hit
		if (!lookup_result.hit)
		{
			// This should never happen - caller should check hit before calling
			assert(false && "chargeRSWLatency called on miss - caller must check hit first");
			return SubsecondTime::Zero();
		}

		[[maybe_unused]] Utopia *m_utopia = (Utopia *)Sim()->getMimicOS()->getMemoryAllocator();
		
		// ================================================================
		// BASELINE MODE ONLY (Phase 0: TAG_IN_ROW/SIDECAR removed)
		// Separate FPA + TAR accesses
		// ================================================================
		
		// Determine if caches are bypassed
		bool bypass_fp_cache = disable_fp_tar_caches || disable_fp_cache_only;
		bool bypass_tar_cache = disable_fp_tar_caches || disable_tar_cache_only;
		
		// Access FPA (Fingerprint Array)
		SubsecondTime fpa_latency = SubsecondTime::Zero();
		HitWhere::where_t fpa_hit_where = HitWhere::UNKNOWN;
		bool fpa_need_memory_access = false;
		
		if (!bypass_fp_cache)
		{
			UtopiaCache::where_t fpacache_hitwhere = fpa_cache->lookup((IntPtr)lookup_result.fpa_address, t_start, true, count);
			fpa_need_memory_access = (fpacache_hitwhere == UtopiaCache::where_t::MISS);
			if (!fpa_need_memory_access)
			{
				if (count)
				{
					translation_stats.meta_accesses_routed_to_fp_tar++;
					if (lookup_result.seg_id == 0) translation_stats.fpa_cache_hits_restseg0++;
					else translation_stats.fpa_cache_hits_restseg1++;
				}
				UInt64 vpn = lookup_result.virtual_address >> 12;
				if (access_pattern_logging_enabled)
					logMetadataAccess(vpn, "FPA", lookup_result.seg_id, lookup_result.fpa_address, 
					                  true, HitWhere::UNKNOWN, fpa_latency);
			}
			else
			{
				if (count) translation_stats.meta_accesses_routed_to_fp_tar++;
			}
		}
		else
		{
			fpa_need_memory_access = true;
			if (count) translation_stats.meta_accesses_routed_to_ptw++;
		}
		
		if (count)
		{
			if (lookup_result.seg_id == 0)
				translation_stats.fpa_accesses_restseg0++;
			else
				translation_stats.fpa_accesses_restseg1++;
		}

		if (fpa_need_memory_access)
		{
			translationPacket packet;
			packet.eip = eip;
			packet.address = lookup_result.fpa_address;
			packet.instruction = instruction;
			packet.lock_signal = lock;
			packet.modeled = modeled;
			packet.count = count;
			packet.type = bypass_fp_cache ? CacheBlockInfo::block_type_t::PAGE_TABLE_DATA 
			                               : CacheBlockInfo::block_type_t::UTOPIA_FP;

			fpa_latency = accessCache(packet, t_start, false, fpa_hit_where);
			
			if (count)
			{
				translation_stats.total_rsw_memory_requests++;
				
				if (lookup_result.seg_id == 0)
				{
					if (fpa_hit_where == HitWhere::L2_OWN)
						translation_stats.fpa_hit_l2_restseg0++;
					else if (fpa_hit_where == HitWhere::NUCA_CACHE)
						translation_stats.fpa_hit_nuca_restseg0++;
					else if (fpa_hit_where == HitWhere::DRAM || fpa_hit_where == HitWhere::DRAM_LOCAL || fpa_hit_where == HitWhere::DRAM_REMOTE)
						translation_stats.fpa_hit_dram_restseg0++;
				}
				else
				{
					if (fpa_hit_where == HitWhere::L2_OWN)
						translation_stats.fpa_hit_l2_restseg1++;
					else if (fpa_hit_where == HitWhere::NUCA_CACHE)
						translation_stats.fpa_hit_nuca_restseg1++;
					else if (fpa_hit_where == HitWhere::DRAM || fpa_hit_where == HitWhere::DRAM_LOCAL || fpa_hit_where == HitWhere::DRAM_REMOTE)
						translation_stats.fpa_hit_dram_restseg1++;
				}
			}
			
			UInt64 vpn = lookup_result.virtual_address >> 12;
			if (access_pattern_logging_enabled)
				logMetadataAccess(vpn, "FPA", lookup_result.seg_id, lookup_result.fpa_address, 
				                  false, fpa_hit_where, fpa_latency);
		}

		if (count)
		{
			if (lookup_result.seg_id == 0)
				translation_stats.fpa_latency_restseg0 += fpa_latency;
			else
				translation_stats.fpa_latency_restseg1 += fpa_latency;
		}

		total_latency += fpa_latency;

		// Access TAR (Tag Array)
		SubsecondTime tar_latency = SubsecondTime::Zero();
		HitWhere::where_t tar_hit_where = HitWhere::UNKNOWN;
		bool tar_need_memory_access = false;
		
		if (!bypass_tar_cache)
		{
			UtopiaCache::where_t tarcache_hitwhere = tar_cache->lookup((IntPtr)lookup_result.tar_address, t_start + fpa_latency, true, count);
			tar_need_memory_access = (tarcache_hitwhere == UtopiaCache::where_t::MISS);
			if (!tar_need_memory_access)
			{
				if (count)
				{
					translation_stats.meta_accesses_routed_to_fp_tar++;
					if (lookup_result.seg_id == 0) translation_stats.tar_cache_hits_restseg0++;
					else translation_stats.tar_cache_hits_restseg1++;
				}
				UInt64 vpn = lookup_result.virtual_address >> 12;
				if (access_pattern_logging_enabled)
					logMetadataAccess(vpn, "TAR", lookup_result.seg_id, lookup_result.tar_address, 
					                  true, HitWhere::UNKNOWN, tar_latency);
			}
			else
			{
				if (count) translation_stats.meta_accesses_routed_to_fp_tar++;
			}
		}
		else
		{
			tar_need_memory_access = true;
			if (count) translation_stats.meta_accesses_routed_to_ptw++;
		}

		if (count)
		{
			if (lookup_result.seg_id == 0)
				translation_stats.tar_accesses_restseg0++;
			else
				translation_stats.tar_accesses_restseg1++;
		}

		if (tar_need_memory_access)
		{
			translationPacket packet;
			packet.eip = eip;
			packet.address = lookup_result.tar_address;
			packet.instruction = instruction;
			packet.lock_signal = lock;
			packet.modeled = modeled;
			packet.count = count;
			packet.type = bypass_tar_cache ? CacheBlockInfo::block_type_t::PAGE_TABLE_DATA 
			                                : CacheBlockInfo::block_type_t::UTOPIA_TAR;

			tar_latency = accessCache(packet, t_start + fpa_latency, false, tar_hit_where);
			
			if (count)
			{
				translation_stats.total_rsw_memory_requests++;
				
				if (lookup_result.seg_id == 0)
				{
					if (tar_hit_where == HitWhere::L2_OWN)
						translation_stats.tar_hit_l2_restseg0++;
					else if (tar_hit_where == HitWhere::NUCA_CACHE)
						translation_stats.tar_hit_nuca_restseg0++;
					else if (tar_hit_where == HitWhere::DRAM || tar_hit_where == HitWhere::DRAM_LOCAL || tar_hit_where == HitWhere::DRAM_REMOTE)
						translation_stats.tar_hit_dram_restseg0++;
				}
				else
				{
					if (tar_hit_where == HitWhere::L2_OWN)
						translation_stats.tar_hit_l2_restseg1++;
					else if (tar_hit_where == HitWhere::NUCA_CACHE)
						translation_stats.tar_hit_nuca_restseg1++;
					else if (tar_hit_where == HitWhere::DRAM || tar_hit_where == HitWhere::DRAM_LOCAL || tar_hit_where == HitWhere::DRAM_REMOTE)
						translation_stats.tar_hit_dram_restseg1++;
				}
			}
			
			UInt64 vpn = lookup_result.virtual_address >> 12;
			if (access_pattern_logging_enabled)
				logMetadataAccess(vpn, "TAR", lookup_result.seg_id, lookup_result.tar_address, 
				                  false, tar_hit_where, tar_latency);
		}

		if (count)
		{
			if (lookup_result.seg_id == 0)
				translation_stats.tar_latency_restseg0 += tar_latency;
			else
				translation_stats.tar_latency_restseg1 += tar_latency;
		}

		total_latency += tar_latency;

		// Phase 2 fix: Hit counters removed from here - caller handles hit tracking
		// to avoid double counting between chargeRSWLatency and RestSegWalk paths

		return total_latency;
	}

// ============================================================================
// TLB Shootdown
// ============================================================================

	void MemoryManagementUnitUtopia::performTLBShootdown(IntPtr address, int page_size)
	{
		// Invalidate entries in all TLB levels (both data and instruction paths)
		// This is called after a page is migrated from FlexSeg to RestSeg
		
		
		mmu_log->debug("TLB shootdown for address=", mmu_log->hex(address), " page_size=", page_size);
		
		translation_stats.tlb_shootdowns++;
		
		int total_invalidated = 0;
		
		// Get data path TLBs and invalidate ALL TLBs unconditionally
		// Note: We removed the supportsPageSize check because a TLB may have
		// cached an entry for a page size that it doesn't officially "support"
		// (e.g., multi-page-size TLBs). The invalidate function will simply
		// return false if there's nothing to invalidate.
		const TLBSubsystem& data_tlbs = tlb_subsystem->getDataPath();
		for (size_t level = 0; level < data_tlbs.size(); level++)
		{
			for (size_t tlb_idx = 0; tlb_idx < data_tlbs[level].size(); tlb_idx++)
			{
				TLB* tlb = data_tlbs[level][tlb_idx];
				if (tlb)
				{
					bool invalidated = tlb->invalidate(address, page_size);
					if (invalidated) total_invalidated++;
				}
			}
		}
		
		// Get instruction path TLBs and invalidate ALL TLBs unconditionally
		const TLBSubsystem& inst_tlbs = tlb_subsystem->getInstructionPath();
		for (size_t level = 0; level < inst_tlbs.size(); level++)
		{
			for (size_t tlb_idx = 0; tlb_idx < inst_tlbs[level].size(); tlb_idx++)
			{
				TLB* tlb = inst_tlbs[level][tlb_idx];
				if (tlb)
				{
					bool invalidated = tlb->invalidate(address, page_size);

					if (invalidated) total_invalidated++;
				}
			}
		}
		
		// Also invalidate UTLB if enabled (though migrated pages shouldn't be in UTLB yet)
		if (utlb_enabled && utlb)
		{
			bool invalidated = utlb->invalidate(address, page_size);

			if (invalidated) total_invalidated++;
		}
		

		// ====================================================================
		// CRITICAL: Invalidate TAR and FPA cache entries for the migrated page
		// ====================================================================
		// When a page migrates from RestSeg to FlexSeg (or vice versa), stale 
		// TAR/FPA cache entries could cause incorrect VA->PA translations.
		// We must invalidate entries for ALL RestSegs since the page could have
		// been in any of them.
		// ====================================================================
		Utopia *utopia = dynamic_cast<Utopia *>(Sim()->getMimicOS()->getMemoryAllocator());
		if (utopia)
		{
			int num_restsegs = utopia->getNumRestSegs();
			for (int i = 0; i < num_restsegs; i++)
			{
				auto restseg = utopia->getRestSeg(i);
				if (restseg)
				{
					// Compute and invalidate FPA cache entry
					UInt64 fpa_address = restseg->calculateFingerprintAddress(address, core->getId());
					if (fpa_cache && fpa_cache->invalidate((IntPtr)fpa_address))
					{
						mmu_log->debug("  Invalidated FPA cache entry for RestSeg ", i, 
						              " FPA addr=", mmu_log->hex(fpa_address));
					}
					
					// Compute and invalidate TAR cache entry
					UInt64 tar_address = restseg->calculateTagAddress(address, core->getId());
					if (tar_cache && tar_cache->invalidate((IntPtr)tar_address))
					{
						mmu_log->debug("  Invalidated TAR cache entry for RestSeg ", i,
						              " TAR addr=", mmu_log->hex(tar_address));
					}
				}
			}
		}
	}

// ============================================================================
// Sanity Check Map Update (after migration)
// ============================================================================

	void MemoryManagementUnitUtopia::updateSanityCheckMaps(IntPtr address, IntPtr old_ppn, IntPtr new_ppn, int page_size)
	{
		if (!sanity_checks_enabled)
			return;

		// Compute page-aligned addresses
		// NOTE: PPN is stored in 4KB granularity (base_page_size = 4KB = 1 << 12)
		// The physical address calculation in performAddressTranslation uses:
		//   final_physical_address = (ppn_result * base_page_size_in_bytes) + offset
		// And the sanity check computes pa_page as:
		//   pa_page = final_physical_address & ~(page_size_bytes - 1)
		// So we need to use the same formula here for consistency.
		constexpr IntPtr base_page_size_in_bytes = 1ULL << 12;  // 4KB
		IntPtr page_size_bytes = 1ULL << page_size;
		IntPtr va_page = address & ~(page_size_bytes - 1);
		
		// Compute physical address the same way as performAddressTranslation
		IntPtr offset = address & (page_size_bytes - 1);
		IntPtr old_pa = (old_ppn * base_page_size_in_bytes) + offset;
		IntPtr new_pa = (new_ppn * base_page_size_in_bytes) + offset;
		IntPtr old_pa_page = old_pa & ~(page_size_bytes - 1);
		IntPtr new_pa_page = new_pa & ~(page_size_bytes - 1);

		mmu_log->debug("[SANITY] Updating maps for migration: VA=", mmu_log->hex(va_page),
		              " old_PA=", mmu_log->hex(old_pa_page), " new_PA=", mmu_log->hex(new_pa_page));

		// Update VA->PA mapping: same VA now maps to new PA
		va_to_pa_map[va_page] = new_pa_page;

		// Remove old PA->VA mapping and add new one
		auto old_pa_it = pa_to_va_map.find(old_pa_page);
		if (old_pa_it != pa_to_va_map.end())
		{
			pa_to_va_map.erase(old_pa_it);
		}
		pa_to_va_map[new_pa_page] = va_page;
	}

	void MemoryManagementUnitUtopia::invalidateSanityCheckMaps(IntPtr address, int page_size)
	{
		if (!sanity_checks_enabled)
			return;

		// Compute page-aligned VA
		IntPtr page_size_bytes = 1ULL << page_size;
		IntPtr va_page = address & ~(page_size_bytes - 1);

		mmu_log->debug("[SANITY] Invalidating maps for evicted VA=", mmu_log->hex(va_page));

		// Remove VA->PA mapping
		auto va_it = va_to_pa_map.find(va_page);
		if (va_it != va_to_pa_map.end())
		{
			// Also remove the reverse PA->VA mapping
			IntPtr old_pa = va_it->second;
			auto pa_it = pa_to_va_map.find(old_pa);
			if (pa_it != pa_to_va_map.end())
			{
				pa_to_va_map.erase(pa_it);
			}
			va_to_pa_map.erase(va_it);
		}
	}

// ============================================================================
// Metadata Access Logging
// ============================================================================

	void MemoryManagementUnitUtopia::logMetadataAccess(UInt64 vpn, const char* access_type, int restseg_id,
	                                                   IntPtr address, bool utopia_cache_hit, 
	                                                   HitWhere::where_t hit_where, SubsecondTime latency)
	{
		// Only log if FPA/TAR logging is enabled
		if (!access_pattern_log || !access_pattern_log->is_open())
			return;
		
		// Fix A: Increment counter BEFORE sampling check to avoid stopping after first log
		++access_pattern_counter;
		
		// Apply sampling rate (log every N-th access where N = sample_rate)
		if (access_pattern_sample_rate > 1 && (access_pattern_counter % access_pattern_sample_rate) != 0)
			return;
		
		IntPtr cache_line_addr = address >> 6;  // Cache line granularity
		
		// Determine hit location booleans
		bool l2_hit = false, nuca_hit = false, dram_hit = false;
		if (!utopia_cache_hit) {
			l2_hit = (hit_where == HitWhere::L2_OWN);
			nuca_hit = (hit_where == HitWhere::NUCA_CACHE);
			dram_hit = (hit_where == HitWhere::DRAM || hit_where == HitWhere::DRAM_LOCAL || 
			            hit_where == HitWhere::DRAM_REMOTE);
		}
		
		// Format: access_num,vpn,access_type,restseg_id,cache_line_addr,utopia_cache_hit,l2_hit,nuca_hit,dram_hit,latency_ns
		*access_pattern_log << access_pattern_counter << ","
		                   << vpn << ","
		                   << access_type << ","
		                   << restseg_id << ","
		                   << cache_line_addr << ","
		                   << (utopia_cache_hit ? 1 : 0) << ","
		                   << (l2_hit ? 1 : 0) << ","
		                   << (nuca_hit ? 1 : 0) << ","
		                   << (dram_hit ? 1 : 0) << ","
		                   << latency.getNS() << std::endl;
	}

	void MemoryManagementUnitUtopia::logPTWCacheAccess(UInt64 ptw_id, UInt64 vpn, int level, int table,
	                                                   IntPtr cache_line_addr, HitWhere::where_t hit_where,
	                                                   SubsecondTime latency, bool is_pte)
	{
		// Only log if PTW logging is enabled
		if (!ptw_access_log || !ptw_access_log->is_open())
			return;
		
		// Apply sampling rate using PTW counter (defensive: guard against sample_rate=0)
		++ptw_access_counter;
		if (access_pattern_sample_rate > 1 && (ptw_access_counter % access_pattern_sample_rate) != 0)
			return;
		
		// Determine hit location booleans
		bool l1_hit = (hit_where == HitWhere::L1_OWN);
		bool l2_hit = (hit_where == HitWhere::L2_OWN);
		bool nuca_hit = (hit_where == HitWhere::NUCA_CACHE);
		bool dram_hit = (hit_where == HitWhere::DRAM || hit_where == HitWhere::DRAM_LOCAL || 
		                 hit_where == HitWhere::DRAM_REMOTE);
		
		// Format: ptw_num,vpn,level,table,cache_line_addr,l1_hit,l2_hit,nuca_hit,dram_hit,latency_ns,is_pte
		*ptw_access_log << ptw_id << ","
		               << vpn << ","
		               << level << ","
		               << table << ","
		               << cache_line_addr << ","
		               << (l1_hit ? 1 : 0) << ","
		               << (l2_hit ? 1 : 0) << ","
		               << (nuca_hit ? 1 : 0) << ","
		               << (dram_hit ? 1 : 0) << ","
		               << latency.getNS() << ","
		               << (is_pte ? 1 : 0) << std::endl;
	}

// ============================================================================
// PTW Filter
// ============================================================================

	PTWResult MemoryManagementUnitUtopia::filterPTWResult(IntPtr virtual_address, PTWResult ptw_result, PageTable *page_table, bool count)
	{
		return ptw_filter->filterPTWResult(virtual_address, ptw_result, page_table, count);
	}

// ============================================================================
// VMA Discovery (placeholder)
// ============================================================================

	void MemoryManagementUnitUtopia::discoverVMAs()
	{
		// Can be implemented to discover Virtual Memory Areas
	}

// ============================================================================
// CATS (Confidence-Aware Throttled Speculation) Helper Methods
// ============================================================================

	UInt64 MemoryManagementUnitUtopia::computeSpecTableKey(int seg_id, int set_index)
	{
		// Simple hash combining segment ID and set index
		// XOR folding to distribute well across table
		UInt64 key = ((UInt64)seg_id << 16) ^ (UInt64)set_index;
		return key;
	}

	MemoryManagementUnitUtopia::SpecCtrl::ConfEntry& MemoryManagementUnitUtopia::lookupConfEntry(UInt64 key)
	{
		UInt64 index = key % spec.conf_table.size();
		UInt16 tag = (UInt16)((key >> 16) & 0xFFFF);
		
		SpecCtrl::ConfEntry& entry = spec.conf_table[index];
		
		// Check tag match, if not allocate new entry
		if (entry.tag != tag)
		{
			entry.tag = tag;
			entry.conf = 0;  // Start with zero confidence
		}
		
		return entry;
	}

	int MemoryManagementUnitUtopia::lookupWayPredictor(UInt64 key)
	{
		if (!spec.way_predictor_enabled) return -1;
		
		UInt64 index = key % spec.way_table.size();
		UInt16 tag = (UInt16)((key >> 16) & 0xFFFF);
		
		SpecCtrl::WayEntry& entry = spec.way_table[index];
		
		// Check tag match
		if (entry.tag == tag && entry.usefulness > 0)
		{
			// Note: cats_way_predictions++ is counted in decideSpeculation() when
			// the prediction actually matches a candidate. Don't double-count here.
			return entry.last_way;
		}
		
		return -1;  // No prediction available
	}

	SpecDecision MemoryManagementUnitUtopia::decideSpeculation(const RSWLookupResult& lr, bool instruction, SubsecondTime now, bool count)
	{
		SpecDecision decision;
		
		if (!spec.enabled)
			return decision;  // CATS disabled
		
		// Note: cats_spec_attempts is counted ONLY when we actually speculate (after all gates pass)
		
		// ================================================================
		// Gate 1: Instruction check
		// ================================================================
		if (instruction && !spec.enable_instruction_speculation)
		{
			if (count) translation_stats.cats_gated_instruction++;
			return decision;
		}
		
		// ================================================================
		// Gate 2: Ambiguity check (num_candidates within bounds)
		// ================================================================
		if (lr.num_candidates == 0 || lr.num_candidates > spec.wmax)
		{
			if (count) translation_stats.cats_gated_ambiguity++;
			return decision;
		}
		
		// ================================================================
		// Gate 3: Confidence check
		// ================================================================
		UInt64 key = computeSpecTableKey(lr.seg_id, lr.set_index);
		SpecCtrl::ConfEntry& conf_entry = lookupConfEntry(key);
		
		if (conf_entry.conf < spec.conf_threshold)
		{
			if (count) translation_stats.cats_gated_confidence++;
			return decision;
		}
		
		// ================================================================
		// All gates passed - approve speculation
		// ================================================================
		if (count) translation_stats.cats_spec_attempts++;  // Count ONLY when we actually decide to speculate
		
		decision.do_speculate = true;
		decision.do_early_demand = false;  // Prefetch-only mode
		
		spec.spec_issued_epoch++;
		
		// Select ways to prefetch based on way predictor
		// Phase 3 fix: predicted_way is a WAY ID, not an index into candidate_ways[]
		// We must find which candidate (if any) matches the predicted way ID
		int predicted_way_id = lookupWayPredictor(key);
		int predicted_idx = -1;  // Index into candidate_ways[] that matches prediction
		
		if (predicted_way_id >= 0)
		{
			// Search for the predicted way ID in the candidate list
			for (int i = 0; i < lr.num_candidates; i++)
			{
				if (lr.candidate_ways[i] == predicted_way_id)
				{
					predicted_idx = i;
					break;
				}
			}
		}
		
		if (predicted_idx >= 0)
		{
			// Prediction matched a candidate - use it as primary
			decision.chosen_way0 = lr.candidate_ways[predicted_idx];
			decision.prefetch_addr0 = lr.candidate_pas[predicted_idx];
			if (count) translation_stats.cats_way_predictions++;  // Count only when prediction is actually used
			
			// Use first non-predicted candidate as secondary (if any)
			for (int i = 0; i < lr.num_candidates; i++)
			{
				if (i != predicted_idx)
				{
					decision.chosen_way1 = lr.candidate_ways[i];
					decision.prefetch_addr1 = lr.candidate_pas[i];
					break;
				}
			}
		}
		else
		{
			// No valid prediction - use first two candidates
			if (lr.num_candidates >= 1)
			{
				decision.chosen_way0 = lr.candidate_ways[0];
				decision.prefetch_addr0 = lr.candidate_pas[0];
			}
			if (lr.num_candidates >= 2)
			{
				decision.chosen_way1 = lr.candidate_ways[1];
				decision.prefetch_addr1 = lr.candidate_pas[1];
			}
		}
		
		return decision;
	}

	void MemoryManagementUnitUtopia::updateCATSOutcome(const RSWLookupResult& lr, const SpecDecision& decision,
	                                                   bool hit, int correct_way, bool metadata_fast, bool count)
	{
		if (!spec.enabled) return;
		
		UInt64 key = computeSpecTableKey(lr.seg_id, lr.set_index);
		SpecCtrl::ConfEntry& conf_entry = lookupConfEntry(key);
		
		int conf_max = (1 << spec.conf_bits) - 1;
		
		// ================================================================
		// Update confidence table and track speculation outcomes
		// ================================================================
		if (hit && decision.do_speculate)
		{
			// Speculation was issued and we got a hit - check if prediction was correct
			bool correct_speculation = (correct_way == decision.chosen_way0 || 
			                            correct_way == decision.chosen_way1);
			
			if (correct_speculation)
			{
				// Correct speculation: hit + right way predicted
				if (conf_entry.conf < conf_max)
					conf_entry.conf++;
				
				if (count) {
					translation_stats.cats_spec_correct++;
					translation_stats.cats_way_prediction_hits++;
				}
			}
			else
			{
				// Wrong way: hit but predicted wrong way
				if (conf_entry.conf > 0)
					conf_entry.conf--;
				
				if (count) translation_stats.cats_spec_wrong_way++;
			}
		}
		else if (!hit && decision.do_speculate)
		{
			// Miss speculation: speculated but RSW missed (wasteful)
			if (conf_entry.conf > 0)
				conf_entry.conf--;
			
			if (count) translation_stats.cats_spec_miss++;
		}
		else if (hit && !decision.do_speculate && metadata_fast)
		{
			// Could have benefited from speculation (hit, didn't speculate, metadata was fast)
			// Slightly increase confidence to encourage future speculation
			if (conf_entry.conf < conf_max)
				conf_entry.conf++;
		}
		
		if (count) translation_stats.cats_conf_updates++;
		
		// ================================================================
		// Update way predictor (always update policy state)
		// ================================================================
		if (spec.way_predictor_enabled && hit && correct_way >= 0)
		{
			UInt64 way_index = key % spec.way_table.size();
			UInt16 way_tag = (UInt16)((key >> 16) & 0xFFFF);
			
			SpecCtrl::WayEntry& way_entry = spec.way_table[way_index];
			
			if (way_entry.tag == way_tag)
			{
				// Update existing entry
				way_entry.last_way = correct_way;
				if (way_entry.usefulness < 3) way_entry.usefulness++;
			}
			else
			{
				// Allocate new entry (only if current entry has low usefulness)
				if (way_entry.usefulness == 0)
				{
					way_entry.tag = way_tag;
					way_entry.last_way = correct_way;
					way_entry.usefulness = 1;
				}
				else
				{
					way_entry.usefulness--;
				}
			}
		}
	}

	void MemoryManagementUnitUtopia::executeSpeculation(const SpecDecision& decision, IntPtr eip, SubsecondTime t_start, bool count)
	{
		if (!decision.do_speculate) return;
		
		// Get L2 cache controller for prefetch
		auto* l2_cntlr = memory_manager->getCacheCntlrAt(core->getId(), MemComponent::component_t::L2_CACHE);
		if (!l2_cntlr) return;
		
		// Issue prefetches for predicted ways
		if (decision.prefetch_addr0 != 0)
		{
			IntPtr cache_line = decision.prefetch_addr0 & ~((IntPtr)63);
			l2_cntlr->handleMMUPrefetch(eip, cache_line, t_start);
			if (count) translation_stats.cats_prefetch_issued++;
		}
		if (decision.prefetch_addr1 != 0)
		{
			IntPtr cache_line = decision.prefetch_addr1 & ~((IntPtr)63);
			l2_cntlr->handleMMUPrefetch(eip, cache_line, t_start);
			if (count) translation_stats.cats_prefetch_issued++;
		}
	}

} // namespace ParametricDramDirectoryMSI
