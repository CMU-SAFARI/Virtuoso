#include "tlb.h"
#include "stats.h"
#include "config.hpp"
#include "simulator.h"
#include <cmath>
#include "cache_cntlr.h"
#include <iostream>
#include <utility>
#include <cstdio>
#include <cstdlib>
#include <unordered_set>
#include "memory_manager.h"
#include "core_manager.h"
#include "cache_set.h"
#include "cache_base.h"
#include "utils.h"
#include "log.h"
#include "rng.h"
#include "address_home_lookup.h"
#include "fault_injection.h"
#include "memory_manager.h"
#include "debug_config.h"
#include <cstdlib>


namespace ParametricDramDirectoryMSI
{

    TLB::TLB(String name, String cfgname, core_id_t core_id, ComponentLatency access_latency, UInt32 num_entries, UInt32 associativity, int *page_size_list, int page_sizes, String tlb_type, bool allocate_on_miss, bool prefetch, TLBPrefetcherBase **tpb, int _number_of_prefetchers, int _max_prefetch_count)
        : m_size(num_entries),
          m_core_id(core_id),
          m_name(name),
          m_associativity(associativity),
          m_num_entries(num_entries),
          m_num_sets(num_entries / associativity),
          entry_size(1L << 3),
          m_cache(name + "_cache",
                  cfgname,
                  core_id, num_entries / associativity,
                  associativity, entry_size,
                  Sim()->getCfg()->hasKey(cfgname + "/replacement_policy")
                     ? Sim()->getCfg()->getString(cfgname + "/replacement_policy") : "lru",
                  CacheBase::PR_L1_CACHE, CacheBase::HASH_MOD,
                  // HASH_MOD lets TLBs accept non-power-of-2 num_sets
                  // (real-silicon TLB sizes are often odd: 10/48/96/1280/3072).
                  // For pow-2 num_sets the compiler optimises % to & mask,
                  // so there's no extra cost vs HASH_MASK.
                  NULL,
                  NULL, true, page_size_list, page_sizes),
          m_type(tlb_type),
          prefetchers(tpb),
          number_of_prefetchers(_number_of_prefetchers),
          m_page_size_list(nullptr),
          m_page_sizes(page_sizes),
          m_allocate_miss(allocate_on_miss),
          m_prefetch(prefetch),
          max_prefetch_count(_max_prefetch_count),
          m_access_latency(access_latency)
    {
        // Initialize SimLog for TLB (uses DEBUG_TLB flag)
        tlb_log = new SimLog(m_name.c_str(), core_id, DEBUG_TLB);


        LOG_ASSERT_ERROR((num_entries / associativity) * associativity == num_entries, "Invalid TLB configuration: num_entries(%d) must be a multiple of the associativity(%d)", num_entries, associativity);

        std::cout << "[MMU] Instantiating TLB: " << m_name << " "
                  << " Core ID: " << m_core_id << " "
                  << " Stores: " << tlb_type << " "
                  << " Number of entries: " << m_size << " "
                  << " Associativity: " << m_associativity << " "
                  << " TLB Type: " << m_type << " "
                  << " Allocate on miss: " << (m_allocate_miss ? "true" : "false") << " "
                  << " Number of prefetchers: " << number_of_prefetchers << " "
                  << " Access latency: " << m_access_latency.getLatency().getNS() << "ns "
                  << " Page sizes: " << m_page_sizes << std::endl;

        m_page_size_list = std::unique_ptr<int[]>(new int[m_page_sizes]);

        for (int i = 0; i < m_page_sizes; i++)
        {
            m_page_size_list[i] = page_size_list[i];
        }

        bzero(&tlb_stats, sizeof(tlb_stats));


		m_num_sets = num_entries / associativity;
		entry_size = ceil(((48 - log2(m_num_sets) - log2(associativity)) + 52)/8);

		bzero(&tlb_stats, sizeof(tlb_stats));

        registerStatsMetric(name, core_id, "accesses", &tlb_stats.m_access);
        registerStatsMetric(name, core_id, "hits", &tlb_stats.m_hit);
        registerStatsMetric(name, core_id, "misses", &tlb_stats.m_miss);
        registerStatsMetric(name, core_id, "evictions", &tlb_stats.m_eviction);
        registerStatsMetric(name, core_id, "insertions", &tlb_stats.m_insertions);

        // Register instruction vs data breakdown stats for Unified TLBs
        if (tlb_type == "Unified")
        {
            registerStatsMetric(name, core_id, "accesses_instruction", &tlb_stats.m_access_instruction);
            registerStatsMetric(name, core_id, "accesses_data", &tlb_stats.m_access_data);
            registerStatsMetric(name, core_id, "hits_instruction", &tlb_stats.m_hit_instruction);
            registerStatsMetric(name, core_id, "hits_data", &tlb_stats.m_hit_data);
            registerStatsMetric(name, core_id, "misses_instruction", &tlb_stats.m_miss_instruction);
            registerStatsMetric(name, core_id, "misses_data", &tlb_stats.m_miss_data);
            registerStatsMetric(name, core_id, "evictions_instruction", &tlb_stats.m_eviction_instruction);
            registerStatsMetric(name, core_id, "evictions_data", &tlb_stats.m_eviction_data);
            registerStatsMetric(name, core_id, "insertions_instruction", &tlb_stats.m_insertions_instruction);
            registerStatsMetric(name, core_id, "insertions_data", &tlb_stats.m_insertions_data);
        }

        if (m_prefetch)
        {
            registerStatsMetric(name, core_id, "pq_dedup_skipped", &tlb_stats.m_pq_dedup_skipped);
            registerStatsMetric(name, core_id, "pq_hits", &tlb_stats.m_pq_hits);
            registerStatsMetric(name, core_id, "pq_materialized", &tlb_stats.m_pq_materialized);

            // In-flight queue pressure.  Without these, a prefetcher that looks
            // weak is indistinguishable from one whose predictions were thrown
            // away because the in-flight queue was at its cap.
            registerStatsMetric(name, core_id, "pq_dedup_skipped_entries", &tlb_stats.m_pq_dedup_skipped_entries);
            registerStatsMetric(name, core_id, "pq_full_skipped_calls", &tlb_stats.m_pq_full_skipped_calls);
            registerStatsMetric(name, core_id, "pq_full_dropped_entries", &tlb_stats.m_pq_full_dropped_entries);
            // High-water mark.  sim.stats prints roi-end minus roi-begin; that
            // equals the mark only because the queue is empty at roi-begin
            // (ROI = whole run).  With a later ROI read the "roi-end" row of
            // sim.stats.sqlite3 instead.  (The cap itself is not a stat: a
            // constant would print as 0.  It is in sim.cfg and the log.)
            registerStatsMetric(name, core_id, "pq_max_occupancy", &tlb_stats.m_pq_max_occupancy);
            registerStatsMetric(name, core_id, "pq_enqueued", &tlb_stats.m_pq_enqueued);
            registerStatsMetric(name, core_id, "pq_purged_stale", &tlb_stats.m_pq_purged_stale);
            registerStatsMetric(name, core_id, "pq_train_skipped_level_hit", &tlb_stats.m_pq_train_skipped_level_hit);
        }

        // Legacy whole-batch dedup.  Correct only for a prefetcher whose batch
        // lives in a single region; for prefetchers whose batches straddle
        // region boundaries it both over- and under-blocks.  Off by default;
        // kept selectable so pre-fix runs can be reproduced.
        m_pq_dedup_whole_batch = Sim()->getCfg()->getBoolDefault(cfgname + "/dedup_whole_batch", false);

        // PQ dedup granularity: region id = address >> m_pq_region_bits.
        //
        // This used to probe the literal key
        //   perf_model/mmu/tlb_prefetch/pq1/temporal_pte_prefetcher/region_shift
        // with the MMU name, the PQ index and the prefetcher name all hardcoded.
        // Any other MMU name (e.g. mmu_valinor), any PQ but pq1, or any
        // prefetcher but temporal_pte silently fell back to 32KB regions -- so
        // one prefetcher's tuning parameter set the dedup granularity for every
        // other prefetcher.  Resolve it against THIS PQ's own config subtree
        // (cfgname is the pq's config path), and let a PQ state it outright.
        m_pq_region_bits = 15;  // default: page_shift 12 + region_shift 3 = 32KB
        if (Sim()->getCfg()->hasKey(cfgname + "/dedup_region_bits"))
        {
            m_pq_region_bits = static_cast<uint32_t>(Sim()->getCfg()->getInt(cfgname + "/dedup_region_bits"));
            LOG_ASSERT_ERROR(m_pq_region_bits >= 12 && m_pq_region_bits < 64,
                "%s/dedup_region_bits must be in [12,64) (got %u)", cfgname.c_str(), m_pq_region_bits);
        }
        else if (Sim()->getCfg()->hasKey(cfgname + "/temporal_pte_prefetcher/region_shift"))
        {
            uint32_t region_shift = static_cast<uint32_t>(Sim()->getCfg()->getInt(cfgname + "/temporal_pte_prefetcher/region_shift"));
            uint32_t page_shift = Sim()->getCfg()->hasKey(cfgname + "/temporal_pte_prefetcher/page_shift")
                ? static_cast<uint32_t>(Sim()->getCfg()->getInt(cfgname + "/temporal_pte_prefetcher/page_shift")) : 12;
            m_pq_region_bits = page_shift + region_shift;
        }

        // Printed here, not in the stats block above: both fields are assigned
        // further down the constructor, so an earlier print reads them
        // uninitialised.
        if (m_prefetch)
        {
            std::cout << "[TLB] PQ " << m_name << " in-flight prefetch queue cap: "
                      << max_prefetch_count
                      << ", dedup region bits: " << m_pq_region_bits
                      << " (" << (1UL << (m_pq_region_bits - 10)) << " KiB)"
                      << (m_pq_dedup_whole_batch ? ", legacy whole-batch dedup" : "")
                      << std::endl;
        }
    }

    CacheBlockInfo *TLB::lookup(IntPtr address, SubsecondTime now, bool model_count, Core::lock_signal_t lock_signal, IntPtr eip, bool modeled, bool count, PageTable *pt, bool instruction)
    {

        if (m_prefetch)
        {
            tlb_log->debug("Prefetching enabled at time: ", now.getNS(), " ns");

            // Materialize any prefetched translations whose walks have completed
            while (!entry_priority_queue.empty() && entry_priority_queue.top().timestamp <= now)
            {
                query_entry entry = entry_priority_queue.top();
                entry_priority_queue.pop();
                // Decrement region refcount on materialization
                uint64_t region_id = static_cast<uint64_t>(entry.address) >> m_pq_region_bits;
                auto it = m_pq_region_refcount.find(region_id);
                if (it != m_pq_region_refcount.end()) {
                    if (--it->second == 0)
                        m_pq_region_refcount.erase(it);
                }
                tlb_log->debug("Materializing prefetch for address: ", entry.address, " at time: ", now.getNS(), " ns");
                allocate(entry.address, entry.timestamp, false, lock_signal, entry.page_size, entry.ppn, true);
                tlb_stats.m_pq_materialized++;
                if (getenv("RECENCY_DEBUG")) { static long mz=0; if(mz++<400) fprintf(stderr,"[PQMAT] addr=0x%lx now_ns=%lu avail_ns=%lu lag_ns=%ld\n",(unsigned long)entry.address,(unsigned long)now.getNS(),(unsigned long)entry.timestamp.getNS(),(long)((long)now.getNS()-(long)entry.timestamp.getNS())); }
                // Notify prefetchers that this translation has been installed in the TLB
                if (prefetchers != NULL)
                {
                    for (int i = 0; i < number_of_prefetchers; i++)
                        prefetchers[i]->prefetchInstalled(entry.address, entry.page_size);
                }
            }
        }

        if (model_count)
        {
            tlb_stats.m_access++;
            if (getType() == TLBtype::Unified)
            {
                if (instruction)
                    tlb_stats.m_access_instruction++;
                else
                    tlb_stats.m_access_data++;
            }
        }

        tlb_log->debug("Lookup for address: ", address, " at time: ", now.getNS(), " ns");

        CacheBlockInfo *hit = m_cache.accessSingleLineTLB(address, Cache::LOAD, NULL, 0, now, true);

        // Detect whether the hit came from a prefetch-queue-sourced entry.
        // PQ-materialized entries are tagged with CacheBlockInfo::PREFETCH in allocate().
        // Clear the flag on demand consumption so the entry looks normal afterwards.
        bool pq_hit = false;
        if (hit && hit->hasOption(CacheBlockInfo::PREFETCH))
        {
            pq_hit = true;
            hit->clearOption(CacheBlockInfo::PREFETCH);
            tlb_stats.m_pq_hits++;
            if (getenv("RECENCY_DEBUG")) { static long ph=0; if(ph++<400) fprintf(stderr,"[PQHIT] addr=0x%lx now_ns=%lu\n",(unsigned long)address,(unsigned long)now.getNS()); }
        }

        if (hit)
            tlb_log->debug("Hit at time: ", now.getNS(), " ns", pq_hit ? " (PQ)" : "");
        if (!hit)
            tlb_log->debug("Miss at time: ", now.getNS(), " ns");

        // Train the prefetchers.  In deferred mode (set by the MMU, see
        // setDeferPrefetch) the access is only recorded here; the MMU calls
        // resolveDeferredPrefetch() once it knows whether the regular TLB(s) of
        // this level hit, and the prefetchers run only on a miss.
        if (m_prefetch && prefetchers != NULL && pt != NULL)
        {
            int hit_page_size = hit ? hit->getPageSize() : 0;  // 0 = unknown (miss), let prefetcher discover
            if (m_defer_prefetch)
            {
                m_deferred.valid = true;
                m_deferred.address = address;   m_deferred.eip = eip;
                m_deferred.lock = lock_signal;  m_deferred.modeled = modeled;
                m_deferred.count = count;       m_deferred.pt = pt;
                m_deferred.instruction = instruction;
                m_deferred.tlb_hit = (hit != NULL);
                m_deferred.pq_hit = pq_hit;
                m_deferred.page_size = hit_page_size;
                m_deferred.now = now;
            }
            else
                runPrefetchers(address, eip, lock_signal, modeled, count, pt, instruction, hit != NULL, pq_hit, hit_page_size, now);
        }

        if (hit)
        {
            tlb_stats.m_hit++;
            if (getType() == TLBtype::Unified)
            {
                if (instruction)
                    tlb_stats.m_hit_instruction++;
                else
                    tlb_stats.m_hit_data++;
            }
            return hit;
        }

        if (model_count)
        {
            tlb_stats.m_miss++; // We reach this point if L1 TLB Miss
            if (getType() == TLBtype::Unified)
            {
                if (instruction)
                    tlb_stats.m_miss_instruction++;
                else
                    tlb_stats.m_miss_data++;
            }
        }

        return NULL;
    }

    /**
     * @brief Ask this PQ's prefetchers for candidates and enqueue them.
     *
     * Called from lookup() (immediate mode) or resolveDeferredPrefetch()
     * (deferred mode: only when the regular TLBs of this level missed).
     */
    void TLB::runPrefetchers(IntPtr address, IntPtr eip, Core::lock_signal_t lock_signal, bool modeled, bool count, PageTable *pt, bool instruction, bool tlb_hit, bool pq_hit, int hit_page_size, SubsecondTime now)
    {
        tlb_log->debug("Generating prefetches at time: ", now.getNS(), " ns");

        size_t current_prefetches = entry_priority_queue.size();
        if (current_prefetches > tlb_stats.m_pq_max_occupancy)
            tlb_stats.m_pq_max_occupancy = current_prefetches;

        for (int i = 0; i < number_of_prefetchers; i++)
        {
            // The in-flight queue is at its cap, so this prefetcher is not
            // consulted at all.  That costs more than the lost prefetch:
            // these prefetchers train inside performPrefetch(), so a skipped
            // call also leaves a hole in the access history they learn from.
            // Counted here so the cost is visible instead of silent.
            if (current_prefetches >= static_cast<size_t>(max_prefetch_count))
            {
                tlb_stats.m_pq_full_skipped_calls++;
                continue;
            }

            tlb_log->debug("Using prefetcher ", i, " at time: ", now.getNS(), " ns");

            std::vector<query_entry> generated_prefetches = prefetchers[i]->invoke(address, eip, lock_signal, modeled, count, pt, instruction, /*tlb_hit=*/tlb_hit, /*pq_hit=*/pq_hit, /*page_size=*/hit_page_size);
            tlb_log->debug("Prefetcher ", i, " generated ", generated_prefetches.size(), " prefetches at time: ", now.getNS(), " ns");

            // PQ dedup at region granularity: drop a prefetch whose region
            // already has an entry in flight.
            //
            // This used to test only the FIRST valid entry's region and then
            // discard the whole batch on a match.  That is equivalent to the
            // per-entry test below for a prefetcher whose batch lives in one
            // region (TemporalPTE: 1 PTW -> 8 PTEs), which is what it was
            // written for.  It is not equivalent for the stride-style
            // prefetchers (ASP, Berti, DP, H2), whose batches straddle region
            // boundaries: there, one already-pending region killed entries for
            // regions nobody had touched, so a prefetcher suppressed itself in
            // proportion to how far it strides.  Test each entry against its
            // own region instead.
            size_t entries_deduped = 0;
            size_t entries_valid = 0;

            // Legacy path: inspect the first valid entry only, drop everything
            // on a match.  Retained verbatim for A/B against the fixed logic.
            bool legacy_batch_deduped = false;
            if (m_pq_dedup_whole_batch)
            {
                for (auto &first_valid : generated_prefetches)
                {
                    if (first_valid.ppn == 0) continue;
                    uint64_t r = static_cast<uint64_t>(first_valid.address) >> m_pq_region_bits;
                    if (m_pq_region_refcount.count(r) > 0)
                        legacy_batch_deduped = true;
                    break;
                }
                if (legacy_batch_deduped)
                {
                    for (auto &pref : generated_prefetches)
                        if (pref.ppn != 0) tlb_stats.m_pq_dedup_skipped_entries++;
                    tlb_stats.m_pq_dedup_skipped++;
                    continue;
                }
            }

            // Regions this batch itself introduced.  A region must only be
            // deduped against prefetches that were already in flight BEFORE
            // this batch: without this, the first entry of a batch would
            // raise the refcount and every later entry sharing its region
            // would be dropped as a duplicate of its own batch -- which for
            // TemporalPTE (8 PTEs, one region) would enqueue 1 instead of 8.
            std::unordered_set<uint64_t> batch_new_regions;

            for (auto &pref : generated_prefetches)
            {
                if (pref.ppn == 0)
                    continue;
                entries_valid++;

                uint64_t region_id = static_cast<uint64_t>(pref.address) >> m_pq_region_bits;
                bool pending_before_batch = !m_pq_dedup_whole_batch
                                            && m_pq_region_refcount.count(region_id) > 0
                                            && batch_new_regions.count(region_id) == 0;
                if (pending_before_batch)
                {
                    entries_deduped++;
                    tlb_stats.m_pq_dedup_skipped_entries++;
                    continue;
                }

                if (current_prefetches >= static_cast<size_t>(max_prefetch_count))
                {
                    // Cap reached mid-batch.  The walk behind this entry has
                    // already been performed and charged, so the traffic is
                    // spent whether or not we keep the result.
                    tlb_stats.m_pq_full_dropped_entries++;
                    continue;
                }

                tlb_log->debug("Adding prefetch for address: ", pref.address, " at time: ", now.getNS(), " ns");

                entry_priority_queue.push(pref);
                m_pq_region_refcount[region_id]++;
                batch_new_regions.insert(region_id);
                current_prefetches++;
                tlb_stats.m_pq_enqueued++;
            }

            if (current_prefetches > tlb_stats.m_pq_max_occupancy)
                tlb_stats.m_pq_max_occupancy = current_prefetches;

            // Preserve the original counter's meaning (whole batch lost to
            // dedup) so it stays comparable with previously collected runs.
            if (!m_pq_dedup_whole_batch && entries_valid > 0 && entries_deduped == entries_valid)
                tlb_stats.m_pq_dedup_skipped++;
        }
    }

    /**
     * @brief Run (or drop) the prefetcher call recorded by the last lookup().
     *
     * @param level_missed true if no regular TLB at this PQ's level hit, i.e.
     *        the access is a miss at this level (a PQ hit still counts as a
     *        miss: the prefetch queue served a translation the TLB lacked).
     */
    void TLB::resolveDeferredPrefetch(bool level_missed)
    {
        if (!m_deferred.valid)
            return;
        m_deferred.valid = false;
        if (!level_missed)
        {
            tlb_stats.m_pq_train_skipped_level_hit++;
            return;
        }
        runPrefetchers(m_deferred.address, m_deferred.eip, m_deferred.lock, m_deferred.modeled, m_deferred.count,
                       m_deferred.pt, m_deferred.instruction, m_deferred.tlb_hit, m_deferred.pq_hit,
                       m_deferred.page_size, m_deferred.now);
    }

    TLBAllocResult TLB::allocate(IntPtr address, SubsecondTime now, bool count, Core::lock_signal_t lock_signal, int page_size, IntPtr ppn, bool self_alloc, bool instruction)
    {
        if (getPrefetch() && !self_alloc)
        {
            return TLBAllocResult(false, 0, 0, 0);
        }
        IntPtr evict_addr;
        CacheBlockInfo evict_block_info;

        IntPtr tag;
        UInt32 set_index;

        m_cache.splitAddressTLB(address, tag, set_index, page_size);

        tlb_log->debug("Allocate ", address, " at level: ", m_name.c_str(), " with page_size ", page_size, " and tag ", tag);

        bool eviction = false;
        m_cache.insertSingleLineTLB(address, NULL, &eviction, &evict_addr, &evict_block_info, NULL, now, NULL, CacheBlockInfo::block_type_t::DATA, page_size, ppn);

        // Mark prefetch-queue-sourced entries so lookup() can detect PQ hits
        if (self_alloc)
        {
            CacheBlockInfo *inserted = m_cache.accessSingleLineTLB(address, Cache::LOAD, NULL, 0, now, false);
            if (inserted)
                inserted->setOption(CacheBlockInfo::PREFETCH);
        }

        if(count || self_alloc)
        {
            tlb_stats.m_insertions++;
            if (getType() == TLBtype::Unified)
            {
                if (instruction)
                    tlb_stats.m_insertions_instruction++;
                else
                    tlb_stats.m_insertions_data++;
            }
        }

        if (eviction && (count || self_alloc))
        {
            tlb_stats.m_eviction++;
            if (getType() == TLBtype::Unified)
            {
                if (instruction)
                    tlb_stats.m_eviction_instruction++;
                else
                    tlb_stats.m_eviction_data++;
            }
        }

        if (eviction)
            tlb_log->debug("Evicted ", evict_addr, " from level: ", m_name.c_str(), " with page_size ", page_size);

        // Notify this TLB's own prefetchers about the evicted victim so they
        // can maintain eviction-aware structures (e.g., the recency list).
        if (eviction && prefetchers != NULL)
        {
            // Only prefetch-queue TLBs own prefetchers, so this eviction is a
            // prefetched entry leaving the PQ: report it through its own hook.
            for (int i = 0; i < number_of_prefetchers; i++)
            {
                if (m_prefetch)
                    prefetchers[i]->pqEntryEvicted(evict_addr, evict_block_info.getPageSize(), evict_block_info.getPPN());
                else
                    prefetchers[i]->victimEvicted(evict_addr, evict_block_info.getPageSize(), evict_block_info.getPPN());
            }
        }

        // Notify external victim observers (PQ prefetchers wired by the
        // TLB subsystem via addVictimObserver).  This is the primary path
        // for regular (non-PQ) TLBs whose evictions must reach PQ
        // prefetchers that use victim-based tracking.
        if (eviction && !m_victim_observers.empty())
        {
            for (auto *obs : m_victim_observers)
                obs->victimEvicted(evict_addr, evict_block_info.getPageSize(), evict_block_info.getPPN());
        }

        return TLBAllocResult(eviction, evict_addr, evict_block_info.getPageSize(), evict_block_info.getPPN());
    }

    bool TLB::invalidate(IntPtr address, int page_size)
    {
        // Use the TLB-specific invalidation method that uses splitAddressTLB
        bool invalidated = m_cache.invalidateSingleLineTLB(address, page_size);
        
        if (invalidated)
        {
            tlb_log->debug("Invalidated entry for address ", address, " page_size ", page_size);
        }

        // Drop any in-flight prefetch for this page too.  A queued entry carries
        // the PPN its walk resolved; after a shootdown (page migration, tier
        // move, unmap) that PPN is stale, and materializing it later would
        // reinstall the pre-migration translation into a TLB that was just
        // invalidated.  Invalidating only m_cache leaves that hole open.
        purgePQ(address, page_size);

        return invalidated;
    }

    /**
     * @brief Remove in-flight prefetch-queue entries covering a virtual address.
     *
     * Rebuilds the queue without the matching entries and releases the region
     * refcount each one held, keeping the push/pop invariant intact (the
     * destructor asserts it).
     *
     * @param address Virtual address being invalidated
     * @param page_size Page size in bits, or 0 to match any page size
     * @return Number of queued entries discarded
     */
    UInt64 TLB::purgePQ(IntPtr address, int page_size)
    {
        if (!m_prefetch || entry_priority_queue.empty())
            return 0;

        // Scan a COPY first and bail out if nothing matches.  Draining and
        // refilling the heap is not order-preserving: Compare ranks only on
        // timestamp, so entries sharing a timestamp come back in a different
        // order, which changes the order they materialize into the TLB.  Doing
        // that on every invalidate -- the overwhelmingly common case, where no
        // queued entry matches at all -- perturbs results for no reason.
        {
            std::priority_queue<query_entry, std::vector<query_entry>, Compare> probe(entry_priority_queue);
            bool any_match = false;
            while (!probe.empty())
            {
                const query_entry &e = probe.top();
                const int ps = (page_size != 0) ? page_size : e.page_size;
                if ((e.address >> ps) == (address >> ps)
                    && (page_size == 0 || e.page_size == page_size))
                {
                    any_match = true;
                    break;
                }
                probe.pop();
            }
            if (!any_match)
                return 0;
        }

        std::vector<query_entry> kept;
        kept.reserve(entry_priority_queue.size());
        UInt64 purged = 0;

        while (!entry_priority_queue.empty())
        {
            query_entry e = entry_priority_queue.top();
            entry_priority_queue.pop();

            const int ps = (page_size != 0) ? page_size : e.page_size;
            const bool same_page = (e.address >> ps) == (address >> ps);
            const bool same_size = (page_size == 0) || (e.page_size == page_size);

            if (same_page && same_size)
            {
                uint64_t region_id = static_cast<uint64_t>(e.address) >> m_pq_region_bits;
                auto it = m_pq_region_refcount.find(region_id);
                if (it != m_pq_region_refcount.end() && --it->second == 0)
                    m_pq_region_refcount.erase(it);
                purged++;
                tlb_stats.m_pq_purged_stale++;
            }
            else
            {
                kept.push_back(e);
            }
        }

        for (auto &e : kept)
            entry_priority_queue.push(e);

        if (purged)
            tlb_log->debug("Purged ", purged, " in-flight prefetch(es) for address ", address);

        return purged;
    }

    bool TLB::contains(IntPtr address, int page_size) const
    {
        // Check if entry exists without modifying anything (for sanity checks)
        return m_cache.containsTLB(address, page_size);
    }

    TLB::~TLB()
    {
        // Invariant: every queued entry holds exactly one refcount on its own
        // region, so the refcounts must sum to the number of entries still in
        // flight.  A mismatch means a push/pop path stopped being balanced and
        // some region is being permanently treated as pending -- which silently
        // suppresses every future prefetch into it.
        if (m_prefetch)
        {
            UInt64 refcount_sum = 0;
            for (const auto &kv : m_pq_region_refcount)
                refcount_sum += kv.second;
            if (refcount_sum != entry_priority_queue.size())
            {
                std::cout << "[TLB] WARNING " << m_name
                          << ": PQ region refcount sum (" << refcount_sum
                          << ") != in-flight queue size (" << entry_priority_queue.size()
                          << ") -- dedup accounting is unbalanced" << std::endl;
            }
        }

        delete tlb_log;
        
        if (prefetchers != NULL)
        {
            for (int i = 0; i < number_of_prefetchers; i++)
            {
                delete prefetchers[i];
            }
            free(prefetchers);
            prefetchers = NULL;
        }
    }

}
