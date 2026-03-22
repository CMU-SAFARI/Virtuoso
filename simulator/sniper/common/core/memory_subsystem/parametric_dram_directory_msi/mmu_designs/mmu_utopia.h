
#pragma once
#include "../memory_manager.h"
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "core.h"
#include "shmem_perf_model.h"
#include "pagetable.h"
#include "tlb_subsystem.h"
#include "mmu_base.h"
#include "metadata_table_base.h"
#include "pwc.h"    
#include "utopia_cache_template.h"
#include "utopia_tlb.h"
#include "ptmshrs.h"
#include "base_filter.h"
#include "sim_log.h"
#include "debug_config.h"

namespace ParametricDramDirectoryMSI
{
	class TLBHierarchy;
	
	/**
	 * @brief Result of a functional RestSeg lookup (no cache accesses performed)
	 * 
	 * This structure captures the result of checking whether an address is in
	 * a RestSeg, along with the metadata addresses that would need to be accessed
	 * to verify and translate. Used for the functional-first translation flow.
	 */
	struct RSWLookupResult {
		bool hit;                  ///< True if address is in a RestSeg
		int seg_id;                ///< Which RestSeg contains the address (-1 if miss)
		int way;                   ///< Way within the RestSeg (-1 if miss)
		int page_size_bits;        ///< Page size in bits (12=4KB, 21=2MB)
		IntPtr ppn;                ///< Physical page number (valid if hit)
		IntPtr physical_address;   ///< Full physical address (valid if hit)
		IntPtr virtual_address;    ///< Original virtual address being translated
		
		// Metadata addresses to access for verification
		IntPtr fpa_address;        ///< Fingerprint Array address (for fingerprint check)
		IntPtr tar_address;        ///< Tag Array address (for tag verification)
		
		// CATS speculation fields
		int set_index;             ///< Set index within the RestSeg (for hash computation)
		
		// Candidate ways for CATS speculation (up to 4 ways)
		static const int MAX_CANDIDATES = 4;
		int num_candidates;        ///< Number of FP-matching candidate ways
		int candidate_ways[MAX_CANDIDATES];  ///< Array of candidate way indices
		IntPtr candidate_pas[MAX_CANDIDATES]; ///< Physical addresses for each candidate way
		
		RSWLookupResult() : hit(false), seg_id(-1), way(-1), page_size_bits(0), 
		                    ppn(0), physical_address(0), virtual_address(0),
		                    fpa_address(0), tar_address(0),
		                    set_index(0), num_candidates(0) {
			for (int i = 0; i < MAX_CANDIDATES; i++) {
				candidate_ways[i] = -1;
				candidate_pas[i] = 0;
			}
		}
	};

	/**
	 * @brief CATS speculation decision result
	 * 
	 * Captures the decision made by the CATS throttling logic including
	 * whether to speculate and which way(s) to prefetch.
	 */
	struct SpecDecision {
		bool do_speculate;         ///< Whether speculation is approved
		bool do_early_demand;      ///< If true: early demand; if false and do_speculate: prefetch only
		int chosen_way0;           ///< Primary predicted way (-1 if none)
		int chosen_way1;           ///< Secondary predicted way (-1 if none)
		IntPtr prefetch_addr0;     ///< Primary prefetch address
		IntPtr prefetch_addr1;     ///< Secondary prefetch address
		
		SpecDecision() : do_speculate(false), do_early_demand(false),
		                 chosen_way0(-1), chosen_way1(-1), prefetch_addr0(0), prefetch_addr1(0) {}
	};

	class MemoryManagementUnitUtopia : public MemoryManagementUnitBase
	{

	protected:
		MemoryManagerBase *memory_manager;
		TLBHierarchy *tlb_subsystem;
		MSHR *pt_walkers; 
		BaseFilter *ptw_filter;


		// Utopia-specific
		UtopiaCache *fpa_cache;  ///< Fingerprint array cache (replaces permission filter)
		UtopiaCache *tar_cache;
		UtopiaTLB *utlb;  ///< Compact TLB for RestSeg translations (UTLB)
		std::unordered_map<IntPtr, pair<int, int>> ptw_stats;	   // keep track of frequency/cost of PTWs
		std::unordered_map<IntPtr, SubsecondTime> migration_queue; // keep track of migrations

		// ====================================================================
		// Translation Sanity Checks (VA-PA consistency)
		// ====================================================================
		// Maps to verify translation consistency:
		// - va_to_pa_map: VA (page-aligned) -> PA (page-aligned), ensures same VA always maps to same PA
		// - pa_to_va_map: PA (page-aligned) -> VA (page-aligned), ensures no two VAs map to same PA
		bool sanity_checks_enabled;  ///< Enable translation sanity checks
		std::unordered_map<IntPtr, IntPtr> va_to_pa_map;  ///< VA -> PA mapping for consistency check
		std::unordered_map<IntPtr, IntPtr> pa_to_va_map;  ///< PA -> VA mapping for uniqueness check
		UInt64 sanity_check_violations;  ///< Count of detected violations

		int ptw_migration_threshold;
		int dram_accesses_migration_threshold;
		bool legacy_migration_enabled;  ///< Whether legacy threshold-based migration is enabled
		bool utlb_enabled;  ///< Whether UTLB is enabled

		// ====================================================================
		// Cost-based Top-K Migration (per-core config cache from allocator)
		// ====================================================================
		// These fields cache config from the allocator for quick access in MMU
		bool cost_topk_migration_enabled;     ///< Master enable
		int cost_score_bits;                  ///< Bits for saturating counter
		int cost_ptw_base_inc;                ///< Base increment per PTW
		int cost_dram_inc_cap;                ///< Max DRAM access contribution

		// ====================================================================
		// Radix Way Table (replaces FPA+TAR for translation lookup)
		// ====================================================================
		bool radix_lookup_enabled;            ///< Use radix table instead of FPA+TAR

		// ====================================================================
		// Utopia Metadata Cache Bypass (route to PTW caches instead)
		// ====================================================================
		bool disable_fp_tar_caches;           ///< If true, route metadata to PTW cache path
		bool disable_fp_cache_only;           ///< If true, only disable FP cache (TAR still used)
		bool disable_tar_cache_only;          ///< If true, only disable TAR cache (FP still used)

		// ====================================================================
		// CATS (Confidence-Aware Throttled Speculation) Controller
		// ====================================================================
		struct SpecCtrl {
			// Main enable
			bool enabled;              ///< Master enable for CATS (prefetch-only mode)
			int wmax;                  ///< Max candidate ways to speculate on
			
			// Confidence table parameters
			int conf_bits;             ///< Bits per confidence counter (e.g., 3 for 0-7)
			int conf_threshold;        ///< Minimum confidence to speculate
			int conf_table_entries;    ///< Number of entries in confidence table
			
			// Way predictor parameters
			bool way_predictor_enabled; ///< Enable last-way predictor
			int lastway_table_entries;  ///< Number of entries in way predictor table
			
			// Instruction speculation
			bool enable_instruction_speculation; ///< Allow speculation on instruction fetches
			
			// Runtime state
			UInt64 spec_issued_epoch;  ///< Speculations issued this epoch
			
			// Confidence table entry
			struct ConfEntry {
				UInt16 tag;
				UInt8 conf;
				ConfEntry() : tag(0xFFFF), conf(0) {}
			};
			std::vector<ConfEntry> conf_table;
			
			// Way predictor entry
			struct WayEntry {
				UInt16 tag;
				UInt8 last_way;
				UInt8 usefulness;
				WayEntry() : tag(0xFFFF), last_way(0), usefulness(0) {}
			};
			std::vector<WayEntry> way_table;
			
			SpecCtrl() : enabled(false), wmax(2),
			             conf_bits(3), conf_threshold(3), conf_table_entries(16384),
			             way_predictor_enabled(true), lastway_table_entries(8192),
			             enable_instruction_speculation(false),
			             spec_issued_epoch(0) {}
		};
		SpecCtrl spec;  ///< CATS speculation controller state
		
		// NOTE: MetadataFetchConfig struct removed (Phase 0 cleanup)
		// Only baseline FPA+TAR path is supported now.

		// Adaptive RestSeg access (sequential vs parallel)
		bool adaptive_rsw_enabled;         ///< Enable adaptive sequential/parallel access
		UInt64 rsw_hits_per_restseg[2];    ///< Track hits per RestSeg (0 and 1)
		UInt64 rsw_total_hits;             ///< Total RSW hits for hit rate calculation
		int rsw_dominant_restseg;          ///< Which RestSeg dominates (-1 = neither, use parallel)
		double rsw_sequential_threshold;   ///< Threshold (e.g., 0.90 = 90%) for sequential access
		UInt64 rsw_warmup_accesses;        ///< Number of accesses before adaptive kicks in

		// Centralized logging (replaces legacy ofstream)
		SimLog *mmu_log;
		
		// FPA/TAR access pattern logging
		bool access_pattern_logging_enabled;     ///< Enable detailed FPA/TAR access pattern logging
		std::ofstream *access_pattern_log;       ///< Log file for FPA/TAR access patterns
		UInt64 access_pattern_sample_rate;       ///< Log every N-th access (1 = all, 100 = 1%)
		UInt64 access_pattern_counter;           ///< Counter for sampling
		
		// PTW access pattern logging
		std::ofstream *ptw_access_log;           ///< Log file for PTW access patterns
		UInt64 ptw_access_counter;               ///< Counter for PTW accesses logged

		struct
		{
			UInt64 num_translations;
			UInt64 page_faults;
			UInt64 page_table_walks;
			UInt64 l2_tlb_misses;  ///< L2 (last level) TLB misses for MPKI calculation

			UInt64 flextorest_migrations;
			UInt64 tlb_shootdowns;                ///< TLB shootdowns after migration
			UInt64 requests_affected_by_migration;
			SubsecondTime migration_stall_cycles;


			UInt64 data_in_restseg;
			UInt64 data_in_flexseg;
			UInt64 data_in_tlb;
			UInt64 data_in_utlb;  ///< Translations found in UTLB


			UInt64 data_in_restseg_no_fault;
			UInt64 data_in_flexseg_no_fault;

			UInt64 total_rsw_memory_requests;

			// Adaptive RestSeg access stats
			UInt64 rsw_sequential_accesses;    ///< RSW accesses using sequential mode
			UInt64 rsw_parallel_accesses;      ///< RSW accesses using parallel mode
			UInt64 rsw_hits_restseg0;          ///< Hits in RestSeg 0
			UInt64 rsw_hits_restseg1;          ///< Hits in RestSeg 1

			// Per-RestSeg detailed latency tracking (for computing averages)
			SubsecondTime fpa_latency_restseg0;   ///< Total FPA access latency for RestSeg 0
			SubsecondTime fpa_latency_restseg1;   ///< Total FPA access latency for RestSeg 1
			SubsecondTime tar_latency_restseg0;   ///< Total TAR access latency for RestSeg 0
			SubsecondTime tar_latency_restseg1;   ///< Total TAR access latency for RestSeg 1
			UInt64 fpa_accesses_restseg0;         ///< Number of FPA accesses to RestSeg 0
			UInt64 fpa_accesses_restseg1;         ///< Number of FPA accesses to RestSeg 1
			UInt64 tar_accesses_restseg0;         ///< Number of TAR accesses to RestSeg 0
			UInt64 tar_accesses_restseg1;         ///< Number of TAR accesses to RestSeg 1
			UInt64 fpa_cache_hits_restseg0;       ///< FPA cache hits for RestSeg 0
			UInt64 fpa_cache_hits_restseg1;       ///< FPA cache hits for RestSeg 1
			UInt64 tar_cache_hits_restseg0;       ///< TAR cache hits for RestSeg 0
			UInt64 tar_cache_hits_restseg1;       ///< TAR cache hits for RestSeg 1
			UInt64 fp_filtered_restseg0;          ///< Times FP filtering skipped TAR for RestSeg 0
			UInt64 fp_filtered_restseg1;          ///< Times FP filtering skipped TAR for RestSeg 1

			// FPA/TAR memory hierarchy hit tracking (where did the access go?)
			UInt64 fpa_hit_l2_restseg0;           ///< FPA hits in L2 cache for RestSeg 0
			UInt64 fpa_hit_l2_restseg1;           ///< FPA hits in L2 cache for RestSeg 1
			UInt64 fpa_hit_nuca_restseg0;         ///< FPA hits in NUCA cache for RestSeg 0
			UInt64 fpa_hit_nuca_restseg1;         ///< FPA hits in NUCA cache for RestSeg 1
			UInt64 fpa_hit_dram_restseg0;         ///< FPA hits in DRAM for RestSeg 0
			UInt64 fpa_hit_dram_restseg1;         ///< FPA hits in DRAM for RestSeg 1
			UInt64 tar_hit_l2_restseg0;           ///< TAR hits in L2 cache for RestSeg 0
			UInt64 tar_hit_l2_restseg1;           ///< TAR hits in L2 cache for RestSeg 1
			UInt64 tar_hit_nuca_restseg0;         ///< TAR hits in NUCA cache for RestSeg 0
			UInt64 tar_hit_nuca_restseg1;         ///< TAR hits in NUCA cache for RestSeg 1
			UInt64 tar_hit_dram_restseg0;         ///< TAR hits in DRAM for RestSeg 0
			UInt64 tar_hit_dram_restseg1;         ///< TAR hits in DRAM for RestSeg 1

			// Speculation correctness tracking
			UInt64 spec_prefetch_correct;         ///< Prefetches where way matched (true positive)
			UInt64 spec_prefetch_wrong;           ///< Prefetches where way didn't match (false positive)
			UInt64 spec_prefetch_unused;          ///< Prefetches issued but no hit in RestSeg

			// ================================================================
			// CATS (Confidence-Aware Throttled Speculation) Stats
			// ================================================================
			UInt64 cats_spec_attempts;            ///< Total CATS speculation attempts (approved)
			UInt64 cats_spec_correct;             ///< Correct speculations (hit + right way predicted)
			UInt64 cats_spec_wrong_way;           ///< Wrong speculations (hit but wrong way predicted)
			UInt64 cats_spec_miss;                ///< Wasteful speculations (speculated but RSW miss)
			UInt64 cats_gated_ambiguity;          ///< Gated due to too many/few candidates
			UInt64 cats_gated_confidence;         ///< Gated due to low confidence
			UInt64 cats_gated_instruction;        ///< Gated due to instruction fetch
			UInt64 cats_prefetch_issued;          ///< Prefetch requests actually issued
			UInt64 cats_conf_updates;             ///< Confidence table updates
			UInt64 cats_way_predictions;          ///< Way predictor lookups
			UInt64 cats_way_prediction_hits;      ///< Way predictor correct predictions
			UInt64 cats_considered;               ///< Times CATS was considered (had candidates)
			UInt64 cats_cold_spec_issued;         ///< Cold speculation issued (RSW miss, PTW success)

			// ================================================================
			// Cost-based Top-K Migration Stats (per-core view)
			// ================================================================
			UInt64 cost_topk_score_updates;       ///< Score bumps after PTW
			UInt64 cost_topk_migration_attempts;  ///< Migration attempts from this core
			UInt64 cost_topk_migrations_issued;   ///< Successful migrations from this core

			// ================================================================
			// Radix Way Table Stats (per-core view)
			// ================================================================
			UInt64 radix_lookups_restseg0;        ///< Radix lookups for RestSeg 0
			UInt64 radix_lookups_restseg1;        ///< Radix lookups for RestSeg 1
			UInt64 radix_hits_restseg0;           ///< Radix hits for RestSeg 0
			UInt64 radix_hits_restseg1;           ///< Radix hits for RestSeg 1
			UInt64 radix_misses_restseg0;         ///< Radix misses for RestSeg 0
			UInt64 radix_misses_restseg1;         ///< Radix misses for RestSeg 1
			SubsecondTime radix_latency_restseg0; ///< Total radix walk latency for RestSeg 0

			// Radix PWC (page walk cache) hit tracking
			UInt64 radix_pwc_hits;                ///< Radix level accesses that hit in PWC
			UInt64 radix_pwc_misses;              ///< Radix level accesses that missed in PWC

			// Radix internal node memory hierarchy hit tracking (on PWC miss)
			UInt64 radix_internal_hit_l2;         ///< Radix internal node hits in L2
			UInt64 radix_internal_hit_nuca;       ///< Radix internal node hits in NUCA
			UInt64 radix_internal_hit_dram;       ///< Radix internal node hits in DRAM
			UInt64 radix_internal_accesses;       ///< Total radix internal node accesses

			// Radix leaf node memory hierarchy hit tracking (on PWC miss)
			UInt64 radix_leaf_hit_l2;             ///< Radix leaf node hits in L2
			UInt64 radix_leaf_hit_nuca;           ///< Radix leaf node hits in NUCA
			UInt64 radix_leaf_hit_dram;           ///< Radix leaf node hits in DRAM
			UInt64 radix_leaf_accesses;           ///< Total radix leaf node accesses

			// ================================================================
			// Metadata routing stats (FP/TAR bypass to PTW caches)
			// ================================================================
			UInt64 meta_accesses_routed_to_ptw;   ///< Metadata accesses routed to PTW cache path
			UInt64 meta_accesses_routed_to_fp_tar; ///< Metadata accesses to dedicated FP/TAR caches
			SubsecondTime radix_latency_restseg1; ///< Total radix walk latency for RestSeg 1

			// NOTE: Metadata co-fetch stats removed (Phase 0 cleanup)
			// Only baseline FPA+TAR path is supported now.

			SubsecondTime total_walk_latency_flexseg_no_fault;
			SubsecondTime total_rsw_latency_restseg_no_fault;

			SubsecondTime total_walk_latency_flexseg;
			SubsecondTime total_rsw_latency_restseg;
			
			SubsecondTime total_tlb_latency_on_tlb_hit;
			SubsecondTime total_utlb_latency;  ///< Total UTLB access latency

			SubsecondTime total_walk_latency;
			SubsecondTime total_rsw_latency;
			SubsecondTime total_tlb_latency;

			SubsecondTime total_fault_latency;

			SubsecondTime total_translation_latency;

			SubsecondTime *tlb_latency_per_level;

		} translation_stats;

		// ====================================================================
		// CATS Helper Methods (private)
		// ====================================================================
		
		/**
		 * @brief Compute hash key for confidence/way tables
		 * @param seg_id RestSeg ID
		 * @param set_index Set index within RestSeg
		 * @return Hash key for table lookup
		 */
		UInt64 computeSpecTableKey(int seg_id, int set_index);
		
		/**
		 * @brief Lookup or allocate confidence table entry
		 * @param key Table key from computeSpecTableKey
		 * @return Reference to confidence entry
		 */
		SpecCtrl::ConfEntry& lookupConfEntry(UInt64 key);
		
		/**
		 * @brief Lookup way predictor for predicted way
		 * @param key Table key
		 * @return Predicted way index (-1 if no prediction)
		 */
		int lookupWayPredictor(UInt64 key);
		
		/**
		 * @brief Decide whether to speculate based on CATS policy
		 * @param lr Functional lookup result with candidates
		 * @param instruction Whether this is instruction fetch
		 * @param now Current simulation time
		 * @param count Whether to count stats (false during warmup)
		 * @return SpecDecision with speculation details
		 */
		SpecDecision decideSpeculation(const RSWLookupResult& lr, bool instruction, SubsecondTime now, bool count);
		
		/**
		 * @brief Update CATS tables after translation outcome
		 * @param lr Original lookup result
		 * @param decision Speculation decision made
		 * @param hit Whether translation hit in RestSeg
		 * @param correct_way Actual way that hit (-1 if miss)
		 * @param metadata_fast Whether metadata was already cached
		 * @param count Whether to count stats (false during warmup)
		 */
		void updateCATSOutcome(const RSWLookupResult& lr, const SpecDecision& decision,
		                       bool hit, int correct_way, bool metadata_fast, bool count);
		
		/**
		 * @brief Execute speculation action (prefetch or early demand)
		 * @param decision Speculation decision
		 * @param eip Instruction pointer
		 * @param t_start Access start time
		 * @param count Whether to count stats (false during warmup)
		 */
		void executeSpeculation(const SpecDecision& decision, IntPtr eip, SubsecondTime t_start, bool count);

	public:
		MemoryManagementUnitUtopia(Core *core, MemoryManagerBase *memory_manager, ShmemPerfModel *shmem_perf_model, String name, MemoryManagementUnitBase *nested_mmu);
		~MemoryManagementUnitUtopia();
		void instantiatePageTableWalker();
		void instantiateTLBSubsystem();
		void instantiateRestSegWalker();
		void instantiateUTLB();  ///< Initialize the compact UTLB for RestSeg
		void registerMMUStats();
		IntPtr performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count);
		
		// RestSeg Walk methods
		virtual std::tuple<int, IntPtr, SubsecondTime> RestSegWalk(IntPtr address, bool instruction, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count);
		
		/**
		 * @brief Functional RestSeg lookup - no cache accesses, no latency charged
		 * 
		 * Checks if an address is in a RestSeg without performing cache accesses.
		 * Returns the lookup result and the metadata addresses that would need
		 * to be accessed to verify the translation.
		 * 
		 * @param address Virtual address to look up
		 * @return RSWLookupResult with hit status, translation info, and metadata addresses
		 */
		RSWLookupResult functionalRSWLookup(IntPtr address);
		
		/**
		 * @brief Charge latency for RestSeg metadata accesses
		 * 
		 * Given an RSWLookupResult, performs the actual cache accesses for
		 * FPA and TAR and returns the total latency.
		 * 
		 * @param lookup_result Result from functionalRSWLookup
		 * @param instruction Whether this is an instruction fetch
		 * @param eip Instruction pointer
		 * @param lock Lock signal
		 * @param modeled Whether to model timing
		 * @param count Whether to count statistics
		 * @return Total latency for the RestSeg walk
		 */
		SubsecondTime chargeRSWLatency(const RSWLookupResult& lookup_result, bool instruction, 
		                               IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count);
		
        PTWResult filterPTWResult(IntPtr virtual_address, PTWResult ptw_result, PageTable *page_table, bool count);

		/**
		 * @brief Perform TLB shootdown for a migrated page
		 * 
		 * Invalidates entries for the given virtual address across all TLB levels.
		 * Called after a page is migrated from FlexSeg to RestSeg to ensure stale
		 * translations are not used.
		 * 
		 * @param address Virtual address of the migrated page
		 * @param page_size Page size in bits (e.g., 12 for 4KB)
		 */
		void performTLBShootdown(IntPtr address, int page_size);

		/**
		 * @brief Update sanity check maps after a page migration
		 * 
		 * When a page is migrated from FlexSeg to RestSeg, the VA-PA mapping
		 * changes. This function updates the consistency tracking maps to
		 * reflect the new mapping.
		 * 
		 * @param address Virtual address of the migrated page
		 * @param old_ppn Old physical page number (in FlexSeg)
		 * @param new_ppn New physical page number (in RestSeg)
		 * @param page_size Page size in bits (e.g., 12 for 4KB)
		 */
		void updateSanityCheckMaps(IntPtr address, IntPtr old_ppn, IntPtr new_ppn, int page_size);

		/**
		 * @brief Invalidate sanity check maps for an evicted page
		 * 
		 * When a page is evicted from RestSeg back to FlexSeg, the VA-PA
		 * mapping changes but we don't know the new FlexSeg PA yet.
		 * This function removes the stale mapping so the next access
		 * will establish the correct new mapping.
		 * 
		 * @param address Virtual address of the evicted page
		 * @param page_size Page size in bits (e.g., 12 for 4KB)
		 */
		void invalidateSanityCheckMaps(IntPtr address, int page_size);

		/**
		 * @brief Log a single FPA or TAR access to CSV file
		 * 
		 * @param vpn Virtual page number being translated
		 * @param access_type "FPA" or "TAR"
		 * @param restseg_id RestSeg ID (0 or 1)
		 * @param address Physical address accessed
		 * @param utopia_cache_hit Whether hit in Utopia's dedicated cache
		 * @param hit_where Where in memory hierarchy (L2/NUCA/DRAM) - only valid if utopia_cache_hit=false
		 * @param latency Access latency
		 */
		void logMetadataAccess(UInt64 vpn, const char* access_type, int restseg_id,
		                       IntPtr address, bool utopia_cache_hit, HitWhere::where_t hit_where,
		                       SubsecondTime latency);

		/**
		 * @brief Override PTW cache access logging from base class
		 */
		void logPTWCacheAccess(UInt64 ptw_id, UInt64 vpn, int level, int table,
		                       IntPtr cache_line_addr, HitWhere::where_t hit_where,
		                       SubsecondTime latency, bool is_pte) override;


		void discoverVMAs();
	};
}