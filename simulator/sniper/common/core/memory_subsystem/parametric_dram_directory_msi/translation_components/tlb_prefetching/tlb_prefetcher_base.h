
#pragma once
#include "subsecond_time.h"
#include "fixed_types.h"
#include "core.h"
#include "../memory_manager.h"
#include "shmem_perf_model.h"
#include "pagetable.h"
#include "cache_block_info.h"
#include "trans_defs.h"
#include <vector>
#include <sstream>

namespace ParametricDramDirectoryMSI
{

	class TLB;  // Forward declaration for TLB residency check

	/**
	 * @brief Base class for every TLB prefetcher.
	 *
	 * A prefetcher is owned by a prefetch-queue TLB (a "PQ", see tlb.cc).  The
	 * PQ sits at one level of the TLB hierarchy next to the regular TLB(s) of
	 * that level, so the prefetcher only sees accesses that REACH that level:
	 * with the PQ at level 2, it is invoked on every L1 TLB miss, whether or
	 * not the L2 TLB then hits.  "tlb_hit" in the hooks below therefore means
	 * "hit in the PQ", not "hit anywhere in the TLB hierarchy".
	 *
	 * Per access the PQ calls invoke() (never performPrefetch() directly).
	 * invoke() keeps a common set of counters for every prefetcher, prints a
	 * progress line every perf_model/tlb_prefetcher_log_interval invocations
	 * (default 20000, about 4 lines per 10M instructions; 0 disables),
	 * and forwards to the subclass's performPrefetch().  At simulation end a
	 * one-line summary is printed.  All lines start with
	 *     [TLBPF <name> c<core>]
	 * so `grep TLBPF` on the run log shows everything the prefetchers report.
	 *
	 * Returned query_entry objects are candidate translations: the walk that
	 * produced each one has ALREADY been modelled (PTWTransparent charges its
	 * cache/DRAM traffic), so the PQ only decides whether to keep the result.
	 * An entry with ppn == 0 is a failed walk and is ignored by the PQ.
	 */
	class TLBPrefetcherBase
	{

	protected:
		std::vector<TLB*> m_tlb_hierarchy;  // TLB pointers for residency check

		/// Counters kept identically for every prefetcher, so they can be
		/// compared across prefetchers (category "tlbpf_<name>" in sim.stats).
		struct CommonStats
		{
			UInt64 invocations;          ///< invoke() calls (accesses reaching the PQ level)
			UInt64 invocations_miss;     ///< ...that missed in the PQ
			UInt64 invocations_pq_hit;   ///< ...that consumed a prefetched entry (first use)
			UInt64 candidates_returned;  ///< query_entries returned, including failed walks
			UInt64 candidates_valid;     ///< ...with a resolved translation (ppn != 0)
			UInt64 candidates_self;      ///< ...for the page being accessed right now.
			                             ///<   Such a prefetch duplicates the demand walk and
			                             ///<   can never be timely; a large value means the
			                             ///<   predictor is degenerate (e.g. a zero stride).
			UInt64 candidates_noncanonical; ///< PTWTransparent calls refused: target outside the 48-bit
			                                ///<   VA space (a VPN that went negative or past 2^48)
		} m_common;

		/**
		 * Address tag.  The trace front end (TraceThread::va2pa) hands the
		 * core `(app_id << 48) | va`: bits 48..63 carry the app id, above the
		 * 48-bit VA the page table decodes.  They are 0 for app 0 and for
		 * single-core runs.  The tag is a simulator artifact, not part of the
		 * VA a hardware prefetcher sees, and prefetchers do VA arithmetic and
		 * keep VPNs in finite fields (TRAIL packs 36-bit VPNs), which drops or
		 * corrupts it -- so apps 1..N lost prefetches that app 0 kept.
		 *
		 * Therefore prefetchers work on UNTAGGED 48-bit VAs only:
		 *  - every address entering a prefetcher is stripped: invoke() and the
		 *    victimEvicted()/pqEntryEvicted()/prefetchInstalled()/
		 *    demandWalkDone() entry points the TLB and MMU call;
		 *  - every address leaving one gets the tag back: returned candidates
		 *    (invoke), PTWTransparent walks, and -- in the prefetchers --
		 *    direct pt->initializeWalk() calls and TLB residency probes, via
		 *    withTag().  The page table, fault handler and TLBs thus see the
		 *    same tagged addresses as for demand accesses.
		 * With tag 0 all of this is the identity.
		 */
		UInt64 m_addr_tag;   ///< bits 48..63 of the access that last invoked this prefetcher

		static IntPtr stripTag(IntPtr a) { return static_cast<uint64_t>(a) & ((1ULL << 48) - 1); }
		/// Tag an untagged VA (from this prefetcher) for the page table / TLBs.
		IntPtr withTag(IntPtr a) const { return static_cast<uint64_t>(a) | (m_addr_tag << 48); }

		UInt64 m_log_interval;  ///< progress line every N invocations; 0 = only the end summary
		bool m_summary_printed;

		/// "[TLBPF <name> c<core>] " -- prefix for every log line.
		std::string logPrefix() const;

		/// Subclasses append their key counters (space-separated k=v) to the
		/// progress/summary line.  Keep it to one line.
		virtual void appendSummary(std::ostream & /*os*/) const {}

		/// Prints one progress or summary line (tag = "progress" / "summary").
		void printStatusLine(const char *tag);

		static SInt64 hookSimEnd(UInt64 self, UInt64 /*time*/);

	public:
		Core *core;
		MemoryManagerBase *memory_manager;
		ShmemPerfModel *shmem_perf_model;
		String m_name;

		string log_file_name;
		std::ofstream log_file;

		TLBPrefetcherBase(Core *_core, MemoryManagerBase *_memory_manager, ShmemPerfModel *_shmem_perf_model, String name);
		virtual ~TLBPrefetcherBase() {}

		void setTLBHierarchy(const std::vector<TLB*>& tlbs) { m_tlb_hierarchy = tlbs; }

		/**
		 * @brief Entry point used by the PQ on every access reaching its level.
		 *
		 * Updates the common counters, forwards to performPrefetch(), and
		 * prints a progress line every m_log_interval calls.  Arguments are
		 * those of performPrefetch().
		 */
		std::vector<query_entry> invoke(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count, PageTable *pt, bool instruction, bool tlb_hit, bool pq_hit, int page_size);

		// Entry points for the TLB / MMU.  They strip the address tag (see
		// m_addr_tag) and forward to the virtual hooks below; call these,
		// never the hooks directly.
		void victimEvicted(IntPtr a, int page_size, IntPtr ppn) { notifyVictim(stripTag(a), page_size, ppn); }
		void pqEntryEvicted(IntPtr a, int page_size, IntPtr ppn) { notifyPQVictim(stripTag(a), page_size, ppn); }
		void prefetchInstalled(IntPtr a, int page_size) { notifyInstall(stripTag(a), page_size); }
		void demandWalkDone(IntPtr a, SubsecondTime walk_latency) { onDemandWalkComplete(stripTag(a), walk_latency); }

		// Called when a TLB evicts a victim during allocate().
		// Override in prefetchers that need victim info (e.g., recency stack).
		virtual void notifyVictim(IntPtr /*victim_address*/, int /*page_size*/, IntPtr /*ppn*/) {}

		// Called when the prefetch queue itself evicts an entry, i.e. a
		// prefetched translation leaves the prefetch buffer (it was never in
		// the TLB proper).  Default forwards to notifyVictim(), which is what
		// every prefetcher saw before this hook existed.
		virtual void notifyPQVictim(IntPtr victim_address, int page_size, IntPtr ppn) { notifyVictim(victim_address, page_size, ppn); }

		// Called when a prefetch-queue entry is materialized into the TLB
		// (i.e., the prefetched translation has been installed).
		// Override in prefetchers that track in-flight prefetches.
		virtual void notifyInstall(IntPtr /*address*/, int /*page_size*/) {}

		// Called by the MMU after every completed demand page-table walk with
		// that walk's latency.  Prefetchers that piggyback on the demand walk
		// (e.g. ATP's SBFP) use it to time the entries they derive from it.
		virtual void onDemandWalkComplete(IntPtr /*address*/, SubsecondTime /*walk_latency*/) {}

		/**
		 * @brief Model a prefetch page-table walk without advancing the core.
		 *
		 * Runs a full, cache-modelled walk (is_prefetch = true) and returns the
		 * translation stamped with the time it becomes available
		 * (now + walk latency).  The core's clock is restored afterwards.
		 * restart_walk = true: if the target page is unmapped the walk takes
		 * the page fault and allocates it (the "prefetch walk pays for the
		 * fault" model); the walk latency, not the fault, is charged.
		 */
		virtual query_entry PTWTransparent(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count, PageTable *pt);

		/**
		 * @brief Train on one access and return candidate prefetches.
		 *
		 * @param tlb_hit  the access hit in the PQ (see class comment)
		 * @param pq_hit   ...and that hit consumed a prefetched entry for the first time
		 * @param page_size page size (bits) of the PQ hit, 0 when unknown (miss)
		 */
		virtual std::vector<query_entry> performPrefetch(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count, PageTable *pt, bool instruction = false, bool tlb_hit = false, bool pq_hit = false, int page_size = 12) = 0;

		// Called by the MMU AFTER the demand page-table walk completes, so any
		// PTE cacheline a prefetcher updated this access is now resident.
		// Prefetchers that model in-PTE payload writes (TRAIL) override this to
		// mark the updated PTE line(s) dirty -> realistic writeback traffic.
		virtual void flushPendingPayloadWritebacks(PageTable * /*pt*/) {}
	};
}
