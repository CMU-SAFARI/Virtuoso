// ============================================================================
// @kanellok: MMU Utopia Coalesce - Dual Radix Walk (2MB + 4KB)
// ============================================================================
//
// This file implements the coalesce-aware Utopia MMU.  It inherits all
// functionality from MemoryManagementUnitUtopia and overrides only
// RestSegWalk() to walk the coalesced 2MB radix tree FIRST before
// falling through to the standard 4KB radix tree.
//
// Translation path (per RestSeg):
//   1.  Walk 2MB radix tree (3 cache-line reads) → if hit, done
//   2.  Walk 4KB radix tree (4 cache-line reads) → if hit, done
//   3.  Miss → fall through to FlexSeg PTW
//
// Everything else (TLB, UTLB, CATS, migration, stats) is inherited.
//
// ============================================================================

#include "mmu_utopia_coalesce.h"
#include "mmu_base.h"
#include "memory_manager.h"
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "core.h"
#include "thread.h"
#include "memory_management/physical_memory_allocators/utopia_hash_coalesce.h"
#include "mimicos.h"
#include <iostream>
#include <algorithm>

using namespace std;

namespace ParametricDramDirectoryMSI
{

// ============================================================================
// Construction / Destruction
// ============================================================================

	MemoryManagementUnitUtopiaCoalesce::MemoryManagementUnitUtopiaCoalesce(
		Core *_core, MemoryManagerBase *_memory_manager,
		ShmemPerfModel *_shmem_perf_model, String _name,
		MemoryManagementUnitBase *_nested_mmu)
		: MemoryManagementUnitUtopia(_core, _memory_manager, _shmem_perf_model, _name, _nested_mmu)
	{
		registerCoalesceMMUStats();

		if (core->getId() == 0) {
			std::cout << "[MMU::UtopiaCoalesce] Dual radix walk enabled (2MB first, 4KB fallback)" << std::endl;
		}
	}

	MemoryManagementUnitUtopiaCoalesce::~MemoryManagementUnitUtopiaCoalesce()
	{
		// Parent destructor handles all shared cleanup
	}

// ============================================================================
// Coalesce-specific stats registration
// ============================================================================

	void MemoryManagementUnitUtopiaCoalesce::registerCoalesceMMUStats()
	{
		// 2MB radix walk stats
		registerStatsMetric(name, core->getId(), "radix_2mb_lookups",  &coalesce_stats.radix_2mb_lookups);
		registerStatsMetric(name, core->getId(), "radix_2mb_hits",     &coalesce_stats.radix_2mb_hits);
		registerStatsMetric(name, core->getId(), "radix_2mb_misses",   &coalesce_stats.radix_2mb_misses);

		// 2MB radix PWC stats
		registerStatsMetric(name, core->getId(), "radix_2mb_pwc_hits",   &coalesce_stats.radix_2mb_pwc_hits);
		registerStatsMetric(name, core->getId(), "radix_2mb_pwc_misses", &coalesce_stats.radix_2mb_pwc_misses);

		// 2MB radix internal node memory hierarchy
		registerStatsMetric(name, core->getId(), "radix_2mb_internal_hit_l2",   &coalesce_stats.radix_2mb_internal_hit_l2);
		registerStatsMetric(name, core->getId(), "radix_2mb_internal_hit_nuca", &coalesce_stats.radix_2mb_internal_hit_nuca);
		registerStatsMetric(name, core->getId(), "radix_2mb_internal_hit_dram", &coalesce_stats.radix_2mb_internal_hit_dram);
		registerStatsMetric(name, core->getId(), "radix_2mb_internal_accesses", &coalesce_stats.radix_2mb_internal_accesses);

		// 2MB radix leaf node memory hierarchy
		registerStatsMetric(name, core->getId(), "radix_2mb_leaf_hit_l2",   &coalesce_stats.radix_2mb_leaf_hit_l2);
		registerStatsMetric(name, core->getId(), "radix_2mb_leaf_hit_nuca", &coalesce_stats.radix_2mb_leaf_hit_nuca);
		registerStatsMetric(name, core->getId(), "radix_2mb_leaf_hit_dram", &coalesce_stats.radix_2mb_leaf_hit_dram);
		registerStatsMetric(name, core->getId(), "radix_2mb_leaf_accesses", &coalesce_stats.radix_2mb_leaf_accesses);

		// Total 2MB radix latency
		registerStatsMetric(name, core->getId(), "radix_2mb_latency_total", &coalesce_stats.radix_2mb_latency_total);
	}

// ============================================================================
// RestSeg Walk Override - Dual Radix (2MB + 4KB)
// ============================================================================

	std::tuple<int, IntPtr, SubsecondTime>
	MemoryManagementUnitUtopiaCoalesce::RestSegWalk(
		IntPtr address, bool instruction, IntPtr eip,
		Core::lock_signal_t lock, bool modeled, bool count)
	{
		SubsecondTime t_start = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

		// Cast allocator to UtopiaCoalesce (our allocator type)
		UtopiaCoalesce *m_utopia = dynamic_cast<UtopiaCoalesce*>(Sim()->getMimicOS()->getMemoryAllocator());
		if (!m_utopia) {
			// Fallback to parent's RestSegWalk if allocator isn't UtopiaCoalesce
			return MemoryManagementUnitUtopia::RestSegWalk(address, instruction, eip, lock, modeled, count);
		}

		int app_id = core->getThread() ? core->getThread()->getAppId() : 0;
		int num_restsegs = m_utopia->getNumRestSegs();

		IntPtr final_address = 0;
		int page_size = -1;
		SubsecondTime result_latency = SubsecondTime::Zero();
		int hit_restseg = -1;

		// ====================================================================
		// Per-RestSeg access state
		// ====================================================================
		struct RestSegAccessCoalesce {
			bool hit = false;
			bool accessed = false;
			bool hit_2mb = false;             ///< Hit came from 2MB radix tree
			int page_size_bits = 0;
			IntPtr physical_address = 0;
			int correct_way = -1;
		};

		std::vector<RestSegAccessCoalesce> rs_access(num_restsegs);

		// ====================================================================
		// Lambda: access a RestSeg via DUAL RADIX walk (2MB first, 4KB fallback)
		// ====================================================================
		auto accessRestSegDualRadix = [&](int i, SubsecondTime access_start) -> SubsecondTime {
			auto restseg = m_utopia->getRestSeg(i);
			if (!restseg) return SubsecondTime::Zero();

			rs_access[i].page_size_bits = restseg->getPageSizeBits();
			rs_access[i].accessed = true;

			SubsecondTime total_latency = SubsecondTime::Zero();
			SubsecondTime current_time = access_start;

			// ============================================================
			// STEP 1: Walk the 2MB radix tree (3 levels for assoc=8)
			// ============================================================
			if (count) coalesce_stats.radix_2mb_lookups++;

			int way_2mb = -1;
			bool radix_2mb_hit = false;

			// Functional check: is there a coalesced 2MB entry?
			int coalesced_way = restseg->lookupCoalesced2MBWay(
				(address >> 12) & ~((1ULL << 9) - 1),  // align VPN to 2MB boundary
				app_id);

			if (coalesced_way >= 0) {
				radix_2mb_hit = true;
				way_2mb = coalesced_way;
			}

			// Model 2MB radix walk latency regardless of hit/miss
			// (we always need to walk the tree to find out)
			std::vector<std::pair<IntPtr, bool>> level_addresses_2mb;
			restseg->calculateRadixLevelAddressesCoalesced(address, app_id, level_addresses_2mb);

			SubsecondTime radix_2mb_latency = SubsecondTime::Zero();
			for (size_t level_idx = 0; level_idx < level_addresses_2mb.size(); level_idx++)
			{
				IntPtr level_addr = level_addresses_2mb[level_idx].first;
				bool is_leaf = level_addresses_2mb[level_idx].second;

				// Check PWC (page walk cache)
				bool pwc_hit = ptw_filter->lookupPWC(level_addr, current_time, level_idx, count);

				if (count) {
					if (pwc_hit) coalesce_stats.radix_2mb_pwc_hits++;
					else         coalesce_stats.radix_2mb_pwc_misses++;
				}

				if (!pwc_hit) {
					// PWC miss: access cache hierarchy
					translationPacket packet;
					packet.eip = eip;
					packet.address = level_addr;
					packet.instruction = instruction;
					packet.lock_signal = lock;
					packet.modeled = modeled;
					packet.count = count;
					packet.type = is_leaf ? CacheBlockInfo::block_type_t::UTOPIA_RADIX_LEAF
					                      : CacheBlockInfo::block_type_t::UTOPIA_RADIX_INTERNAL;

					HitWhere::where_t hit_where;
					SubsecondTime level_latency = accessCache(packet, current_time, false, hit_where);
					radix_2mb_latency += level_latency;
					current_time = current_time + level_latency;

					if (count) {
						translation_stats.total_rsw_memory_requests++;

						if (is_leaf) {
							coalesce_stats.radix_2mb_leaf_accesses++;
							if (hit_where == HitWhere::L2_OWN)
								coalesce_stats.radix_2mb_leaf_hit_l2++;
							else if (hit_where == HitWhere::NUCA_CACHE || hit_where == HitWhere::L3_OWN)
								coalesce_stats.radix_2mb_leaf_hit_nuca++;
							else if (hit_where == HitWhere::DRAM || hit_where == HitWhere::DRAM_LOCAL ||
							         hit_where == HitWhere::DRAM_REMOTE || hit_where == HitWhere::MISS)
								coalesce_stats.radix_2mb_leaf_hit_dram++;
						} else {
							coalesce_stats.radix_2mb_internal_accesses++;
							if (hit_where == HitWhere::L2_OWN)
								coalesce_stats.radix_2mb_internal_hit_l2++;
							else if (hit_where == HitWhere::NUCA_CACHE || hit_where == HitWhere::L3_OWN)
								coalesce_stats.radix_2mb_internal_hit_nuca++;
							else if (hit_where == HitWhere::DRAM || hit_where == HitWhere::DRAM_LOCAL ||
							         hit_where == HitWhere::DRAM_REMOTE || hit_where == HitWhere::MISS)
								coalesce_stats.radix_2mb_internal_hit_dram++;
						}
					}
				}
			}

			total_latency += radix_2mb_latency;

			if (count)
				coalesce_stats.radix_2mb_latency_total += radix_2mb_latency;

			// ============================================================
			// If 2MB radix hit → we're done for this RestSeg
			// ============================================================
			if (radix_2mb_hit) {
				if (count) coalesce_stats.radix_2mb_hits++;

				rs_access[i].hit = true;
				rs_access[i].hit_2mb = true;
				rs_access[i].correct_way = way_2mb;
				// Calculate physical address using the coalesced way
				// For a 2MB coalesced region, the PPN covers the full 2MB
				rs_access[i].physical_address = restseg->calculatePPNFromWay(address, way_2mb);

				// Touch LRU for this way
				restseg->touchWayLRU(address, way_2mb);

				// Track standard radix stats (keep consistent with parent)
				if (count) {
					if (i == 0) {
						translation_stats.radix_lookups_restseg0++;
						translation_stats.radix_hits_restseg0++;
					} else {
						translation_stats.radix_lookups_restseg1++;
						translation_stats.radix_hits_restseg1++;
					}
				}

				return total_latency;
			}

			// ============================================================
			// 2MB miss → fall through to 4KB radix tree (4 levels)
			// ============================================================
			if (count) coalesce_stats.radix_2mb_misses++;

			int way_4kb = -1;
			bool radix_4kb_hit = restseg->lookupWayRadix(address, app_id, way_4kb);

			if (radix_4kb_hit && way_4kb >= 0) {
				rs_access[i].hit = true;
				rs_access[i].hit_2mb = false;
				rs_access[i].correct_way = way_4kb;
				rs_access[i].physical_address = restseg->calculatePPNFromWay(address, way_4kb);

				// Touch LRU
				restseg->touchWayLRU(address, way_4kb);
			}

			// Track 4KB radix stats
			if (count) {
				if (i == 0) {
					translation_stats.radix_lookups_restseg0++;
					if (radix_4kb_hit) translation_stats.radix_hits_restseg0++;
					else               translation_stats.radix_misses_restseg0++;
				} else {
					translation_stats.radix_lookups_restseg1++;
					if (radix_4kb_hit) translation_stats.radix_hits_restseg1++;
					else               translation_stats.radix_misses_restseg1++;
				}
			}

			// Model 4KB radix walk latency
			std::vector<std::pair<IntPtr, bool>> level_addresses_4kb;
			restseg->calculateRadixLevelAddresses(address, app_id, level_addresses_4kb);

			SubsecondTime radix_4kb_latency = SubsecondTime::Zero();
			for (size_t level_idx = 0; level_idx < level_addresses_4kb.size(); level_idx++)
			{
				IntPtr level_addr = level_addresses_4kb[level_idx].first;
				bool is_leaf = level_addresses_4kb[level_idx].second;

				bool pwc_hit = ptw_filter->lookupPWC(level_addr, current_time, level_idx, count);

				if (count) {
					if (pwc_hit) translation_stats.radix_pwc_hits++;
					else         translation_stats.radix_pwc_misses++;
				}

				if (!pwc_hit) {
					translationPacket packet;
					packet.eip = eip;
					packet.address = level_addr;
					packet.instruction = instruction;
					packet.lock_signal = lock;
					packet.modeled = modeled;
					packet.count = count;
					packet.type = is_leaf ? CacheBlockInfo::block_type_t::UTOPIA_RADIX_LEAF
					                      : CacheBlockInfo::block_type_t::UTOPIA_RADIX_INTERNAL;

					HitWhere::where_t hit_where;
					SubsecondTime level_latency = accessCache(packet, current_time, false, hit_where);
					radix_4kb_latency += level_latency;
					current_time = current_time + level_latency;

					if (count) translation_stats.total_rsw_memory_requests++;

					if (count) {
						if (is_leaf) {
							translation_stats.radix_leaf_accesses++;
							if (hit_where == HitWhere::L2_OWN)
								translation_stats.radix_leaf_hit_l2++;
							else if (hit_where == HitWhere::NUCA_CACHE || hit_where == HitWhere::L3_OWN)
								translation_stats.radix_leaf_hit_nuca++;
							else if (hit_where == HitWhere::DRAM || hit_where == HitWhere::DRAM_LOCAL ||
							         hit_where == HitWhere::DRAM_REMOTE || hit_where == HitWhere::MISS)
								translation_stats.radix_leaf_hit_dram++;
						} else {
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
			}

			total_latency += radix_4kb_latency;

			// Track radix latency per RestSeg
			if (count) {
				if (i == 0)
					translation_stats.radix_latency_restseg0 += total_latency;
				else
					translation_stats.radix_latency_restseg1 += total_latency;
			}

			return total_latency;
		};

		// ====================================================================
		// ADAPTIVE ACCESS: Determine sequential vs parallel (same as parent)
		// ====================================================================
		bool use_sequential = false;
		int first_restseg = 0;

		if (num_restsegs > 1 && adaptive_rsw_enabled && rsw_total_hits >= rsw_warmup_accesses)
		{
			double hit_rate_0 = (rsw_total_hits > 0) ? (double)rsw_hits_per_restseg[0] / rsw_total_hits : 0.5;
			double hit_rate_1 = 1.0 - hit_rate_0;

			if (hit_rate_0 >= rsw_sequential_threshold)      { use_sequential = true; first_restseg = 0; rsw_dominant_restseg = 0; }
			else if (hit_rate_1 >= rsw_sequential_threshold) { use_sequential = true; first_restseg = 1; rsw_dominant_restseg = 1; }
			else                                              { rsw_dominant_restseg = -1; }
		}

		// ====================================================================
		// SINGLE RESTSEG MODE
		// ====================================================================
		if (num_restsegs == 1)
		{
			result_latency = accessRestSegDualRadix(0, t_start);
			if (rs_access[0].hit)
			{
				hit_restseg = 0;
				page_size = rs_access[0].hit_2mb ? 21 : rs_access[0].page_size_bits;
				final_address = rs_access[0].physical_address;
			}
		}
		// ====================================================================
		// SEQUENTIAL ACCESS MODE
		// ====================================================================
		else if (use_sequential)
		{
			if (count) translation_stats.rsw_sequential_accesses++;

			int second_restseg = (first_restseg == 0) ? 1 : 0;

			SubsecondTime first_latency = accessRestSegDualRadix(first_restseg, t_start);
			if (rs_access[first_restseg].hit)
			{
				hit_restseg = first_restseg;
				page_size = rs_access[first_restseg].hit_2mb ? 21 : rs_access[first_restseg].page_size_bits;
				final_address = rs_access[first_restseg].physical_address;
				result_latency = first_latency;
			}
			else
			{
				SubsecondTime t_second = t_start + first_latency;
				SubsecondTime second_latency = accessRestSegDualRadix(second_restseg, t_second);

				if (rs_access[second_restseg].hit)
				{
					hit_restseg = second_restseg;
					page_size = rs_access[second_restseg].hit_2mb ? 21 : rs_access[second_restseg].page_size_bits;
					final_address = rs_access[second_restseg].physical_address;
				}
				result_latency = first_latency + second_latency;
			}
		}
		// ====================================================================
		// PARALLEL ACCESS MODE
		// ====================================================================
		else
		{
			if (count) translation_stats.rsw_parallel_accesses++;

			std::vector<SubsecondTime> rs_lat(num_restsegs);
			for (int i = 0; i < num_restsegs; i++)
				rs_lat[i] = accessRestSegDualRadix(i, t_start);

			// Find hit
			for (int i = 0; i < num_restsegs; i++)
			{
				if (rs_access[i].hit)
				{
					hit_restseg = i;
					page_size = rs_access[i].hit_2mb ? 21 : rs_access[i].page_size_bits;
					final_address = rs_access[i].physical_address;
					break;
				}
			}

			// Latency: hitting segment or max of all
			if (hit_restseg >= 0)
				result_latency = rs_lat[hit_restseg];
			else
				for (int i = 0; i < num_restsegs; i++)
					result_latency = max(result_latency, rs_lat[i]);
		}

		// ====================================================================
		// Update hit tracking for adaptive access
		// ====================================================================
		if (hit_restseg >= 0)
		{
			rsw_total_hits++;
			rsw_hits_per_restseg[hit_restseg]++;

			if (count)
			{
				if (hit_restseg == 0) translation_stats.rsw_hits_restseg0++;
				else                  translation_stats.rsw_hits_restseg1++;
			}
		}

		return make_tuple(page_size, final_address, result_latency);
	}

} // namespace ParametricDramDirectoryMSI
