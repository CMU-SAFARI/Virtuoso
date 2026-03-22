#pragma once

#include "tlb_prefetcher_base.h"
#include "stride_prefetcher.h"
#include "H2Prefetcher.h"
#include "arbitrary_stride_prefetcher.h"
#include "AgileTLBPrefetcher.h"
#include "RecencyTLBPrefetcher.h"
#include "DistanceTLBPrefetcher.h"

namespace ParametricDramDirectoryMSI
{
	class TLBprefetcherFactory
	{
	public:
		static TLBPrefetcherBase *createASPPrefetcher(String mmu_name, String prefetcher_name, String pq_index, Core *core, MemoryManagerBase *memory_manager, ShmemPerfModel *shmem_perf_model)
		{
			int table_size = Sim()->getCfg()->getInt("perf_model/"+mmu_name+"/tlb_prefetch/pq" + pq_index + "/asp_prefetcher/table_size");
			int prefetch_threshold = Sim()->getCfg()->getInt("perf_model/"+mmu_name+"/tlb_prefetch/pq" + pq_index + "/asp_prefetcher/prefetch_threshold");
			bool extra_prefetch = Sim()->getCfg()->getBool("perf_model/"+mmu_name+"/tlb_prefetch/pq" + pq_index + "/asp_prefetcher/extra_prefetch");
			int lookahead = Sim()->getCfg()->getInt("perf_model/"+mmu_name+"/tlb_prefetch/pq" + pq_index + "/asp_prefetcher/lookahead");
			int degree = Sim()->getCfg()->getInt("perf_model/"+mmu_name+"/tlb_prefetch/pq" + pq_index + "/asp_prefetcher/degree");
			return new ArbitraryStridePrefetcher(core, memory_manager, shmem_perf_model, table_size, prefetch_threshold, extra_prefetch, lookahead, degree, prefetcher_name);
		}
		static TLBPrefetcherBase *createTLBPrefetcherStride(String mmu_name, String prefetcher_name, String pq_index, Core *core, MemoryManagerBase *memory_manager, ShmemPerfModel *shmem_perf_model)
		{
			int length = Sim()->getCfg()->getInt("perf_model/"+mmu_name+"/tlb_prefetch/pq" + pq_index + "/stride_prefetcher/length");
			return new StridePrefetcher(core, memory_manager, shmem_perf_model, length, prefetcher_name);
		}
		static TLBPrefetcherBase *createTLBPrefetcherH2(String mmu_name, String prefetcher_name, String pq_index, Core *core, MemoryManagerBase *memory_manager, ShmemPerfModel *shmem_perf_model)
		{
			return new H2Prefetcher(core, memory_manager, shmem_perf_model, prefetcher_name);
		}
		static TLBPrefetcherBase *createAgileTLBPrefetcher(String mmu_name, String prefetcher_name, String pq_index, Core *core, MemoryManagerBase *memory_manager, ShmemPerfModel *shmem_perf_model)
		{
			String cfg_base = "perf_model/" + mmu_name + "/tlb_prefetch/pq" + pq_index + "/atp_prefetcher/";

			uint32_t pq_size          = static_cast<uint32_t>(Sim()->getCfg()->getInt(cfg_base + "pq_size"));
			uint32_t sampler_size     = static_cast<uint32_t>(Sim()->getCfg()->getInt(cfg_base + "sampler_size"));
			uint32_t fpq_size         = static_cast<uint32_t>(Sim()->getCfg()->getInt(cfg_base + "fpq_size"));
			uint32_t fdt_counter_bits = static_cast<uint32_t>(Sim()->getCfg()->getInt(cfg_base + "fdt_counter_bits"));
			uint16_t fdt_threshold    = static_cast<uint16_t>(Sim()->getCfg()->getInt(cfg_base + "fdt_threshold"));
			uint32_t enable_pref_bits = static_cast<uint32_t>(Sim()->getCfg()->getInt(cfg_base + "enable_pref_bits"));
			uint32_t select1_bits     = static_cast<uint32_t>(Sim()->getCfg()->getInt(cfg_base + "select1_bits"));
			uint32_t select2_bits     = static_cast<uint32_t>(Sim()->getCfg()->getInt(cfg_base + "select2_bits"));
			uint32_t masp_entries     = static_cast<uint32_t>(Sim()->getCfg()->getInt(cfg_base + "masp_entries"));
			uint32_t masp_assoc       = static_cast<uint32_t>(Sim()->getCfg()->getInt(cfg_base + "masp_assoc"));
			uint32_t page_shift       = static_cast<uint32_t>(Sim()->getCfg()->getInt(cfg_base + "page_shift"));

			return new AgileTLBPrefetcher(core, memory_manager, shmem_perf_model, prefetcher_name,
				pq_size, sampler_size, fpq_size, fdt_counter_bits, fdt_threshold,
				enable_pref_bits, select1_bits, select2_bits,
				masp_entries, masp_assoc, page_shift);
		}
		static TLBPrefetcherBase *createRecencyTLBPrefetcher(String mmu_name, String prefetcher_name, String pq_index, Core *core, MemoryManagerBase *memory_manager, ShmemPerfModel *shmem_perf_model)
		{
			String cfg_base = "perf_model/" + mmu_name + "/tlb_prefetch/pq" + pq_index + "/recency_prefetcher/";

			uint32_t page_shift              = static_cast<uint32_t>(Sim()->getCfg()->getInt(cfg_base + "page_shift"));
			bool     prefetch_same_recency    = Sim()->getCfg()->getBool(cfg_base + "prefetch_same_recency");
			bool     prefetch_recency_minus_1 = Sim()->getCfg()->getBool(cfg_base + "prefetch_recency_minus_1");
			bool     prefetch_recency_plus_1  = Sim()->getCfg()->getBool(cfg_base + "prefetch_recency_plus_1");
			bool     prefetch_on_tlb_hit      = Sim()->getCfg()->getBool(cfg_base + "prefetch_on_tlb_hit");
			bool     model_prefetch_walks     = Sim()->getCfg()->getBool(cfg_base + "model_prefetch_walks");
			bool     consume_pq_on_hit        = Sim()->getCfg()->getBool(cfg_base + "consume_pq_on_hit");
			bool     model_pointer_chase       = Sim()->getCfg()->getBool(cfg_base + "model_pointer_chase");

			return new RecencyTLBPrefetcher(core, memory_manager, shmem_perf_model, prefetcher_name,
				page_shift, prefetch_same_recency, prefetch_recency_minus_1,
				prefetch_recency_plus_1, prefetch_on_tlb_hit,
				model_prefetch_walks, consume_pq_on_hit, model_pointer_chase);
		}
		static TLBPrefetcherBase *createDistanceTLBPrefetcher(String mmu_name, String prefetcher_name, String pq_index, Core *core, MemoryManagerBase *memory_manager, ShmemPerfModel *shmem_perf_model)
		{
			String cfg_base = "perf_model/" + mmu_name + "/tlb_prefetch/pq" + pq_index + "/dp_prefetcher/";

			uint32_t page_shift          = static_cast<uint32_t>(Sim()->getCfg()->getInt(cfg_base + "page_shift"));
			uint32_t num_rows            = static_cast<uint32_t>(Sim()->getCfg()->getInt(cfg_base + "num_rows"));
			uint32_t num_slots           = static_cast<uint32_t>(Sim()->getCfg()->getInt(cfg_base + "num_slots"));
			uint32_t assoc               = static_cast<uint32_t>(Sim()->getCfg()->getInt(cfg_base + "assoc"));
			bool     model_prefetch_walks = Sim()->getCfg()->getBool(cfg_base + "model_prefetch_walks");

			return new DistanceTLBPrefetcher(core, memory_manager, shmem_perf_model, prefetcher_name,
				page_shift, num_rows, num_slots, assoc, model_prefetch_walks);
		}
		static TLBPrefetcherBase *createTLBPrefetcher(String mmu_name, String prefetcher_name, String pq_index, Core *core, MemoryManagerBase *memory_manager, ShmemPerfModel *shmem_perf_model)
		{
			if (prefetcher_name == "stride")
			{
				return createTLBPrefetcherStride(mmu_name, prefetcher_name, pq_index, core, memory_manager, shmem_perf_model);
			}
			else if (prefetcher_name == "h2")
			{
				return createTLBPrefetcherH2(mmu_name, prefetcher_name, pq_index, core, memory_manager, shmem_perf_model);
			}
			else if (prefetcher_name == "asp")
			{

				return createASPPrefetcher(mmu_name, prefetcher_name, pq_index, core, memory_manager, shmem_perf_model);
			}
			else if (prefetcher_name == "atp")
			{
				return createAgileTLBPrefetcher(mmu_name, prefetcher_name, pq_index, core, memory_manager, shmem_perf_model);
			}
			else if (prefetcher_name == "recency")
			{
				return createRecencyTLBPrefetcher(mmu_name, prefetcher_name, pq_index, core, memory_manager, shmem_perf_model);
			}
			else if (prefetcher_name == "dp")
			{
				return createDistanceTLBPrefetcher(mmu_name, prefetcher_name, pq_index, core, memory_manager, shmem_perf_model);
			}
			else
			{
				std::cout << "[CONFIG ERROR] No such TLB prefetcher: " << prefetcher_name << std::endl;
				
				return nullptr;
			}
		}
	};
}