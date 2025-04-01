
#include "tlb_prefetcher_base.h"
#include "arbitrary_stride_prefetcher.h"
#include "cache_cntlr.h"
#include "stats.h"
// #define DEBUG
namespace ParametricDramDirectoryMSI
{
	ArbitraryStridePrefetcher::ArbitraryStridePrefetcher(Core *_core, MemoryManager *_memory_manager, ShmemPerfModel *_shmem_perf_model, int _table_size, int _prefetch_threshold, bool _extra_prefetch, int _lookahead, int _degree) : TLBPrefetcherBase(_core, _memory_manager, _shmem_perf_model), core(_core),
																																																		   memory_manager(_memory_manager),
																																																		   shmem_perf_model(_shmem_perf_model),
																																																		   prefetch_threshold(_prefetch_threshold), 
																																																		   extra_prefetch(_extra_prefetch),
																																																		   lookahead(_lookahead),
																																																		   degree(_degree)
	{
		std::cout << "Arbitrary Stride prefetcher created with table size " << _table_size << " and prefetch threshold " << _prefetch_threshold << std::endl;
		
		stats.successful_prefetches = 0;
		stats.prefetch_attempts = 0;
		stats.failed_prefetches = 0;
		table_size = std::pow(2, _table_size);
		table = (entry_prefetcher *)malloc(table_size * sizeof(entry_prefetcher));
		for (int i = 0; i < table_size; i++)
		{
			table[i].PC = 0;
			table[i].vaddr = 0;
			table[i].stride = 0;
			table[i].saturation_counter = 0;
		}
		registerStatsMetric("asp_tlb", core->getId() , "successful_prefetches", &stats.successful_prefetches);
		registerStatsMetric("asp_tlb", core->getId() , "prefetch_attempts", &stats.prefetch_attempts);
		registerStatsMetric("asp_tlb", core->getId() , "failed_prefetches", &stats.failed_prefetches);

	}

	
	std::vector<query_entry> ArbitraryStridePrefetcher::performPrefetch(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count, PageTable *pt)
	{
		vector<query_entry> result;
		int index = eip % table_size;
		IntPtr VPN = address >> 12; // We assume that the page size is 4KB
		if (table[index].PC == eip)
		{
			IntPtr new_stride = VPN - table[index].vaddr;

			if (table[index].stride == -1)
			{
				table[index].stride = new_stride;
				table[index].saturation_counter++;
			}
			else if (table[index].stride == new_stride)
			{
				table[index].saturation_counter++;
			}
			else
			{
				table[index].saturation_counter = 0;
			}
			if (table[index].saturation_counter > prefetch_threshold)
			{
				query_entry result_ptw = PTWTransparent((VPN + table[index].stride) << 12 , eip, lock, modeled, count, pt);

				#ifdef DEBUG
					std::cout << "Arbitrary Stride Prefetcher: VPN: " << VPN << " stride: " << table[index].stride << " PPN: " << result_ptw.ppn << std::endl;
				#endif

				stats.prefetch_attempts++;

				if(result_ptw.ppn!=0){
					stats.successful_prefetches++;
					result.push_back(result_ptw);
				}
				else {
					stats.failed_prefetches++;
				}

				if (extra_prefetch)
				{
					result_ptw = PTWTransparent((VPN - table[index].vaddr) << 12, eip, lock, modeled, count, pt);
					if(result_ptw.ppn!=0){
						stats.successful_prefetches++;
						result.push_back(result_ptw);
					}
				}

			}
			table[index].vaddr = VPN;
		}
		else
		{
			table[index].PC = eip;
			table[index].vaddr = VPN;
			table[index].stride = -1;
			table[index].saturation_counter = 0;
		}

		// std::cout << "Arbitrary Stride Prefetcher: " << table[index].PC << " " << table[index].vaddr << " " << table[index].stride << " " << table[index].saturation_counter << std::endl;
		// if (result.size() != 0)
		// 	std::cout << "Result size: " << result.size() << std::endl;
		return result;
	}

}