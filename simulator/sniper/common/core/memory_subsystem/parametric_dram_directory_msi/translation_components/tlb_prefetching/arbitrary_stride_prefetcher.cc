#include "arbitrary_stride_prefetcher.h"
#include "cache_cntlr.h"
#include "stats.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <iostream>

namespace ParametricDramDirectoryMSI
{

	ArbitraryStridePrefetcher::ArbitraryStridePrefetcher(
		Core *_core, MemoryManagerBase *_memory_manager, ShmemPerfModel *_shmem_perf_model,
		int table_entries, int _prefetch_threshold, bool _extra_prefetch,
		int _lookahead, int _degree, String name, bool _install_pq, bool _skip_zero_stride)
		: TLBPrefetcherBase(_core, _memory_manager, _shmem_perf_model, name),
		  core(_core),
		  memory_manager(_memory_manager),
		  shmem_perf_model(_shmem_perf_model),
		  prefetch_threshold(_prefetch_threshold),
		  extra_prefetch(_extra_prefetch),
		  lookahead(_lookahead),
		  degree(_degree),
		  install_pq(_install_pq),
		  skip_zero_stride(_skip_zero_stride)
	{
		LOG_ASSERT_ERROR(table_entries >= 1, "ASP table must have at least one entry (got %d)", table_entries);
		int entries = table_entries;
		table_size = entries;
		table = new entry_prefetcher[entries];

		for (int i = 0; i < entries; i++)
		{
			table[i].PC = 0;
			table[i].vaddr = 0;
			table[i].stride = -1;
			table[i].stride_valid = false;
			table[i].saturation_counter = 0;
		}

		memset(&stats, 0, sizeof(stats));

		log_file_name = std::string(name.c_str()) + ".log." + std::to_string(core->getId());
		log_file_name = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + log_file_name;
		log_file.open(log_file_name);

		std::cout << logPrefix() << "config: entries=" << entries
		          << " threshold=" << prefetch_threshold << " lookahead=" << lookahead
		          << " degree=" << degree << " extra_prefetch=" << extra_prefetch
		          << " install_pq=" << install_pq << " skip_zero_stride=" << skip_zero_stride
		          << (skip_zero_stride ? "" : "  (LEGACY: stride-0 entries prefetch the page being accessed)")
		          << (entries == 1 ? "  (LEGACY: 1-entry table, as in the pre-2026-09-28 v4 runs)" : "")
		          << std::endl;

		registerStatsMetric("asp_tlb", core->getId(), "successful_prefetches", &stats.successful_prefetches);
		registerStatsMetric("asp_tlb", core->getId(), "prefetch_attempts", &stats.prefetch_attempts);
		registerStatsMetric("asp_tlb", core->getId(), "failed_prefetches", &stats.failed_prefetches);

		registerStatsMetric("asp_tlb", core->getId(), "queries", &stats.queries);
		registerStatsMetric("asp_tlb", core->getId(), "pc_hits", &stats.pc_hits);
		registerStatsMetric("asp_tlb", core->getId(), "pc_misses", &stats.pc_misses);
		registerStatsMetric("asp_tlb", core->getId(), "pc_evictions", &stats.pc_evictions);

		registerStatsMetric("asp_tlb", core->getId(), "new_stride_observed", &stats.new_stride_observed);
		registerStatsMetric("asp_tlb", core->getId(), "stride_same", &stats.stride_same);
		registerStatsMetric("asp_tlb", core->getId(), "stride_change", &stats.stride_change);
		registerStatsMetric("asp_tlb", core->getId(), "zero_stride", &stats.zero_stride);
		registerStatsMetric("asp_tlb", core->getId(), "positive_stride", &stats.positive_stride);
		registerStatsMetric("asp_tlb", core->getId(), "negative_stride", &stats.negative_stride);

		registerStatsMetric("asp_tlb", core->getId(), "threshold_reached", &stats.threshold_reached);
		registerStatsMetric("asp_tlb", core->getId(), "trained_entries", &stats.trained_entries);
		registerStatsMetric("asp_tlb", core->getId(), "trained_but_no_prefetch", &stats.trained_but_no_prefetch);

		registerStatsMetric("asp_tlb", core->getId(), "extra_prefetches_issued", &stats.extra_prefetches_issued);
		registerStatsMetric("asp_tlb", core->getId(), "extra_prefetches_successful", &stats.extra_prefetches_successful);
		registerStatsMetric("asp_tlb", core->getId(), "extra_prefetches_failed", &stats.extra_prefetches_failed);

		registerStatsMetric("asp_tlb", core->getId(), "sum_abs_stride", &stats.sum_abs_stride);
		registerStatsMetric("asp_tlb", core->getId(), "prefetch_distance_sum", &stats.prefetch_distance_sum);
		registerStatsMetric("asp_tlb", core->getId(), "max_saturation_counter", &stats.max_saturation_counter);
		registerStatsMetric("asp_tlb", core->getId(), "table_accesses", &stats.table_accesses);
		registerStatsMetric("asp_tlb", core->getId(), "zero_stride_skipped", &stats.zero_stride_skipped);
		registerStatsMetric("asp_tlb", core->getId(), "targets_out_of_range", &stats.targets_out_of_range);
	}

	std::vector<query_entry> ArbitraryStridePrefetcher::performPrefetch(
		IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count, PageTable *pt, bool instruction, bool tlb_hit, bool pq_hit, int page_size)
	{
		std::vector<query_entry> result;
		int index = eip % table_size;
		if (index < 0)
			index = -index;

		IntPtr VPN = address >> 12;

		stats.queries++;
		stats.table_accesses++;

		if (table[index].PC == eip)
		{
			stats.pc_hits++;

			long long new_stride = static_cast<long long>(VPN) - static_cast<long long>(table[index].vaddr);

			if (new_stride == 0)
				stats.zero_stride++;
			else if (new_stride > 0)
				stats.positive_stride++;
			else
				stats.negative_stride++;

			// Strict mode: a stride-0 observation carries no prediction (the
			// next page would be this page), so it neither trains nor predicts.
			// Just refresh the last VPN and leave the stride/confidence alone.
			if (skip_zero_stride && new_stride == 0)
			{
				stats.zero_stride_skipped++;
				table[index].vaddr = VPN;
				if (!install_pq) result.clear();
				return result;
			}

			// "Unset" test: explicit valid bit in strict mode; the legacy code
			// used stride == -1, which is also a legitimate stride.
			const bool stride_unset = skip_zero_stride ? !table[index].stride_valid
			                                           : (table[index].stride == -1);
			if (stride_unset)
			{
				table[index].stride = new_stride;
				table[index].stride_valid = true;
				table[index].saturation_counter++;
				stats.new_stride_observed++;
			}
			else if (table[index].stride == new_stride)
			{
				table[index].saturation_counter++;
				stats.stride_same++;
			}
			else
			{
				stats.stride_change++;
				table[index].saturation_counter = 0;
				table[index].stride = new_stride;
			}

			if (table[index].saturation_counter > stats.max_saturation_counter)
				stats.max_saturation_counter = table[index].saturation_counter;

			if (table[index].saturation_counter > static_cast<unsigned int>(prefetch_threshold))
			{
				stats.threshold_reached++;

				long long abs_stride = (table[index].stride >= 0) ? table[index].stride : -table[index].stride;
				stats.sum_abs_stride += static_cast<UInt64>(abs_stride);

				// Lookahead + degree: prefetch pages at VPN + (lookahead + i) * stride, for i in [0, degree)
				for (int d = 0; d < degree; d++)
				{
					long long offset = static_cast<long long>(lookahead + d) * table[index].stride;
					long long prefetch_vpn_signed = static_cast<long long>(VPN) + offset;
					IntPtr prefetch_vpn = static_cast<IntPtr>(prefetch_vpn_signed);

					// A stride learned between two unrelated regions (e.g. heap at
					// 0x0b70_9000 and an mmap area at 0x126b_0000_0000: ~4.9e9 pages)
					// can put the target below 0 or beyond the 48-bit address space
					// the page table decodes (bits 48.. carry the app id, see
					// vpnInTriggerSpace).  Such an address cannot be mapped;
					// walking it would alias into the low 48 bits and allocate a
					// junk page.  Drop it before the walk (no walk is charged).
					if (!vpnInTriggerSpace(prefetch_vpn_signed, VPN))
					{
						stats.targets_out_of_range++;
						continue;
					}

					long long dist = (offset >= 0) ? offset : -offset;
					stats.prefetch_distance_sum += static_cast<UInt64>(dist);

					query_entry prefetch_result = PTWTransparent(prefetch_vpn << 12, eip, lock, modeled, count, pt);
					stats.prefetch_attempts++;

					if (prefetch_result.ppn != 0)
					{
						stats.successful_prefetches++;
						stats.trained_entries++;
						result.push_back(prefetch_result);
					}
					else
					{
						stats.failed_prefetches++;
						stats.trained_but_no_prefetch++;
					}
				}

				if (extra_prefetch)
				{
					// One walk behind the current page, at VPN - stride.
					long long extra_vpn_signed = static_cast<long long>(VPN) - table[index].stride;
					if (!vpnInTriggerSpace(extra_vpn_signed, VPN))
					{
						stats.targets_out_of_range++;   // same guard as above; not walked
					}
					else
					{
						stats.extra_prefetches_issued++;
						stats.prefetch_attempts++;   // the extra walk is a walk too
						IntPtr extra_vpn = static_cast<IntPtr>(extra_vpn_signed);
						query_entry extra_result = PTWTransparent(extra_vpn << 12, eip, lock, modeled, count, pt);

						if (extra_result.ppn != 0)
						{
							stats.extra_prefetches_successful++;
							stats.successful_prefetches++;
							result.push_back(extra_result);
						}
						else
						{
							stats.extra_prefetches_failed++;
							stats.failed_prefetches++;
						}
					}
				}
			}

			table[index].vaddr = VPN;
		}
		else
		{
			stats.pc_misses++;
			if (table[index].PC != 0)
				stats.pc_evictions++;

			// New PC (or a conflict in the direct-mapped table): start over.
			table[index].PC = eip;
			table[index].vaddr = VPN;
			table[index].stride = -1;
			table[index].stride_valid = false;
			table[index].saturation_counter = 0;
		}

		// Cache-only mode: the PTWTransparent walks above already warmed the PTE
		// cache lines; dropping the results means nothing is inserted into the PQ.
		if (!install_pq)
			result.clear();

		return result;
	}


	void ArbitraryStridePrefetcher::appendSummary(std::ostream &os) const
	{
		os << " queries=" << stats.queries
		   << " pc_hits=" << stats.pc_hits
		   << " zero_stride=" << stats.zero_stride
		   << " zero_stride_skipped=" << stats.zero_stride_skipped
		   << " out_of_range=" << stats.targets_out_of_range
		   << " trained(>thr)=" << stats.threshold_reached
		   << " walks=" << stats.prefetch_attempts
		   << " walks_ok=" << stats.successful_prefetches
		   << " avg|dist|=" << (stats.prefetch_attempts ? stats.prefetch_distance_sum / stats.prefetch_attempts : 0);
	}

}
