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
#include "utopia.h"
#include "mimicos.h"
#include "instruction.h"
#include <iostream>
#include <fstream>
#include <algorithm>

#define DEBUG_MMU

using namespace std;

namespace ParametricDramDirectoryMSI
{

	/*
	 * The MemoryManagementUnitUtopia class extends MemoryManagementUnitBase and provides
	 * a more advanced translation mechanism. It leverages multiple subsystems, such as:
	 *   - Page Table Walker (PTW)
	 *   - TLB Hierarchy
	 *   - RestSeg Walker
	 * It also tracks translation statistics and logs events for debugging.
	 */
	MemoryManagementUnitUtopia::MemoryManagementUnitUtopia(Core *_core, MemoryManager *_memory_manager, ShmemPerfModel *_shmem_perf_model, String _name, MemoryManagementUnitBase *_nested_mmu)
		: MemoryManagementUnitBase(_core, _memory_manager, _shmem_perf_model, _name, _nested_mmu)
	{
		// Each core will have a unique log file name constructed from core->getId().
		log_file_name = "mmu.log." + std::to_string(core->getId());
		log_file_name = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + log_file_name;
		log_file.open(log_file_name.c_str());

		// Build and initialize the page table walker, TLB subsystem, and RestSeg walker.
		instantiatePageTableWalker();
		instantiateTLBSubsystem();
		instantiateRestSegWalker();

		// Set up internal data structures and counters for statistics.
		registerMMUStats();
	}

	/*
	 * This function sets up the page table walker (PTW). The type of page table
	 * is determined by configuration settings. If a PWC (Page Walk Cache) is enabled,
	 * those structures are also allocated and initialized.
	 */
	void MemoryManagementUnitUtopia::instantiatePageTableWalker()
	{

		ptw_migration_threshold = Sim()->getCfg()->getInt("perf_model/utopia/ptw_migration_threshold");
		dram_accesses_migration_threshold = Sim()->getCfg()->getInt("perf_model/utopia/dram_accesses_migration_threshold");
		
		// Retrieve the name of the mimic OS (e.g., Utopia, Linux, etc.)
		String mimicos_name = Sim()->getMimicOS()->getName();
		String page_table_type = Sim()->getCfg()->getString("perf_model/" + mimicos_name + "/page_table_type");
		String page_table_name = Sim()->getCfg()->getString("perf_model/" + mimicos_name + "/page_table_name");

		// Only configure PWC if the page table type is "radix"
		if (page_table_type == "radix")
		{
			// Retrieve how many levels the page table has from the config.
			int levels = Sim()->getCfg()->getInt("perf_model/" + mimicos_name + "/" + page_table_name + "/levels");
			m_pwc_enabled = Sim()->getCfg()->getBool("perf_model/" + name + "/pwc/enabled");

			// If the Page Walk Cache is enabled, then allocate and configure it based on config parameters.
			if (m_pwc_enabled)
			{
				std::cout << "[MMU] Page walk caches are enabled" << std::endl;

				// The PWC for each page table level requires information on associativity and number of entries.
				UInt32 *entries = (UInt32 *)malloc(sizeof(UInt64) * (levels - 1));
				UInt32 *associativities = (UInt32 *)malloc(sizeof(UInt64) * (levels - 1));
				for (int i = 0; i < levels - 1; i++)
				{
					entries[i] = Sim()->getCfg()->getIntArray("perf_model/" + name + "/pwc/entries", i);
					associativities[i] = Sim()->getCfg()->getIntArray("perf_model/" + name + "/pwc/associativity", i);
				}

				ComponentLatency pwc_access_latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/" + name + "/pwc/access_penalty"));
				ComponentLatency pwc_miss_latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/" + name + "/pwc/miss_penalty"));

				// Create a new Page Walk Cache (pwc) object that can service lookups at multiple levels.
				pwc = new PWC("pwc",
							  "perf_model/" + name + "/pwc",
							  core->getId(),
							  associativities,
							  entries,
							  levels - 1,
							  pwc_access_latency,
							  pwc_miss_latency,
							  false /* Not a victim cache, presumably */);
			}
		}

		// pt_walkers is an MSHR (Miss Status Handling Register) system that tracks the number of parallel page table walkers.
		pt_walkers = new MSHR(Sim()->getCfg()->getInt("perf_model/" + name + "/page_table_walkers"));
	}

	/*
	 * Creates the TLB hierarchy object. This TLBHierarchy manages multiple TLB
	 * levels (e.g., L1I, L1D, L2, etc.) for both instruction and data translations.
	 */
	void MemoryManagementUnitUtopia::instantiateTLBSubsystem()
	{
		// TLBHierarchy is responsible for building TLB structures based on config.
		tlb_subsystem = new TLBHierarchy(name, core, memory_manager, shmem_perf_model);
	}

	/*
	 * Set up the "RestSeg" walker and related caches (permission cache, tag cache) used in Utopia.
	 * These caches speed up lookups for segments known to be in the "RestSeg" portion of memory.
	 */
	void MemoryManagementUnitUtopia::instantiateRestSegWalker()
	{
		// Permission Cache parameters
		int permission_cache_size = Sim()->getCfg()->getInt("perf_model/utopia/pcache/size");
		int permission_cache_assoc = Sim()->getCfg()->getInt("perf_model/utopia/pcache/assoc");
		ComponentLatency permission_cache_access_latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/utopia/pcache/access_penalty"));
		ComponentLatency permission_cache_miss_latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/utopia/pcache/miss_penalty"));

		// Create the permission cache ("sf_cache"), used to quickly check permissions for RestSeg pages.
		sf_cache = new UtopiaCache("sfcache",
								   "perf_model/utopia/sfcache",
								   core->getId(),
								   64, // line size
								   permission_cache_size,
								   permission_cache_assoc,
								   permission_cache_access_latency,
								   permission_cache_miss_latency);

		// Tag Cache parameters
		int tag_cache_size = Sim()->getCfg()->getInt("perf_model/utopia/tagcache/size");
		int tag_cache_assoc = Sim()->getCfg()->getInt("perf_model/utopia/tagcache/assoc");
		ComponentLatency tag_cache_access_latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/utopia/tagcache/access_penalty"));
		ComponentLatency tag_cache_miss_latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/utopia/tagcache/miss_penalty"));

		// Create the tag cache ("tar_cache"), used to verify the location of data in RestSeg.
		tar_cache = new UtopiaCache("tarcache",
									"perf_model/utopia/tarcache",
									core->getId(),
									64, // line size
									tag_cache_size,
									tag_cache_assoc,
									tag_cache_access_latency,
									tag_cache_miss_latency);
	}

	/*
	 * This function registers various statistics that will be tracked throughout the
	 * lifetime of the MMU. These stats include TLB hit/miss latencies, walk latencies,
	 * page faults, etc.
	 */
	void MemoryManagementUnitUtopia::registerMMUStats()
	{
		// Clears out translation_stats before we begin accumulating any stats
		bzero(&translation_stats, sizeof(translation_stats));

		// Registering the statistic counters for page faults and latencies
		registerStatsMetric(name, core->getId(), "page_faults", &translation_stats.page_faults);
		registerStatsMetric(name, core->getId(), "total_fault_latency", &translation_stats.total_fault_latency);

		// Detailed breakdown of latencies in the memory translation pipeline
		registerStatsMetric(name, core->getId(), "total_walk_latency", &translation_stats.total_walk_latency);
		registerStatsMetric(name, core->getId(), "total_rsw_latency", &translation_stats.total_rsw_latency);
		registerStatsMetric(name, core->getId(), "total_tlb_latency", &translation_stats.total_tlb_latency);

		registerStatsMetric(name, core->getId(), "total_walk_latency_flexseg_no_fault", &translation_stats.total_walk_latency_flexseg_no_fault);
		registerStatsMetric(name, core->getId(), "total_rsw_latency_restseg_no_fault", &translation_stats.total_rsw_latency_restseg_no_fault);

		// Additional breakdown for FlexSeg and RestSeg
		registerStatsMetric(name, core->getId(), "total_walk_latency_flexseg", &translation_stats.total_walk_latency_flexseg);
		registerStatsMetric(name, core->getId(), "total_rsw_latency_restseg", &translation_stats.total_rsw_latency_restseg);

		// TLB latency specific stats
		registerStatsMetric(name, core->getId(), "total_tlb_latency_on_tlb_hit", &translation_stats.total_tlb_latency_on_tlb_hit);

		// Count how many memory requests the RestSeg subsystem issues
		registerStatsMetric(name, core->getId(), "total_rsw_memory_requests", &translation_stats.total_rsw_memory_requests);

		// Where data is found (FlexSeg, RestSeg, TLB)
		registerStatsMetric(name, core->getId(), "data_in_flexseg", &translation_stats.data_in_flexseg);
		registerStatsMetric(name, core->getId(), "data_in_restseg", &translation_stats.data_in_restseg);
		registerStatsMetric(name, core->getId(), "data_in_tlb", &translation_stats.data_in_tlb);

		// Similar counters, but ignoring pages that had page faults
		registerStatsMetric(name, core->getId(), "data_in_flexseg_no_fault", &translation_stats.data_in_flexseg_no_fault);
		registerStatsMetric(name, core->getId(), "data_in_restseg_no_fault", &translation_stats.data_in_restseg_no_fault);

		// Track the total translation latency (excluding page faults)
		registerStatsMetric(name, core->getId(), "total_translation_latency", &translation_stats.total_translation_latency);

		// Additional stats on migrations from one segment to another
		registerStatsMetric(name, core->getId(), "flextorest_migrations", &translation_stats.flextorest_migrations);
		registerStatsMetric(name, core->getId(), "requests_affected_by_migration", &translation_stats.requests_affected_by_migration);
		registerStatsMetric(name, core->getId(), "migration_stall_cycles", &translation_stats.migration_stall_cycles);

		// Number of translations performed (address-translation requests)
		registerStatsMetric(name, core->getId(), "num_translations", &translation_stats.num_translations);

		// TLB latency data per TLB level
		translation_stats.tlb_latency_per_level = new SubsecondTime[tlb_subsystem->getTLBSubsystem().size()];
		for (UInt32 i = 0; i < tlb_subsystem->getTLBSubsystem().size(); i++)
			registerStatsMetric(name, core->getId(), "tlb_latency_" + itostr(i), &translation_stats.tlb_latency_per_level[i]);
	}

	/*
	 * This function performs the core address translation logic for an address.
	 * It checks TLBs, possibly uses RestSeg logic, and if needed, falls back to
	 * a page table walk (PTW). The final physical address is returned as an IntPtr.
	 */
	IntPtr MemoryManagementUnitUtopia::performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count)
	{
		// If we should count this request towards statistics, increment num_translations
		if (count)
			translation_stats.num_translations++;

		IntPtr ppn_result = 0;			   // Holds the final physical page number
		int page_size = -1;				   // The actual page size used for this address
		bool allocated_in_restseg = false; // Track if the allocation for the translation is in RestSeg

#ifdef DEBUG_MMU
		log_file << std::endl;
		log_file << "[MMU::Utopia] Address Translation: " << address << std::endl;
#endif

		/*
		 * Handle potential ongoing migrations. If the page is being migrated from
		 * one segment to another, we stall until that migration completes.
		 */
		SubsecondTime migration_latency = SubsecondTime::Zero();
		String page_table_name = Sim()->getMimicOS()->getPageTableName();
		String mimicos_name = Sim()->getMimicOS()->getName();

		int page_sizes = Sim()->getCfg()->getInt("perf_model/" + mimicos_name + "/" + page_table_name + "/page_sizes");
		int page_size_list[page_sizes];

		for (int i = 0; i < page_sizes; i++)
		{
			page_size_list[i] = Sim()->getCfg()->getIntArray("perf_model/" + mimicos_name + "/" + page_table_name + "/page_size_list", i);
		}

		// Check for migration in progress for each potential page size
		for (int i = 0; i < page_sizes; i++)
		{
			int vpn = address >> page_size_list[i];
			if (migration_queue.find(vpn) != migration_queue.end())
			{
				if (migration_queue[vpn] > shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD))
				{
					// Migration hasn't completed yet, so stall until it does.
					migration_latency = migration_queue[vpn] - shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
					translation_stats.migration_stall_cycles += migration_latency;

					if (count)
						translation_stats.requests_affected_by_migration++;
				}
			}
		}

		if (count)
			translation_stats.total_translation_latency += migration_latency;

#ifdef DEBUG_MMU
		log_file << "[MMU::Utopia] Migration Stall Cycles: " << migration_latency << std::endl;
#endif

		/*
		 * Next, we attempt to find a TLB hit by walking through each TLB level in the TLB hierarchy.
		 * If an entry is found, we record a TLB hit. Otherwise, we keep track that we missed and may
		 * need to do a deeper lookup (RestSeg or PTW).
		 */
		TLBSubsystem tlbs = tlb_subsystem->getTLBSubsystem();
		SubsecondTime time = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

		bool hit = false;						   // Will be set to true if any TLB hits
		TLB *hit_tlb = NULL;					   // The TLB that provided a hit
		CacheBlockInfo *tlb_block_info_hit = NULL; // The actual translation info
		CacheBlockInfo *tlb_block_info = NULL;
		int hit_level = -1;

		// Iterate through TLB hierarchy levels
		for (UInt32 i = 0; i < tlbs.size(); i++)
		{
#ifdef DEBUG_MMU
			log_file << "[MMU::Utopia] Searching TLB at level: " << i << std::endl;
#endif
			for (UInt32 j = 0; j < tlbs[i].size(); j++)
			{
				// We check if this TLB can store instruction addresses or data addresses
				bool tlb_stores_instructions = (tlbs[i][j]->getType() == TLBtype::Instruction) || (tlbs[i][j]->getType() == TLBtype::Unified);

				// If the TLB can store instructions AND the request is an instruction, then do a lookup
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
				else if (!instruction) // For data addresses
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
			// If we found a TLB hit at any TLB in this level, break from the loop of TLBs for this level
			if (hit)
			{
#ifdef DEBUG_MMU
				log_file << "[MMU::Utopia] TLB Hit at level: " << hit_level << " at TLB " << hit_tlb->getName() << std::endl;
#endif
				break;
			}
		}

		SubsecondTime charged_tlb_latency = SubsecondTime::Zero();

		/*
		 * If we have a TLB hit, we must charge the TLB latency for each level leading up to (and including)
		 * the one that reported a hit. This is because we effectively "visit" each level in the hierarchy.
		 */
		if (hit)
		{
#ifdef DEBUG_MMU
			log_file << "[MMU::Utopia] TLB Hit ? " << hit << " at level: " << hit_level << " at TLB: " << hit_tlb->getName() << std::endl;
#endif
			// Retrieve either the instruction path or data path TLB hierarchy.
			if (instruction)
				tlbs = tlb_subsystem->getInstructionPath();
			else
				tlbs = tlb_subsystem->getDataPath();

			// We'll store latencies for each TLB level we pass through, then aggregate them.
			SubsecondTime tlb_latency[hit_level + 1];
			for (int i = 0; i < hit_level; i++)
			{
				for (UInt32 j = 0; j < tlbs[i].size(); j++)
				{
					tlb_latency[i] = max(tlbs[i][j]->getLatency(), tlb_latency[i]);
				}
#ifdef DEBUG_MMU
				log_file << "[MMU::Utopia] Charging TLB Latency: " << tlb_latency[i] << " at level: " << i << std::endl;
#endif
				translation_stats.total_tlb_latency += tlb_latency[i];
				translation_stats.tlb_latency_per_level[i] += tlb_latency[i];
				charged_tlb_latency += tlb_latency[i];
			}

			// At the actual level that hits, only the TLB that hit charges latency
			for (UInt32 j = 0; j < tlbs[hit_level].size(); j++)
			{
				if (tlbs[hit_level][j] == hit_tlb)
				{
					translation_stats.total_tlb_latency += hit_tlb->getLatency();
					charged_tlb_latency += hit_tlb->getLatency();
					translation_stats.tlb_latency_per_level[hit_level] += hit_tlb->getLatency();

#ifdef DEBUG_MMU
					log_file << "[MMU::Utopia] Charging TLB Hit Latency: " << hit_tlb->getLatency() << " at level: " << hit_level << std::endl;
#endif
				}
			}

			// Advance our performance model time by the total TLB latency we just accumulated.
			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time + charged_tlb_latency);

#ifdef DEBUG_MMU
			log_file << "[MMU::Utopia] New time after charging TLB latency: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
#endif

			// If this request is counted for stats, track TLB-latency-on-TLB-hit metrics.
			if (count)
			{
				translation_stats.total_tlb_latency_on_tlb_hit += charged_tlb_latency;
				translation_stats.data_in_tlb++;
			}
		}
		else
		{
			/*
			 * TLB Miss: we must still charge TLB latency for the entire TLB path, because
			 * we "visited" all levels trying to find a hit but did not succeed.
			 */
#ifdef DEBUG_MMU
			log_file << "[MMU::Utopia] TLB Miss" << std::endl;
#endif
			SubsecondTime tlb_latency[tlbs.size()];
			for (UInt32 i = 0; i < tlbs.size(); i++)
			{
				for (UInt32 j = 0; j < tlbs[i].size(); j++)
				{
					tlb_latency[i] = max(tlbs[i][j]->getLatency(), tlb_latency[i]);
				}
#ifdef DEBUG_MMU
				log_file << "[MMU::Utopia] Charging TLB Latency: " << tlb_latency[i] << " at level: " << i << std::endl;
#endif
				translation_stats.total_tlb_latency += tlb_latency[i];
				charged_tlb_latency += tlb_latency[i];
			}
#ifdef DEBUG_MMU
			log_file << "[MMU::Utopia] New time after charging TLB latency: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
#endif
		}

		/*
		 * If we have a TLB miss, we try the RestSeg walker in parallel. If the address
		 * is found in RestSeg, we skip the PTW. Otherwise, we do a full page table walk.
		 */
		bool rsw_hit = false;
		SubsecondTime rsw_latency = SubsecondTime::Zero();

		// Only attempt the RestSeg walker if we had a TLB miss.
		if (!hit)
		{
#ifdef DEBUG_MMU
			log_file << "[MMU::Utopia] RSW in parallel with TLB access" << std::endl;
#endif
			auto rsw_result = RestSegWalk(address, instruction, eip, lock, modeled, count);

			// rsw_result is a tuple: (page_size, final_address, latency)
			rsw_hit = (get<0>(rsw_result) != -1);
			page_size = get<0>(rsw_result);
			ppn_result = get<1>(rsw_result);
			rsw_latency = get<2>(rsw_result);

			// If we’re counting stats, track RestSeg latency
			if (count)
				translation_stats.total_rsw_latency += rsw_latency;

#ifdef DEBUG_MMU
			log_file << "[MMU::Utopia] RSW Hit ? " << rsw_hit << std::endl;
			log_file << "[MMU::Utopia] RSW Latency: " << rsw_latency << std::endl;
			log_file << "[MMU::Utopia] Page Size: " << page_size << std::endl;
#endif
		}

		/*
		 * If we have an RSW hit, we finalize the translation with the maximum of TLB-latency or RSW-latency,
		 * because they proceed in parallel. Whichever is longer will determine the total time cost so far.
		 */
		if (!hit && rsw_hit)
		{
#ifdef DEBUG_MMU
			log_file << "[MMU::Utopia] RSW Hit and TLB Miss" << std::endl;
#endif
			if (count)
			{
				translation_stats.total_translation_latency += max(charged_tlb_latency, rsw_latency);
				translation_stats.total_rsw_latency_restseg_no_fault += rsw_latency;
				translation_stats.data_in_restseg_no_fault++;
			}

			// Advance the clock by whichever is bigger, TLB time or RSW time.
			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, max(charged_tlb_latency, rsw_latency) + time);

#ifdef DEBUG_MMU
			log_file << "[MMU::Utopia] New time after charging the maximum of TLB and RSW latency: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
#endif
		}

		/*
		 * total_walk_latency tracks the time for a page table walk (if needed),
		 * total_fault_latency handles page fault overhead, if the PTW encounters a fault.
		 */
		SubsecondTime total_walk_latency = SubsecondTime::Zero();
		SubsecondTime total_fault_latency = SubsecondTime::Zero();

		/*
		 * TLB miss AND RSW miss => We must do a full page table walk. This can also
		 * cause a page fault (handled below).
		 */

		bool was_serviced_by_flexseg = false;

		if(!hit && !rsw_hit)
		{
			if (count)
			{
				translation_stats.page_table_walks++;
			}

#ifdef DEBUG_MMU
			log_file << "[MMU::Utopia] TLB Miss and RSW Miss" << std::endl;
#endif

			SubsecondTime time_for_pt = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

			// Request an MSHR entry for a PT walker. If all are busy, we might have to wait.
			struct MSHREntry pt_walker_entry;
			pt_walker_entry.request_time = time_for_pt;

			// This call obtains any needed “slot allocation delay” if no PT walker is free.
			SubsecondTime delay = pt_walkers->getSlotAllocationDelay(pt_walker_entry.request_time);

			// Add this delay to our total translation latency, if counting.
			if (count)
				translation_stats.total_translation_latency += delay;

			// Advance time so that the actual page table walk starts after we get a free PT walker.
			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time_for_pt + delay);

#ifdef DEBUG_MMU
			log_file << "[MMU] New time after charging the PT walker allocation delay: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
#endif

			int app_id = core->getThread()->getAppId();
			PageTable *page_table = Sim()->getMimicOS()->getPageTable(app_id);

			// We do not plan to re-walk the page table upon page fault if we set restart_walk_upon_page_fault = false here,
			// but that might be overridden below if a fault occurs.
			const bool restart_walk_upon_page_fault = false;

			// Perform the page table walk. The result is a tuple containing walk latency, fault status, final PPN, page size, etc.
			auto ptw_result = performPTW(address, modeled, count, false, eip, lock, page_table, restart_walk_upon_page_fault);

#ifdef DEBUG_MMU
			log_file << "[MMU::Utopia] PTW Result: " << get<0>(ptw_result) << " " << get<1>(ptw_result) << " " << get<2>(ptw_result) << " " << get<3>(ptw_result) << std::endl;
#endif

			// The first element is total walk latency (excluding page fault).
			total_walk_latency = get<0>(ptw_result);

			if (count)
			{
				translation_stats.total_walk_latency += total_walk_latency;
				translation_stats.total_translation_latency += total_walk_latency;
			}

#ifdef DEBUG_MMU
			log_file << "[MMU::Utopia] Total Walk Latency: " << total_walk_latency << std::endl;
#endif

			bool caused_page_fault = get<1>(ptw_result);
			if (caused_page_fault)
			{
				// If a fault occurs, we must pay page fault latency
				SubsecondTime m_page_fault_latency = Sim()->getMimicOS()->getPageFaultLatency();
				if (count)
				{
					translation_stats.page_faults++;
					translation_stats.total_fault_latency += m_page_fault_latency;
				}
				total_fault_latency = m_page_fault_latency;
			}

			// The time it takes to fully complete the PTW includes:
			//  (1) any delay waiting for an available PT walker,
			//  (2) the walk latency itself,
			//  (3) any fault latency.
			pt_walker_entry.completion_time = time_for_pt + delay + total_walk_latency + total_fault_latency;
			pt_walkers->allocate(pt_walker_entry);

			// If there's a page fault, we queue up a pseudo-instruction that simulates the fault-handling routine.
			if (caused_page_fault)
			{
				PseudoInstruction *i = new PageFaultRoutineInstruction(total_fault_latency);
				getCore()->getPerformanceModel()->queuePseudoInstruction(i);
				shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, pt_walker_entry.completion_time);
			}
			else
			{
				shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, pt_walker_entry.completion_time);
			}

			/*
			 * If a page fault did occur, we then handle it (by calling handle_page_fault).
			 * Depending on whether the page was allocated in RestSeg or not, we possibly
			 * must restart the RSW or the PTW to finalize the translation.
			 */
			if (caused_page_fault)
			{
#ifdef DEBUG_MMU
				log_file << "[MMU::Utopia] Page Fault occured" << std::endl;
#endif
				Sim()->getMimicOS()->handle_page_fault(address, app_id, 0);

#ifdef DEBUG_MMU
				log_file << "[MMU::Utopia] Page Fault handled" << std::endl;
#endif
				Utopia *utopia = dynamic_cast<Utopia *>(Sim()->getMimicOS()->getMemoryAllocator());

				// If the page ended up being allocated in RestSeg after fault handling,
				// we need to do a RestSeg walk again.
				if (utopia->getLastAllocatedInRestSeg())
				{
#ifdef DEBUG_MMU
					log_file << "[MMU::Utopia] Data was allocated in RestSeg" << std::endl;
					log_file << "[MMU::Utopia] Restarting RSW" << std::endl;
#endif
					auto restarted_rsw_result = RestSegWalk(address, instruction, eip, lock, modeled, count);
					ppn_result = get<1>(restarted_rsw_result);
					page_size = get<0>(restarted_rsw_result);
					SubsecondTime restarted_walk_latency = get<2>(restarted_rsw_result);

#ifdef DEBUG_MMU
					log_file << "[MMU::Utopia] VPN: " << (address >> page_size) << std::endl;
					log_file << "[MMU::Utopia] PPN: " << ppn_result << std::endl;
					log_file << "[MMU::Utopia] Page Size: " << page_size << std::endl;
					log_file << "[MMU::Utopia] RSW Latency: " << restarted_walk_latency << std::endl;
#endif
					if (count)
					{
						translation_stats.total_rsw_latency_restseg += restarted_walk_latency;
						translation_stats.total_translation_latency += restarted_walk_latency;
						translation_stats.data_in_restseg++;
					}

#ifdef DEBUG_MMU
					log_file << "[MMU::Utopia] RSW Result: " << get<0>(restarted_rsw_result) << " " << get<1>(restarted_rsw_result) << " " << get<2>(restarted_rsw_result) << std::endl;
#endif

					shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, pt_walker_entry.completion_time + restarted_walk_latency);

#ifdef DEBUG_MMU
					log_file << "[MMU::Utopia] New time after finishing restarted RSW: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
#endif
				}
				else
				{
					was_serviced_by_flexseg = true;

					// Otherwise, if the page ended up allocated in the "FlexSeg" region, we do a new PTW.
					auto restarted_ptw_result = performPTW(address, modeled, count, true, eip, lock, page_table, restart_walk_upon_page_fault);

#ifdef DEBUG_MMU
					log_file << "[MMU::Utopia] Restarted PTW Result: " << get<0>(restarted_ptw_result) << " " << get<1>(restarted_ptw_result) << " " << get<2>(restarted_ptw_result) << " " << get<3>(restarted_ptw_result) << std::endl;
#endif
					SubsecondTime total_restarted_walk_latency = get<0>(restarted_ptw_result);
					page_size = get<3>(restarted_ptw_result);
					ppn_result = get<2>(restarted_ptw_result);

#ifdef DEBUG_MMU
					log_file << "[MMU::Utopia] VPN: " << (address >> page_size) << std::endl;
					log_file << "[MMU::Utopia] PPN: " << ppn_result << std::endl;
					log_file << "[MMU::Utopia] Page Size: " << page_size << std::endl;
#endif
					if (count)
					{
						translation_stats.total_walk_latency_flexseg += total_restarted_walk_latency;
						translation_stats.total_translation_latency += total_restarted_walk_latency;
						translation_stats.data_in_flexseg++;
					}

					shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, pt_walker_entry.completion_time + total_restarted_walk_latency);

#ifdef DEBUG_MMU
					log_file << "[MMU::Utopia] New time after finishing restarted PTW: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
#endif
				}
			}
			else
			{
				/*
				 * If there is no page fault, we set up the final translation result from
				 * the PTW. The page_size and ppn_result are obtained from the PTW tuple.
				 */
				was_serviced_by_flexseg = true;

#ifdef DEBUG_MMU
				log_file << "[MMU::Utopia] No Page Fault: Data was in FlexSeg" << std::endl;
#endif
				ppn_result = get<2>(ptw_result);
				page_size = get<3>(ptw_result);

				if (count)
				{
					translation_stats.data_in_flexseg_no_fault++;
					translation_stats.total_walk_latency_flexseg_no_fault += total_walk_latency;
				}
			}

			// If the PTW was successful and was served from the FlexSeg region, 
			// we need to find out if the page is costly to translate

			if (was_serviced_by_flexseg)
			{

				// For each page, track how often we do a page table walk and how many DRAM accesses that walk required.
				IntPtr vpn = address >> page_size;

				if (ptw_stats.find(vpn) == ptw_stats.end())
				{
					ptw_stats[vpn] = make_pair(1, 0);
				}
				else
				{
					ptw_stats[vpn].first++;
				}

				int dram_accesses_occured = getDramAccessesDuringLastWalk();

				ptw_stats[vpn].second += dram_accesses_occured;

	#ifdef DEBUG_MMU
				log_file << "[MMU::Utopia] Page PTW stats: " << ptw_stats[vpn].first << " " << ptw_stats[vpn].second << std::endl;
	#endif

				/*
				* If the page table walk is determined to be "expensive" (e.g., used more than 4 times,
				* with more than 3 DRAM accesses), we may migrate the page from FlexSeg to RestSeg.
				* The actual migration triggers a future stall (in migration_queue).
				*/

				/*----------Migration Logic----------*/
				
				if ((ptw_stats[vpn].first >= ptw_migration_threshold) && (ptw_stats[vpn].second >= dram_accesses_migration_threshold))
				{
	#ifdef DEBUG_MMU
					log_file << "[MMU::Utopia] This page is really costly-to-translate, migrate it to RestSeg " << std::endl;
	#endif
					migration_queue[vpn] = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) + ComponentLatency(core->getDvfsDomain(), 200).getLatency();

					if (count)
						translation_stats.flextorest_migrations++;

					/*
					* Here, you would call a function from the Utopia Allocator to handle the actual data migration
					* from one segment to another. The code is omitted, but presumably it sets up future references
					* so that subsequent translations do not require the entire walk again.
					*/

					Utopia *utopia = dynamic_cast<Utopia *>(Sim()->getMimicOS()->getMemoryAllocator());
	#ifdef DEBUG_MMU
					log_file << "[MMU::Utopia] Migrating page: " << vpn << std::endl;
					log_file << "[MMU::Utopia] Page Size: " << page_size << std::endl;
					log_file << "[MMU::Utopia] PPN: " << ppn_result << std::endl;
	#endif
					IntPtr new_ppn = utopia->migratePage(address, ppn_result, page_size, core->getThread()->getAppId());
	#ifdef DEBUG_MMU
					log_file << "[MMU::Utopia] New PPN: " << new_ppn << std::endl;
	#endif
					ppn_result = new_ppn;

				}
			}
		}
		else if (hit)
		{
			/*
			 * For TLB hits, simply read out the page size and PPN from the TLB block info.
			 */
			page_size = tlb_block_info_hit->getPageSize();
			ppn_result = tlb_block_info_hit->getPPN();

#ifdef DEBUG_MMU
			log_file << "[MMU::Utopia] TLB Hit" << std::endl;
			log_file << "[MMU::Utopia] Page Size: " << tlb_block_info_hit->getPageSize() << std::endl;
			log_file << "[MMU::Utopia] VPN: " << (address >> page_size) << std::endl;
			log_file << "[MMU::Utopia] PPN: " << tlb_block_info_hit->getPPN() << std::endl;
#endif
		}

#ifdef DEBUG_MMU
		log_file << "[MMU::Utopia] Total Walk Latency: " << total_walk_latency << std::endl;
		log_file << "[MMU::Utopia] Total Fault Latency: " << total_fault_latency << std::endl;
		log_file << "[MMU::Utopia] Total RSW Latency: " << rsw_latency << std::endl;
#endif

		/*
		 * Once we know the final translation (TLB or PTW or RSW), we optionally allocate
		 * that translation in upper TLB levels. This helps with future hits in those TLBs.
		 */
		// instruction path: follows the instruction TLB path
		// data path: follows the data TLB path
		if (instruction)
			tlbs = tlb_subsystem->getInstructionPath();
		else
			tlbs = tlb_subsystem->getDataPath();

		std::map<int, vector<tuple<IntPtr, int>>> evicted_translations;

		// We need to allocate the entry in every "allocate on miss" TLB

		int tlb_levels = tlbs.size();

		if (tlb_subsystem->isPrefetchEnabled())
		{
			tlb_levels = tlbs.size() - 1;
#ifdef DEBUG_MMU
			log_file << "[MMU] Prefetching is enabled" << std::endl;
#endif
		}

		for (int i = 0; i < tlb_levels; i++)
		{
			// We will check where we need to allocate the page

			for (UInt32 j = 0; j < tlbs[i].size(); j++)
			{
				// We need to check if there are any evicted translations from the previous level and allocate them
				if ((i > 0) && (evicted_translations[i - 1].size() != 0))
				{
					tuple<bool, IntPtr, int> result;

#ifdef DEBUG_MMU
					log_file << "[MMU] There are evicted translations from level: " << i - 1 << std::endl;
#endif
					// iterate through the evicted translations and allocate them in the current TLB
					for (UInt32 k = 0; k < evicted_translations[i - 1].size(); k++)
					{
#ifdef DEBUG_MMU
						log_file << "[MMU] Evicted Translation: " << get<0>(evicted_translations[i - 1][k]) << std::endl;
#endif
						// We need to check if the TLB supports the page size of the evicted translation
						if (tlbs[i][j]->supportsPageSize(page_size))
						{
#ifdef DEBUG_MMU
							log_file << "[MMU] Allocating evicted entry in TLB: Level = " << i << " Index =  " << j << std::endl;
#endif

							result = tlbs[i][j]->allocate(get<0>(evicted_translations[i - 1][k]), time, count, lock, get<1>(evicted_translations[i - 1][k]), ppn_result);

							// If the allocation was successful and we have an evicted translation, 
							// we need to add it to the evicted translations vector for

							if (get<0>(result) == true)
							{
								evicted_translations[i].push_back(make_tuple(get<1>(result), get<2>(result)));
							}
						}
					}
				}

				// We need to allocate the current translation in the TLB if:
				// 1) The TLB supports the page size of the translation
				// 2) The TLB is an "allocate on miss" TLB
				// 3) There was a TLB miss or the TLB hit was at a higher level and you need to allocate the translation in the current level
				
				if (tlbs[i][j]->supportsPageSize(page_size) && tlbs[i][j]->getAllocateOnMiss() && (!hit || (hit && hit_level > i)))
				{
					
#ifdef DEBUG_MMU
					log_file << "[MMU] " << tlbs[i][j]->getName() << " supports page size: " << page_size << std::endl;
					log_file << "[MMU] Allocating in TLB: Level = " << i << " Index = " << j << " with page size: " << page_size << " and VPN: " << (address >> page_size) << std::endl;
#endif
					tuple<bool, IntPtr, int> result;

					result = tlbs[i][j]->allocate(address, time, count, lock, page_size, ppn_result);
					if (get<0>(result) == true)
					{
						evicted_translations[i].push_back(make_tuple(get<1>(result), get<2>(result)));
					}
				}

			}
		}

		/*
		 * Compute the final physical address based on the ppn_result and offset within that page.
		 * For example, if page_size is 12 bits, shift the PPN left by 12 and add the offset.
		 */
		int base_page_size = pow(2, 12);
		int offset = address % page_size;
		IntPtr final_physical_address = ppn_result * base_page_size + offset;

#ifdef DEBUG_MMU
		log_file << "[MMU::Utopia] VPN: " << (address >> page_size) << std::endl;
		log_file << "[MMU::Utopia] PPN: " << ppn_result << std::endl;
		log_file << "[MMU::Utopia] Offset: " << offset << std::endl;
		log_file << "[MMU::Utopia] Final Physical Address: " << final_physical_address << std::endl;
		log_file << "[MMU::Utopia] Translation Done" << std::endl;
#endif

		return final_physical_address;
	}

	/*
	 * Walk the RestSeg data structures (and caches) to see if the address belongs to any
	 * RestSeg region. If found, returns a tuple of (page_size, final_address, latency).
	 * Otherwise, it indicates a miss with a page_size of -1.
	 */
	std::tuple<int, IntPtr, SubsecondTime> MemoryManagementUnitUtopia::RestSegWalk(IntPtr address, bool instruction, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count)
	{
		vector<pair<IntPtr, int>> memory_accesses;
		SubsecondTime t_start = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
		IntPtr final_address = 0;
		Utopia *m_utopia = (Utopia *)Sim()->getMimicOS()->getMemoryAllocator();
		RestSeg *restseg;
		bool tag_match_skip = false;
		bool restsegwalk_skip = false;
		int page_size = -1;
		bool restseg_hit = false;

		// Iterate over all RestSeg objects that might contain the address
		for (int i = 0; i < m_utopia->RestSegs; i++)
		{
#ifdef DEBUG_MMU
			log_file << "[MMU::Utopia] RestSeg Walk for RestSeg: " << i << "with page size: " << m_utopia->getRestSeg(i)->getPageSize() << std::endl;
#endif
			restseg = m_utopia->getRestSeg(i);

			// Check if the address belongs to the current RestSeg region
			if (restseg->inRestSeg(address, count, t_start, core->getId()))
			{
#ifdef DEBUG_MMU
				log_file << "[MMU::Utopia] Data is in RestSeg: " << i << std::endl;
#endif
				page_size = restseg->getPageSize();
				restseg_hit = true;
			}

			// The permission filter check might allow skipping further tag checks
			if (restseg->permission_filter(address, core->getId()))
				tag_match_skip = true;
			else
				tag_match_skip = false;

#ifdef DEBUG_MMU
			log_file << "[MMU::Utopia] Tag Match Skip: " << tag_match_skip << std::endl;
#endif
			UInt64 permission_filter = restseg->calculate_permission_address(address, core->getId());

			// Check the permission cache (sf_cache)
			UtopiaCache::where_t permcache_hitwhere;
			permcache_hitwhere = sf_cache->lookup((IntPtr)permission_filter, t_start, true, count);

#ifdef DEBUG_MMU
			log_file << "[MMU::Utopia] Accessing Permission Filter: " << permission_filter << std::endl;
#endif

			// If we miss in the permission cache, we must do a memory access
			if (permcache_hitwhere == UtopiaCache::where_t::MISS)
			{
				memory_accesses.push_back(make_pair(permission_filter, 0));
#ifdef DEBUG_MMU
				log_file << "[MMU::Utopia] Set Filter cache miss: " << permission_filter << std::endl;
#endif
			}

			if (tag_match_skip == false)
			{
				UInt64 tag = restseg->calculate_tag_address(address, core->getId());

#ifdef DEBUG_MMU
				log_file << "[MMU::Utopia] Accessing Tag: " << tag << std::endl;
#endif
				UtopiaCache::where_t tagcache_hitwhere;
				tagcache_hitwhere = tar_cache->lookup((IntPtr)tag, t_start, true, count);

				// If the tag cache missed, we record this as an additional memory access
				if (tagcache_hitwhere == UtopiaCache::where_t::MISS)
				{
#ifdef DEBUG_MMU
					log_file << "[MMU::Utopia] Tag cache miss: " << tag << std::endl;
#endif
					memory_accesses.push_back(make_pair(tag, 0));
				}
			}

			// If we discovered a hit in this RestSeg, we have the final address
			if (restseg_hit == true)
			{
				final_address = restseg->calculate_physical_address(address, core->getId());
#ifdef DEBUG_MMU
				log_file << "[MMU::Utopia] Final Address: " << final_address << std::endl;
#endif
				break;
			}
		}

		SubsecondTime latency = SubsecondTime::Zero();
		SubsecondTime total_latency = SubsecondTime::Zero();

		/*
		 * For every needed memory access (permission/tag checks) that missed in the caches,
		 * we invoke accessCache. This simulates the time to retrieve that information from memory.
		 */
		for (UInt32 i = 0; i < memory_accesses.size(); i++)
		{
			translationPacket packet;
			packet.eip = eip;
			packet.address = memory_accesses[i].first;
			packet.instruction = instruction;
			packet.lock_signal = lock;
			packet.modeled = modeled;
			packet.count = count;
			packet.type = CacheBlockInfo::block_type_t::UTOPIA; // Indicates a RestSeg type request

			latency = accessCache(packet);
		}

		// If multiple memory accesses took place, total_latency could track the sum or the max. Here we do average as an example.
		if (memory_accesses.size() != 0)
			total_latency = total_latency / memory_accesses.size();

		// Increment the total RSW memory requests by however many times we accessed memory here.
		if (count)
			translation_stats.total_rsw_memory_requests += memory_accesses.size();

		/*
		 * If we found the address in RestSeg, page_size is not -1. Otherwise, it remains -1
		 * indicating a miss in RestSeg. We return the final_address if found, and the latency
		 * from these cache lookups.
		 */
		return make_tuple(page_size, final_address, latency);
	}

	/*
	 * This function filters the results of a page table walk result (ptw_result) through
	 * any enabled Page Walk Caches (PWC). If an entry is found in the PWC, we can skip the
	 * actual memory access. The filtered result is returned.
	 */
	PTWResult MemoryManagementUnitUtopia::filterPTWResult(PTWResult ptw_result, PageTable *page_table, bool count)
	{
		accessedAddresses ptw_accesses;
		bool pwc_hit = false;

		if (m_pwc_enabled)
		{
			accessedAddresses original_ptw_accesses = get<1>(ptw_result);

			// We iterate through each memory access that the PTW reported
			for (UInt32 i = 0; i < get<1>(ptw_result).size(); i++)
			{
				bool pwc_hit = false;
				int level = get<1>(original_ptw_accesses[i]);
				PageTableRadix *page_table_radix = dynamic_cast<PageTableRadix *>(page_table);
				int levels = page_table_radix->getMaxLevel();
				IntPtr pwc_address = get<2>(original_ptw_accesses[i]);

#ifdef DEBUG_MMU
				log_file << "[MMU] Checking PWC for address: " << pwc_address << " at level: " << level << std::endl;
#endif
				/*
				 * If level < 3, we check the PWC at that level. (This condition can be changed
				 * depending on how many levels you want to accelerate with PWC.)
				 */
				if (level < 3)
				{
					pwc_hit = pwc->lookup(pwc_address, SubsecondTime::Zero(), true, level, count);
				}

#ifdef DEBUG_MMU
				log_file << "[MMU] PWC HIT: " << pwc_hit << " level: " << level << std::endl;
#endif
				/*
				 * If not in PWC, we keep it in ptw_accesses so the real memory access cost is reflected.
				 */
				if (!pwc_hit)
				{
					ptw_accesses.push_back(get<1>(ptw_result)[i]);
				}
			}
		}

		// Return the same final PPN, page size, and walk stats, but updated with whichever addresses remain after PWC filtering.
		return PTWResult(get<0>(ptw_result), ptw_accesses, get<2>(ptw_result), get<3>(ptw_result), get<4>(ptw_result));
	}

	/*
	 * Possibly used to discover Virtual Memory Areas (VMAs) in the system, though
	 * currently unimplemented.
	 */
	void MemoryManagementUnitUtopia::discoverVMAs()
	{
		// Implementation can be added here to parse or discover new VMAs,
		// depending on the operating system or environment.
	}

} // namespace ParametricDramDirectoryMSI
