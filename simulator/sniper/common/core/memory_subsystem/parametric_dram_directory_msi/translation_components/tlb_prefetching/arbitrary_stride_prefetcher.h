
#pragma once
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "core.h"
#include "shmem_perf_model.h"
#include "pagetable.h"
#include "tlb_subsystem.h"
#include "cache_block_info.h"
#include "tlb_prefetcher_base.h"
#include <fstream>
#include <string>
#include <vector>
#include <cstring>

namespace ParametricDramDirectoryMSI
{

	/**
	 * @brief ASP: PC-indexed arbitrary-stride TLB prefetcher.
	 *
	 * A direct-mapped table indexed by PC holds, per PC, the last VPN it
	 * touched, the stride between its last two VPNs, and a confidence counter.
	 * Every access by the same PC with the same stride bumps the counter; a
	 * different stride resets it.  Once the counter exceeds prefetch_threshold
	 * the prefetcher walks VPN + (lookahead + d) * stride for d in [0, degree),
	 * plus VPN - stride when extra_prefetch is set.
	 *
	 * Known model issue (skip_zero_stride): a PC that misses on the SAME page
	 * again (common in pointer-chasing code, where the page is evicted from the
	 * TLB between two touches) learns stride 0.  Repeated stride-0 observations
	 * train the entry, and the "prefetch" is then VPN + k*0 = the page being
	 * accessed -- it duplicates the demand walk, is never timely, and turns the
	 * PQ into a victim buffer.  On gc/dlrm this was 80-92% of all ASP
	 * prefetches.  skip_zero_stride=true neither trains on nor prefetches with
	 * a zero stride, and uses an explicit valid bit instead of the old
	 * stride == -1 "unset" sentinel (which also collided with a real -1 stride:
	 * a change away from stride -1 did not reset the counter).  Default true;
	 * false reproduces the pre-2026-09-28 behaviour.
	 */
	class ArbitraryStridePrefetcher : public TLBPrefetcherBase
	{

	protected:
		typedef struct entry_prefetcher_t
		{
			IntPtr PC;               // tag: full PC of the owning instruction
			IntPtr vaddr;            // last VPN touched by this PC (a VPN, despite the name)
			long long stride;        // last observed VPN stride; -1 = "unset" in legacy mode
			bool stride_valid;       // strict mode only: stride holds a real observation
			uint saturation_counter; // confidence: consecutive repeats of `stride` (unbounded)
		} entry_prefetcher;

		struct ASPStats
		{
			// Existing
			UInt64 successful_prefetches;
			UInt64 prefetch_attempts;
			UInt64 failed_prefetches;

			// New high-level effectiveness metrics
			UInt64 queries;                // Calls to performPrefetch()
			UInt64 pc_hits;                // Table entry PC == eip
			UInt64 pc_misses;              // PC mismatch, new entry allocated
			UInt64 pc_evictions;           // Overwriting a non-empty entry

			// Stride learning behavior
			UInt64 new_stride_observed;    // Times stride was first set from -1
			UInt64 stride_same;            // Times new_stride == recorded stride
			UInt64 stride_change;          // Times new_stride != recorded stride (!=-1)
			UInt64 zero_stride;            // new_stride == 0
			UInt64 positive_stride;        // new_stride > 0
			UInt64 negative_stride;        // new_stride < 0

			// Threshold / training stats
			UInt64 threshold_reached;      // Times saturation_counter > prefetch_threshold
			UInt64 trained_entries;        // Entries that ever reached threshold
			UInt64 trained_but_no_prefetch;// Threshold reached but PTWTransparent failed (no PPN)

			// Extra prefetch statistics
			UInt64 extra_prefetches_issued;
			UInt64 extra_prefetches_successful;
			UInt64 extra_prefetches_failed;

			// Stride magnitude & saturation stats
			UInt64 sum_abs_stride;         // Sum of |stride| at prediction time
			UInt64 prefetch_distance_sum;  // Sum of |VPN_prefetch - VPN_current|
			UInt64 max_saturation_counter; // Max saturation_counter seen

			// Table usage pattern
			UInt64 table_accesses;         // Number of table slots touched

			UInt64 zero_stride_skipped;    // strict mode: stride-0 observations not used for training/prefetch
			UInt64 targets_out_of_range;   // targets that left the trigger's 48-bit space (< 0 or past 2^48): dropped, not walked
		} stats;

	public:
		Core *core;
		MemoryManagerBase *memory_manager;
		ShmemPerfModel *shmem_perf_model;
		int table_size;          // number of table entries (direct-mapped on PC)
		int prefetch_threshold;
		bool extra_prefetch;
		int lookahead;
		int degree;
		bool install_pq;   // false = "cache-only": walk the PTE (warm cache) but don't insert into PQ
		bool skip_zero_stride;  // see class comment
		entry_prefetcher *table;

		std::string log_file_name;
		std::ofstream log_file;

		/// table_entries is the resolved entry count (the factory converts the
		/// legacy log2 `table_size` key; see createASPPrefetcher).
		ArbitraryStridePrefetcher(Core *_core, MemoryManagerBase *_memory_manager, ShmemPerfModel *_shmem_perf_model, int table_entries, int prefetch_threshold, bool extra_prefetch, int lookahead, int degree, String name, bool install_pq = true, bool skip_zero_stride = true);
		std::vector<query_entry> performPrefetch(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count, PageTable *pt, bool instruction = false, bool tlb_hit = false, bool pq_hit = false, int page_size = 12) override;

	protected:
		void appendSummary(std::ostream &os) const override;

		/// True if a (signed) target 4KB VPN stays in the same 48-bit address
		/// space as the triggering VPN (VPN bits 36.. unchanged); a negative
		/// target or one past 2^48 fails.  The trigger arrives untagged (see
		/// TLBPrefetcherBase::m_addr_tag), so this is 0 <= target < 2^36; the
		/// low part is the full 48-bit VA, not the x86 47-bit user limit
		/// (traces use the upper half of the 48-bit space).
		static bool vpnInTriggerSpace(long long target_vpn, IntPtr trigger_vpn)
		{
			return (static_cast<uint64_t>(target_vpn) >> (48 - 12)) == (static_cast<uint64_t>(trigger_vpn) >> (48 - 12));
		}
	};
}
