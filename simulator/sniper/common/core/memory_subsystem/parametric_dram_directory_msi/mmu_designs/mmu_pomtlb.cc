#include "mmu_pomtlb.h"
#include "mmu_base.h"
#include "tlb.h"
#include "memory_manager.h"
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "pagetable_factory.h"
#include "core.h"
#include "thread.h"
#include "mimicos.h"
#include "instruction.h"
#include <iostream>
#include <fstream>
#include <algorithm>

//#define DEBUG_MMU

using namespace std;

namespace ParametricDramDirectoryMSI
{
	// # Based on Ryoo et al. "Rethinking TLB designs in virtualized environments: 
	// # A very large part-of-memory TLB" https://ieeexplore.ieee.org/document/8192494
   
	/* Implementation of 
	 * MemoryManagementUnitPOMTLB class:
	 *   - Extends MemoryManagementUnitBase to include a specialized "software-managed" TLB
	 *     system (m_pom_tlb). 
	 *   - The "POM" TLB is an additional TLB that is allocated in software (like a table in memory)
	 *     and accessed after conventional hardware TLB misses.
	 *   - If both the hardware TLB and POM TLB miss, it performs a full page table walk (PTW).
	 *
	 * Key data structures / fields:
	 *   - tlb_subsystem: the normal hardware TLB hierarchy.
	 *   - m_pom_tlb[]: an array of TLB pointers for software-managed TLBs, one per page size.
	 *   - software_tlb_base_register[]: base addresses in memory where each software TLB is allocated.
	 *   - translation_stats: accumulates statistics like total walk latency, TLB latency, etc.
	 *   - pt_walkers: an MSHR-like structure that tracks concurrency for page table walks.
	 *   - pwc (Page Walk Cache): if enabled, can filter out memory accesses for intermediate page-table entries.
	 */

	MemoryManagementUnitPOMTLB::MemoryManagementUnitPOMTLB(Core *_core, 
	                                                       MemoryManager *_memory_manager, 
	                                                       ShmemPerfModel *_shmem_perf_model, 
	                                                       String _name, 
	                                                       MemoryManagementUnitBase *_nested_mmu)
	: MemoryManagementUnitBase(_core, _memory_manager, _shmem_perf_model, _name, _nested_mmu)
	{
		log_file_name = "mmu_pomtlb.log." + std::to_string(core->getId());
		log_file_name = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + log_file_name;
		log_file.open(log_file_name.c_str());

		// Build the page table walker and the TLB subsystem, then register stats
		instantiatePageTableWalker();
		instantiateTLBSubsystem();
		registerMMUStats();
	}

	/*
	 * instantiatePageTableWalker(...):
	 *   - Checks configuration for the OS (mimicos_name) and page table type (e.g., "radix").
	 *   - If PWC (page walk cache) is enabled, sets up a PWC object.
	 *   - Also creates an MSHR object 'pt_walkers' for concurrency in page table walks.
	 */
	void MemoryManagementUnitPOMTLB::instantiatePageTableWalker()
	{
		String mimicos_name = Sim()->getMimicOS()->getName();
		String page_table_type = Sim()->getCfg()->getString("perf_model/"+mimicos_name+"/page_table_type");
		String page_table_name = Sim()->getCfg()->getString("perf_model/"+mimicos_name+"/page_table_name");

		if (page_table_type == "radix")
		{
			int levels = Sim()->getCfg()->getInt("perf_model/"+mimicos_name+"/"+page_table_name+"/levels");
			m_pwc_enabled = Sim()->getCfg()->getBool("perf_model/"+name+"/pwc/enabled");

			if (m_pwc_enabled)
			{
				std::cout << "[MMU:POM-TLB] Page walk caches are enabled" << std::endl;

				UInt32 *entries = (UInt32 *)malloc(sizeof(UInt64) * (levels - 1));
				UInt32 *associativities = (UInt32 *)malloc(sizeof(UInt64) * (levels - 1));
				for (int i = 0; i < levels - 1; i++)
				{
					entries[i] = Sim()->getCfg()->getIntArray("perf_model/" + name + "/pwc/entries", i);
					associativities[i] = Sim()->getCfg()->getIntArray("perf_model/" + name + "/pwc/associativity", i);
				}

				ComponentLatency pwc_access_latency = ComponentLatency(core->getDvfsDomain(),
					Sim()->getCfg()->getInt("perf_model/" + name + "/pwc/access_penalty"));
				ComponentLatency pwc_miss_latency = ComponentLatency(core->getDvfsDomain(),
					Sim()->getCfg()->getInt("perf_model/" + name + "/pwc/miss_penalty"));

				pwc = new PWC("pwc",
				              "perf_model/" + name + "/pwc",
				              core->getId(),
				              associativities,
				              entries,
				              levels - 1,
				              pwc_access_latency,
				              pwc_miss_latency,
				              false /* not a victim cache */ );
			}
		}

		// 'pt_walkers' is an MSHR-based structure that ensures we can do N concurrent PT walks
		pt_walkers = new MSHR(Sim()->getCfg()->getInt("perf_model/"+name+"/page_table_walkers"));
	}

	/*
	 * instantiateTLBSubsystem(...):
	 *   - Builds the normal TLBHierarchy object that includes L1 TLBs, L2 TLB, etc.
	 *   - Also reads config for "perf_model/pom_tlb" to set up the software TLB(s) that 
	 *     correspond to each possible page size.
	 *   - Each software TLB is allocated in memory (via handle_page_table_allocations) 
	 *     and stored in m_pom_tlb[i].
	 */
	void MemoryManagementUnitPOMTLB::instantiateTLBSubsystem()
	{
		tlb_subsystem = new TLBHierarchy(name, core, memory_manager, shmem_perf_model);

		int page_sizes = Sim()->getCfg()->getInt("perf_model/pom_tlb/page_sizes");

		// software_tlb_base_register[i] is the memory base address for TLB i
		software_tlb_base_register = new IntPtr[page_sizes];
		m_pom_tlb = new TLB*[page_sizes];

		for (int i = 0; i < page_sizes; i++)
		{
			int page_size = Sim()->getCfg()->getIntArray("perf_model/pom_tlb/page_size_list", i);
			int entries   = Sim()->getCfg()->getIntArray("perf_model/pom_tlb/entries", i);
			int assoc     = Sim()->getCfg()->getIntArray("perf_model/pom_tlb/assoc", i);
			String type   = Sim()->getCfg()->getStringArray("perf_model/pom_tlb/type", i);
			bool allocate_on_miss = Sim()->getCfg()->getBoolArray("perf_model/pom_tlb/allocate_on_miss", i);

			String name = "pom_tlb_" + itostr(i);

			int num_sets = entries / assoc;

			/*
			 * We assume a 48-bit address space. 
			 * The 'tag_size' is computed as the remaining bits after subtracting the index bits 
			 * (log2(num_sets)) and the page offset bits (page_size).
			 * The entry_size is the total bits needed for the TLB entry (e.g., tag_size + physical address bits).
			 */
			int tag_size   = (48 - page_size - log2(num_sets)); 
			int entry_size = tag_size + 52; // e.g. 52 bits might represent physical address + valid bits, etc.

			/*
			 * The actual memory allocated for the TLB is entries * entry_size (bits), 
			 * but we must convert to bytes, and handle alignment by rounding up.
			 */
			software_tlb_base_register[i] = Sim()->getMimicOS()->getMemoryAllocator()->handle_page_table_allocations(
				ceil(entries * entry_size / 8.0));

			int* page_size_list = new int[1];
			page_size_list[0] = page_size;

			std::cout << "[MMU:POM-TLB] Allocating software TLB for page size: " 
			          << page_size << " with " << entries << " entries and " 
			          << assoc << " ways and " << num_sets << " sets" << std::endl;
			std::cout << "[MMU:POM-TLB] Entry size: " << entry_size << std::endl;
			std::cout << "[MMU:POM-TLB] Tag size: " << tag_size << std::endl;
			std::cout << "[MMU:POM-TLB] Type: " << type << std::endl;
			std::cout << "[MMU:POM-TLB] Allocate on miss: " << allocate_on_miss << std::endl;
			std::cout << "[MMU:POM-TLB] Base register: " << software_tlb_base_register[i] << std::endl;

			// Build the TLB object for the software-managed TLB
			m_pom_tlb[i] = new TLB(name,
			                       "perf_model/pom_tlb",
			                       core->getId(),
			                       ComponentLatency(core->getDvfsDomain(), 0) /* no fixed TLB latency here*/,
			                       entries,
			                       assoc,
			                       page_size_list,
			                       1 /* #page sizes in this TLB*/,
			                       type,
			                       allocate_on_miss);
		}
	}

	/*
	 * registerMMUStats(...):
	 *   - Initializes the memory translation stats structure (translation_stats),
	 *   - Adds the counters for page faults, table walks, TLB latency, software TLB latency, etc.
	 *   - Also registers per-level TLB latencies in TLB subsystem.
	 */
	void MemoryManagementUnitPOMTLB::registerMMUStats()
	{
		bzero(&translation_stats, sizeof(translation_stats));

		// Basic counters for this MMU
		registerStatsMetric(name, core->getId(), "page_faults", &translation_stats.page_faults);
		registerStatsMetric(name, core->getId(), "total_table_walk_latency", &translation_stats.total_walk_latency);
		registerStatsMetric(name, core->getId(), "total_fault_latency", &translation_stats.total_fault_latency);
		registerStatsMetric(name, core->getId(), "total_tlb_latency", &translation_stats.total_tlb_latency);
		registerStatsMetric(name, core->getId(), "total_translation_latency", &translation_stats.total_translation_latency);
		registerStatsMetric(name, core->getId(), "total_software_tlb_latency", &translation_stats.software_tlb_latency);
		registerStatsMetric(name, core->getId(), "total_walk_delay_latency", &translation_stats.total_walk_delay_latency);

		// Per-level TLB latency stats
		translation_stats.tlb_latency_per_level = new SubsecondTime[tlb_subsystem->getTLBSubsystem().size()];
		for (UInt32 i = 0; i < tlb_subsystem->getTLBSubsystem().size(); i++)
			registerStatsMetric(name, core->getId(), "tlb_latency_" + itostr(i), &translation_stats.tlb_latency_per_level[i]);
	}

	/*
	 * performAddressTranslation(...):
	 *   - The main function that performs a translation for the given address. 
	 *   - Tries the hardware TLBs first. If that misses, tries the "software" TLB(s). 
	 *   - If both fail, does a page table walk (PTW). 
	 *   - Accumulates latencies for each step (hardware TLB, software TLB, PTW, page fault).
	 *   - Finally, inserts the new translation into TLB(s) and/or the software TLB if needed.
	 *
	 * Returns the final physical address as an IntPtr.
	 */
	IntPtr MemoryManagementUnitPOMTLB::performAddressTranslation(IntPtr eip,
	                                                             IntPtr address,
	                                                             bool instruction,
	                                                             Core::lock_signal_t lock,
	                                                             bool modeled,
	                                                             bool count)
	{
#ifdef DEBUG_MMU
		log_file << std::endl;
		log_file << "[MMU:POM-TLB] ---- Starting address translation for virtual address: "
		         << address <<  " ---- at time "
		         << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
#endif

		int number_of_page_sizes = Sim()->getMimicOS()->getNumberOfPageSizes();
		int *page_size_list = Sim()->getMimicOS()->getPageSizeList();

		SubsecondTime time = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

		// Clear completed PTW MSHR entries that have finished
		pt_walkers->removeCompletedEntries(time);

		if (count)
			translation_stats.num_translations++;

		// Normal hardware TLB hierarchy
		TLBSubsystem tlbs = tlb_subsystem->getTLBSubsystem();

		bool tlb_hit   = false;
		TLB *hit_tlb   = NULL;
		CacheBlockInfo *tlb_block_info_hit = NULL; 
		CacheBlockInfo *tlb_block_info     = NULL; 
		int hit_level  = -1;

		int page_size_result = -1; // Will track the discovered page size in bits (e.g. 12 or 21).
		IntPtr ppn_result     = 0; // Physical Page Number

		/*
		 * 1) Try each TLB level in the hardware TLB subsystem. If we get a match, we break out
		 *    after setting tlb_hit = true.
		 */
		for (UInt32 i = 0; i < tlbs.size(); i++)
		{
#ifdef DEBUG_MMU
			log_file << "[MMU] Searching TLB at level: " << i << std::endl;
#endif
			for (UInt32 j = 0; j < tlbs[i].size(); j++)
			{
				bool tlb_stores_instructions = ((tlbs[i][j]->getType() == TLBtype::Instruction) ||
				                                 (tlbs[i][j]->getType() == TLBtype::Unified));

				if (tlb_stores_instructions && instruction)
				{
					// We pass "NULL" for the page table pointer as it is not used in the modern TLB code
					tlb_block_info = tlbs[i][j]->lookup(address, time, count, lock, eip, modeled, count, NULL);
					if (tlb_block_info != NULL)
					{
						tlb_block_info_hit = tlb_block_info;
						hit_tlb = tlbs[i][j];
						hit_level = i;
						tlb_hit = true;
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
							tlb_hit = true;
						}
					}
				}
			}
			if (tlb_hit)
			{
#ifdef DEBUG_MMU
				log_file << "[MMU] TLB Hit at level: " << hit_level 
				         << " at TLB " << hit_tlb->getName() << std::endl;
#endif
				break;
			}
		}

		SubsecondTime charged_tlb_latency = SubsecondTime::Zero();

		/*
		 * 2) If TLB hit, gather TLB latencies from each level up to the level that hit 
		 *    (similar to a multi-level cache).
		 */
		if (tlb_hit)
		{
			page_size_result = tlb_block_info_hit->getPageSize();
			ppn_result       = tlb_block_info_hit->getPPN();

#ifdef DEBUG_MMU
			log_file << "[MMU] TLB Hit ? " << tlb_hit << " at level: " << hit_level
			         << " at TLB: " << hit_tlb->getName() << std::endl;
#endif

			// Retrieve the instruction or data TLB path
			if (instruction)
				tlbs = tlb_subsystem->getInstructionPath();
			else
				tlbs = tlb_subsystem->getDataPath();

			// We track the "slowest" TLB component at each level up to hit_level
			SubsecondTime tlb_latency[hit_level + 1];

			for (int i = 0; i < hit_level; i++)
			{
				for (UInt32 j = 0; j < tlbs[i].size(); j++)
				{
					tlb_latency[i] = max(tlbs[i][j]->getLatency(), tlb_latency[i]);
				}
#ifdef DEBUG_MMU
				log_file << "[MMU] Charging TLB Latency: " << tlb_latency[i]
				         << " at level: " << i << std::endl;
#endif
				translation_stats.total_tlb_latency += tlb_latency[i];
				translation_stats.tlb_latency_per_level[i] += tlb_latency[i];
				charged_tlb_latency += tlb_latency[i];
			}

			// For the actual TLB that hit, add its latency as well
			for (UInt32 j = 0; j < tlbs[hit_level].size(); j++)
			{
				if (tlbs[hit_level][j] == hit_tlb)
				{
					translation_stats.total_tlb_latency += hit_tlb->getLatency();
					charged_tlb_latency += hit_tlb->getLatency();
					translation_stats.tlb_latency_per_level[hit_level] += hit_tlb->getLatency();
#ifdef DEBUG_MMU
					log_file << "[MMU] Charging TLB Hit Latency: "
					         << hit_tlb->getLatency() << " at level: "
					         << hit_level << std::endl;
#endif
				}
			}

			// Advance the clock to reflect TLB-latency consumption
			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time + charged_tlb_latency);
			if (count)
				translation_stats.total_translation_latency += charged_tlb_latency;

#ifdef DEBUG_MMU
			log_file << "[MMU] New time after charging TLB latency: "
			         << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD)
			         << std::endl;
#endif
		}
		else
		{
			/*
			 * 2a) TLB Miss => sum the latency for all TLB levels. We do not break out at a 
			 *     partial level because we "visited" them all searching for a hit.
			 */
#ifdef DEBUG_MMU
			log_file << "[MMU] TLB Miss" << std::endl;
#endif
			SubsecondTime tlb_latency[tlbs.size()];

			for (UInt32 i = 0; i < tlbs.size(); i++)
			{
				for (UInt32 j = 0; j < tlbs[i].size(); j++)
				{
					tlb_latency[i] = max(tlbs[i][j]->getLatency(), tlb_latency[i]);
				}
#ifdef DEBUG_MMU
				log_file << "[MMU] Charging TLB Latency: "
				         << tlb_latency[i] << " at level: " << i << std::endl;
#endif
				translation_stats.total_tlb_latency += tlb_latency[i];
				charged_tlb_latency += tlb_latency[i];
			}

			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time + charged_tlb_latency);
			if (count)
				translation_stats.total_translation_latency += charged_tlb_latency;

#ifdef DEBUG_MMU
			log_file << "[MMU] New time after charging TLB latency: "
			         << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD)
			         << std::endl;
#endif
		}

		/*
		 * 3) If TLB miss, we attempt the software TLB(s). The code loops over each possible
		 *    page size in 'm_pom_tlb' to see if we have a hit. We measure the access latencies
		 *    (software_tlb_latency[i]) and pick either the one that hits or the max if all miss.
		 */
		CacheBlockInfo *software_tlb_block_info = NULL;
		bool software_tlb_hit = false;

		SubsecondTime time_before_software_tlb = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
		SubsecondTime software_tlb_latency[number_of_page_sizes];
		for (int i = 0; i < number_of_page_sizes; i++)
		{
			software_tlb_latency[i] = SubsecondTime::Zero();
		}

		SubsecondTime final_software_tlb_latency = SubsecondTime::Zero();

		if (!tlb_hit)
		{
#ifdef DEBUG_MMU
			log_file << "[MMU] TLB Miss, checking software TLB" << std::endl;
#endif
			for (int page_size = 0; page_size < number_of_page_sizes; page_size++)
			{
#ifdef DEBUG_MMU
				log_file << "[MMU] Searching software TLB for page size: "
				         << page_size_list[page_size] << std::endl;
#endif
				TLB* pom = m_pom_tlb[page_size];
				software_tlb_block_info = pom->lookup(address, time, count, lock, eip, modeled, count, NULL);

#ifdef DEBUG_MMU
				log_file << "[MMU] Software TLB Hit ? "
				         << (software_tlb_block_info != NULL)
				         << " at TLB: " << pom->getName() << std::endl;
#endif

				// Simulate the memory access that the software TLB structure does
				translationPacket packet;
				packet.eip          = eip;
				packet.instruction  = false;
				packet.lock_signal  = lock;
				packet.modeled      = modeled;
				packet.count        = count;
				packet.type         = CacheBlockInfo::block_type_t::TLB_ENTRY;

				IntPtr tag;
				UInt32 set_index;
				pom->getCache().splitAddressTLB(address, tag, set_index, page_size_list[page_size]);

#ifdef DEBUG_MMU
				log_file << "[MMU] Software TLB Lookup: " << address
				         << " at page size: " << page_size_list[page_size]
				         << " with tag: " << tag
				         << " and set index: " << set_index
				         << " and base address: "
				         << software_tlb_base_register[page_size]*4096 << std::endl;
#endif
				packet.address = software_tlb_base_register[page_size]*4096
				                 + pom->getAssoc()* pom->getEntrySize() * set_index;

#ifdef DEBUG_MMU
				log_file << "[MMU] Software TLB Address: " << packet.address << std::endl;
#endif

				software_tlb_latency[page_size] = accessCache(packet, time_before_software_tlb);

#ifdef DEBUG_MMU
				log_file << "[MMU] Software TLB Latency: " 
				         << software_tlb_latency[page_size]
				         << " at page size: " << page_size_list[page_size] 
				         << std::endl;
#endif

				if (software_tlb_block_info != NULL)
				{
					// We have a software TLB hit
					final_software_tlb_latency = software_tlb_latency[page_size];
					software_tlb_hit = true;
					ppn_result = software_tlb_block_info->getPPN();
					page_size_result = software_tlb_block_info->getPageSize();
#ifdef DEBUG_MMU
					log_file << "[MMU] Software TLB Hit at page size: "
					         << page_size_result << std::endl;
					log_file << "[MMU] Software TLB Hit PPN: " << ppn_result << std::endl;
					log_file << "[MMU] Software TLB Hit VPN: "
					         << (address >> page_size_result) << std::endl;
					log_file << "[MMU] Software TLB Hit Tag: " << tag << std::endl;
					log_file << "[MMU] Software TLB Hit Latency: "
					         << final_software_tlb_latency << std::endl;
#endif
				}
			}

			if (!software_tlb_hit)
			{
				// If all software TLBs missed, we take the max of the latencies
#ifdef DEBUG_MMU
				log_file << "[MMU] Software TLB Miss" << std::endl;
#endif
				SubsecondTime max_software_tlb_latency = SubsecondTime::Zero();
				for (int page_size = 0; page_size < number_of_page_sizes; page_size++)
				{
					max_software_tlb_latency = max(max_software_tlb_latency, software_tlb_latency[page_size]);
				}
				final_software_tlb_latency = max_software_tlb_latency;

				if (count)
					translation_stats.software_tlb_latency += final_software_tlb_latency;
			}
			else
			{
				// If we had a software TLB hit, accumulate the latency
				if (count)
					translation_stats.software_tlb_latency += final_software_tlb_latency;
			}

#ifdef DEBUG_MMU
			log_file << "[MMU] Final Software TLB Latency: "
			         << final_software_tlb_latency << std::endl;
#endif

			// Advance the clock by the final software TLB latency
			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD,
				time_before_software_tlb + final_software_tlb_latency);
			if (count)
				translation_stats.total_translation_latency += final_software_tlb_latency;

#ifdef DEBUG_MMU
			log_file << "[MMU] New time after charging Software TLB latency: "
			         << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD)
			         << std::endl;
#endif
		}

		/*
		 * 4) If both hardware TLB and software TLB miss, we do a page table walk (PTW).
		 *    This includes possibly incurring a page fault if the memory is not resident,
		 *    and also a possible delay if all PT walkers are busy (pt_walkers->getSlotAllocationDelay).
		 */
		SubsecondTime total_fault_latency = SubsecondTime::Zero();
		SubsecondTime total_walk_latency  = SubsecondTime::Zero();

		if (!tlb_hit && !software_tlb_hit)
		{
			SubsecondTime time_for_pt = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

			MSHREntry pt_walker_entry; 
			pt_walker_entry.request_time = time_for_pt;

			// Possibly wait if all PT walkers are busy
			SubsecondTime delay = pt_walkers->getSlotAllocationDelay(pt_walker_entry.request_time);
			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time_for_pt + delay);
			if(count)
				translation_stats.total_translation_latency += delay;

#ifdef DEBUG_MMU
			log_file << "[MMU] New time after charging the PT walker allocation delay: "
			         << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD)
			         << std::endl;
#endif

			// Perform the PT walk
			int app_id = core->getThread()->getAppId();
			PageTable* page_table = Sim()->getMimicOS()->getPageTable(app_id);

			const bool restart_walk_upon_page_fault = true;
			auto ptw_result = performPTW(address, modeled, count, false, eip, lock, page_table, restart_walk_upon_page_fault);

#ifdef DEBUG_MMU
			log_file << "[MMU] PTW Result: "
			         << get<0>(ptw_result) << " "
			         << get<1>(ptw_result) << " "
			         << get<2>(ptw_result) << " "
			         << get<3>(ptw_result) << std::endl;
#endif

			total_walk_latency = get<0>(ptw_result);
			if (count)
			{
				translation_stats.total_walk_latency += total_walk_latency;
				translation_stats.page_table_walks++;
			}

			bool caused_page_fault = get<1>(ptw_result);
			if (caused_page_fault)
			{
#ifdef DEBUG_MMU
				log_file << "[MMU] Page Fault occured" << std::endl;
#endif
				SubsecondTime m_page_fault_latency = Sim()->getMimicOS()->getPageFaultLatency();
				if (count)
				{
					translation_stats.page_faults++;
					translation_stats.total_fault_latency += m_page_fault_latency;
				}
				total_fault_latency = m_page_fault_latency;
			}

			pt_walker_entry.completion_time = time_for_pt + delay 
			                                  + total_walk_latency
			                                  + total_fault_latency;

			pt_walkers->allocate(pt_walker_entry);

			// Move the time to the end of the PTW + possible page fault
			if (caused_page_fault)
			{
				PseudoInstruction *i = new PageFaultRoutineInstruction(total_fault_latency);
				getCore()->getPerformanceModel()->queuePseudoInstruction(i);
				shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, pt_walker_entry.completion_time);
				if(count)
					translation_stats.total_translation_latency += total_walk_latency;
			}
			else
			{
				shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, pt_walker_entry.completion_time);
				if(count)
					translation_stats.total_translation_latency += total_walk_latency;
			}

			ppn_result       = get<2>(ptw_result);
			page_size_result = get<3>(ptw_result);

#ifdef DEBUG_MMU
			log_file << "[MMU] New time after charging the PT walker completion time: "
			         << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD)
			         << std::endl;
#endif
		}

#ifdef DEBUG_MMU
		log_file << "[MMU] Total Walk Latency: " << total_walk_latency << std::endl;
		log_file << "[MMU] Total Fault Latency: " << total_fault_latency << std::endl;
#endif

		// 5) Allocate the translation in intermediate TLB levels (if needed).
		if (instruction)
			tlbs = tlb_subsystem->getInstructionPath();
		else
			tlbs = tlb_subsystem->getDataPath();

		std::map<int, vector<tuple<IntPtr, int>>> evicted_translations;
		int tlb_levels = tlbs.size();

		// Some TLB hierarchies might have a "prefetch" TLB, skipping the last level for 
		// "allocate on miss." We handle that with isPrefetchEnabled checks if relevant.
		if (tlb_subsystem->isPrefetchEnabled())
		{
			tlb_levels = tlbs.size() - 1;
#ifdef DEBUG_MMU
			log_file << "[MMU] Prefetching is enabled" << std::endl;
#endif
		}

		// For each TLB level, we insert or attempt to insert the final translation 
		// if "allocate on miss" is set and if it supports our page size.
		for (int i = 0; i < tlb_levels; i++)
		{
			for (UInt32 j = 0; j < tlbs[i].size(); j++)
			{
				// If there's any "evicted" translation from the previous level, we attempt to place it here
				if ((i > 0) && (!evicted_translations[i - 1].empty()))
				{
					tuple<bool, IntPtr, int> result;

#ifdef DEBUG_MMU
					log_file << "[MMU] There are evicted translations from level: " << (i - 1) << std::endl;
#endif
					for (auto &evicted : evicted_translations[i - 1])
					{
#ifdef DEBUG_MMU
						log_file << "[MMU] Evicted Translation: " << get<0>(evicted) << std::endl;
#endif
						if (tlbs[i][j]->supportsPageSize(page_size_result))
						{
#ifdef DEBUG_MMU
							log_file << "[MMU] Allocating evicted entry in TLB: Level = "
							         << i << " Index =  " << j << std::endl;
#endif
							result = tlbs[i][j]->allocate(get<0>(evicted), time, count, lock,
							                              get<1>(evicted), ppn_result);

							if (get<0>(result) == true)
							{
								evicted_translations[i].push_back(
									make_tuple(get<1>(result), get<2>(result)));
							}
						}
					}
				}

				// If the TLB can store this page size, is "allocate on miss," and we either missed 
				// or want to replicate the translation in lower levels:
				if (tlbs[i][j]->supportsPageSize(page_size_result)
				    && tlbs[i][j]->getAllocateOnMiss()
				    && (!tlb_hit || (tlb_hit && hit_level > i)))
				{
#ifdef DEBUG_MMU
					log_file << "[MMU] " << tlbs[i][j]->getName()
					         << " supports page size: " << page_size_result << std::endl;
					log_file << "[MMU] Allocating in TLB: Level = " << i
					         << " Index = " << j
					         << " with page size: " << page_size_result
					         << " and VPN: " << (address >> page_size_result) << std::endl;
#endif
					auto result = tlbs[i][j]->allocate(address, time, count, lock, page_size_result, ppn_result);
					if (get<0>(result) == true)
					{
						evicted_translations[i].push_back(
							make_tuple(get<1>(result), get<2>(result)));
					}
				}
			}
		}

		/*
		 * 6) Allocate the translation in the software TLB if we had a miss there. 
		 *    We check each software TLB if it supports page_size_result and is "allocate on miss."
		 */
		if (!software_tlb_hit && !tlb_hit)
		{
			for (int page_size = 0; page_size < number_of_page_sizes; page_size++)
			{
				TLB* pom = m_pom_tlb[page_size];
				if (pom->supportsPageSize(page_size_result) && pom->getAllocateOnMiss())
				{
#ifdef DEBUG_MMU
					log_file << "[MMU] Allocating in Software TLB: Page Size = "
					         << page_size_list[page_size]
					         << " with VPN: " << (address >> page_size_result)
					         << " with PPN: " << ppn_result << std::endl;
#endif
					pom->allocate(address, time, count, lock, page_size_result, ppn_result);
				}
			}
		}

		/*
		 * 7) Compute the final physical address:
		 *    final_physical_address = (ppn_result << 12) + (offset)  if page_size_result=12
		 *    or similarly with 2MB offset for bigger pages. 
		 *    We use 'address % page_size_in_bytes' for the offset, 
		 *    and 'ppn_result * base_page_size_in_bytes' for the base.
		 */
		int page_size_in_bytes = pow(2, page_size_result);
		int base_page_size_in_bytes = pow(2, 12);

		IntPtr final_physical_address = (ppn_result * base_page_size_in_bytes)
		                                + (address % page_size_in_bytes);

#ifdef DEBUG_MMU
		log_file << "[MMU] Offset: " << (address % page_size_in_bytes) << std::endl;
		log_file << "[MMU] Physical Address: " << final_physical_address
		         << " PPN: " << ppn_result*base_page_size_in_bytes
		         << " Page Size: " << page_size_result << std::endl;
		log_file << "[MMU] Physical Address: " << bitset<64>(final_physical_address)
		         << " PPN:" << bitset<64>(ppn_result*base_page_size_in_bytes)
		         << " Offset: " << bitset<64>(address % page_size_in_bytes) << std::endl;
		log_file << "[MMU] Total translation latency: "
		         << charged_tlb_latency + final_software_tlb_latency + total_walk_latency
		         << std::endl;
		log_file << "[MMU] Total fault latency: " << total_fault_latency << std::endl;
		log_file << "[MMU] ---- Ending address translation for virtual address: "
		         << address << " ----" << std::endl;
#endif

		return final_physical_address;
	}

	/*
	 * filterPTWResult(...):
	 *   - A helper function that "filters" out PTW accesses that are served by the Page Walk Cache (PWC),
	 *     removing them from the list of actual memory accesses needed for the page table walk.
	 *   - If m_pwc_enabled is true, it checks each PT walk-level address. If found in PWC, we skip it.
	 *   - This can reduce the number of memory accesses needed by the final PTW result.
	 */
	PTWResult MemoryManagementUnitPOMTLB::filterPTWResult(PTWResult ptw_result,
	                                                      PageTable *page_table,
	                                                      bool count)
	{
		accessedAddresses ptw_accesses;

		if (m_pwc_enabled)
		{
			accessedAddresses original_ptw_accesses = get<1>(ptw_result);
			for (UInt32 i = 0; i < get<1>(ptw_result).size(); i++)
			{
				bool pwc_hit = false;

				int level = get<1>(original_ptw_accesses[i]);
				IntPtr pwc_address = get<2>(original_ptw_accesses[i]);

#ifdef DEBUG_MMU
				log_file << "[MMU] Checking PWC for address: "
				         << pwc_address << " at level: " << level << std::endl;
#endif
				if (level < 3)
				{
					// If the level is not the first, we see if the PWC has it
					pwc_hit = pwc->lookup(pwc_address, SubsecondTime::Zero(), true, level, count);
				}

#ifdef DEBUG_MMU
				log_file << "[MMU] PWC HIT: " << pwc_hit
				         << " level: " << level << std::endl;
#endif
				if (!pwc_hit)
				{
					// If not in PWC, we keep it in the final list (ptw_accesses) 
					ptw_accesses.push_back(get<1>(ptw_result)[i]);
				}
			}
		}
		// Return an updated PTWResult with the filtered addresses
		return PTWResult(get<0>(ptw_result), ptw_accesses,
		                 get<2>(ptw_result), get<3>(ptw_result), get<4>(ptw_result));
	}

	/*
	 * discoverVMAs(...):
	 *   - Hook for the OS to discover Virtual Memory Areas. Not currently implemented.
	 */
	void MemoryManagementUnitPOMTLB::discoverVMAs()
	{
	}

} // namespace ParametricDramDirectoryMSI
