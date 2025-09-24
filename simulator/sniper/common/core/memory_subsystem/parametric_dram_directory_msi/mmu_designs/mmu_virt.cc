
#include "mmu.h"
#include "mmu_base.h"
#include "mmu_virt.h"
#include "memory_manager.h"
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "pagetable_factory.h"
#include "pagetable_radix.h"
#include "mimicos.h"
#include "instruction.h"
#include "core.h"
#include "thread.h"
#include <iostream>
#include <fstream>
#include <algorithm>


/* If you enable this flag, you will see debug messages that will help you understand the address translation process
 * This is useful for debugging purposes 
*/
//#define DEBUG_MMU 

using namespace std;

namespace ParametricDramDirectoryMSI
{

	MemoryManagementUnitVirt::MemoryManagementUnitVirt(Core *_core, MemoryManager *_memory_manager, ShmemPerfModel *_shmem_perf_model, String _name, MemoryManagementUnitBase *_nested_mmu)
	: MemoryManagementUnitBase(_core, _memory_manager, _shmem_perf_model, _name, _nested_mmu), memory_manager(_memory_manager)
	{
		std::cout << std::endl;
		std::cout << "[MMU] Initializing Virt MMU for core " << core->getId() << std::endl;
		// mmu_N.log is the log file for the MMU of core N
		log_file = std::ofstream();
		log_file_name = "mmu.log." + std::to_string(core->getId());
		log_file.open(log_file_name.c_str());

		instantiatePageTableWalker(); // This instantiates the page table walker
		instantiateTLBSubsystem(); // This instantiates the TLB hierarchy
		registerMMUStats(); // This instantiates the MMU stats
		std::cout << std::endl;

	}

	MemoryManagementUnitVirt::~MemoryManagementUnitVirt()
	{
		log_file.close();
		delete tlb_subsystem;
		delete pt_walkers;
		delete[] translation_stats.tlb_latency_per_level;
	}

	void MemoryManagementUnitVirt::instantiatePageTableWalker()
	{
		String mimicos_name = Sim()->getMimicOS_VM()->getName();
		pt_walkers = new MSHR(Sim()->getCfg()->getInt("perf_model/"+name+"/page_table_walkers"));
			
	}

	void MemoryManagementUnitVirt::instantiateTLBSubsystem()
	{
		tlb_subsystem = new TLBHierarchy(name, core, memory_manager, shmem_perf_model);
	}
	
	void MemoryManagementUnitVirt::registerMMUStats()
	{
		bzero(&translation_stats, sizeof(translation_stats));

		// Statistics for the whole MMU

		registerStatsMetric(name, core->getId(), "num_page_table_walks", &translation_stats.num_page_table_walks);
		registerStatsMetric(name, core->getId(), "num_translations", &translation_stats.num_translations);
		registerStatsMetric(name, core->getId(), "total_translation_latency", &translation_stats.total_translation_latency);
		registerStatsMetric(name, core->getId(), "total_walk_latency", &translation_stats.total_walk_latency);
		registerStatsMetric(name, core->getId(), "total_tlb_latency", &translation_stats.total_tlb_latency);

		// Statistics for TLB subsystem
		translation_stats.tlb_latency_per_level = new SubsecondTime[tlb_subsystem->getTLBSubsystem().size()];

		// Keep track of the tlb latency for each TLB level
		for (UInt32 i = 0; i < tlb_subsystem->getTLBSubsystem().size(); i++){
			registerStatsMetric(name, core->getId(), "tlb_latency_" + itostr(i), &translation_stats.tlb_latency_per_level[i]);
		}
	
	}

	IntPtr MemoryManagementUnitVirt::performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count)
	{

#ifdef DEBUG_MMU
		log_file << std::endl;
		log_file << "[MMU] ---- Starting address translation for virtual address: " << address <<  " ---- at time " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
#endif

		IntPtr final_addr;

		SubsecondTime time = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD); 

		// Cleanup so that we do not have any completed entries left inside the MSHRs of the PT walkers
		pt_walkers->removeCompletedEntries(time);

		if (count)
			translation_stats.num_translations++;

		TLBSubsystem tlbs = tlb_subsystem->getTLBSubsystem(); // Get the TLB hierarchy

		// @hsongara: Perform translation
		bool hit = false; // Variables to keep track of TLB hits
		TLB *hit_tlb = NULL; // We need to keep track of the TLB that hit

		CacheBlockInfo *tlb_block_info_hit = NULL; // If there is a TLB hit, we need to keep track of the block info (which eventually contains the translation)
		CacheBlockInfo *tlb_block_info = NULL; // This is the block info that we get from the TLB lookup
		
		int hit_level = -1;
		int page_size = 12; // This is the page size in bits (4KB). This variable will reflect if the virtual address is mapped to a 4KB page or a 2MB page
		
		// We iterate through the TLB hierarchy to find if there is a TLB hit
		for (UInt32 i = 0; i < tlbs.size(); i++)
		{
#ifdef DEBUG_MMU
			log_file << "[MMU] Searching TLB at level: " << i << std::endl;
#endif
			for (UInt32 j = 0; j < tlbs[i].size(); j++)
			{
				bool tlb_stores_instructions = (tlbs[i][j]->getType() == TLBtype::Instruction) || (tlbs[i][j]->getType() == TLBtype::Unified);

				// If the TLB stores instructions, we need to check if the address is an instruction address
				if (tlb_stores_instructions && instruction)
				{
					// @kanellok: Passing the page table to the TLB lookup function is a legacy from the old TLB implementation. 
					// It is not used in the current implementation.

					tlb_block_info = tlbs[i][j]->lookup(address, time, count, lock, eip, modeled, count, NULL);

					if (tlb_block_info != NULL) // If we have a hit in TLB
					{
						tlb_block_info_hit = tlb_block_info;
						hit_tlb = tlbs[i][j]; // Keep track of the TLB that hit
						hit_level = i; // Keep track of the level of the TLB that hit
						hit = true; // We have a hit
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
							hit = true;
						}
					}
				}
			}
			if (hit) // We search every TLB in the level until we find a hit but we break if we find a hit in the level 
			{
#ifdef DEBUG_MMU
			log_file << "[MMU] TLB Hit at level: " << hit_level << " at TLB " << hit_tlb->getName() << std::endl;
#endif
				break;
			}
		}

		// @hsongara: Charge TLB access latencies

		SubsecondTime charged_tlb_latency = SubsecondTime::Zero();
		
		if (hit)
		{
			#ifdef DEBUG_MMU
				log_file << "[MMU] TLB Hit ? " << hit << " at level: " << hit_level << " at TLB: " << hit_tlb->getName() << std::endl;
			#endif
			
			if (instruction)
				tlbs = tlb_subsystem->getInstructionPath(); // Get the TLB path for instructions
			else
				tlbs = tlb_subsystem->getDataPath(); // Get the TLB path for data

			SubsecondTime tlb_latency[hit_level + 1]; // We need to keep track of the latency of the TLBs at each level of the hierarchy

			// We iterate through the TLBs to find the slowest component at each level of the hierarchy until the level where we had a hit
			for (int i = 0; i < hit_level; i++) 
			{
				for (UInt32 j = 0; j < tlbs[i].size(); j++) 
				{
					tlb_latency[i] = max(tlbs[i][j]->getLatency(), tlb_latency[i]);
				}
#ifdef DEBUG_MMU
				log_file << "[MMU] Charging TLB Latency: " << tlb_latency[i] << " at level: " << i << std::endl;
#endif
				translation_stats.total_tlb_latency += tlb_latency[i];
				translation_stats.tlb_latency_per_level[i] += tlb_latency[i];
				charged_tlb_latency += tlb_latency[i];
			}

			for (UInt32 j = 0; j < tlbs[hit_level].size(); j++) // We iterate through the TLBs in the level where we had a hit
			{ 
				if (tlbs[hit_level][j] == hit_tlb) // We find the TLB that hit
				{
					translation_stats.total_tlb_latency += hit_tlb->getLatency(); // We charge the latency of the TLB that hit
					charged_tlb_latency += hit_tlb->getLatency();
					translation_stats.tlb_latency_per_level[hit_level] += hit_tlb->getLatency();

#ifdef DEBUG_MMU
					log_file << "[MMU] Charging TLB Hit Latency: " << hit_tlb->getLatency() << " at level: " << hit_level << std::endl;
#endif
				}
			}

			// Progress the clock to the time after the TLB latency
			// This is done so that the PTW starts after the TLB latency
			// @kanellok: Be very careful if you want to play around with the timing model
			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time + charged_tlb_latency); 
			#ifdef DEBUG_MMU
				log_file << "[MMU] New time after charging TLB latency: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
			#endif

		}

		SubsecondTime tlb_latency[tlbs.size()];

		if (!hit)
		{
#ifdef DEBUG_MMU
		log_file << "[MMU] TLB Miss" << std::endl;
#endif
			for (UInt32 i = 0; i < tlbs.size(); i++) 
			{
				for (UInt32 j = 0; j < tlbs[i].size(); j++)
				{
					tlb_latency[i] = max(tlbs[i][j]->getLatency(), tlb_latency[i]);
				}
#ifdef DEBUG_MMU
				log_file << "[MMU] Charging TLB Latency: " << tlb_latency[i] << " at level: " << i << std::endl;
#endif
				translation_stats.total_tlb_latency += tlb_latency[i];
				charged_tlb_latency += tlb_latency[i];
			}
			
			// We progress the time by the charged TLB latency so that the PTW starts after the TLB latency
			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time + charged_tlb_latency);
			#ifdef DEBUG_MMU
				log_file << "[MMU] New time after charging TLB latency: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
			#endif
		}

		// @hsongara: We perform a nested PTW for a TLB miss
		// We need to keep track of the total walk latency
		SubsecondTime total_walk_latency = SubsecondTime::Zero();
		
		// This is the physical page number that we will get from the PTW
		IntPtr ppn_result;

		// We only trigger the PTW if there was a TLB miss
		if (!hit)
		{	
			if (count)
				translation_stats.num_page_table_walks++;

			// Keep track of the time before the PTW starts
			SubsecondTime time_for_pt = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

			// @hsongara: Get a PT walker and charge corresponding latency
			struct MSHREntry pt_walker_entry;
			pt_walker_entry.request_time = time_for_pt;

			SubsecondTime delay = pt_walkers->getSlotAllocationDelay(pt_walker_entry.request_time);

			// We switch the time to the time when the PT walker is allocated so that we start the PTW at that time
			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time_for_pt + delay);
			#ifdef DEBUG_MMU
				log_file << "[MMU] New time after charging the PT walker allocation delay: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
			#endif

			int app_id = core->getThread()->getAppId();
			PageTable* guest_page_table = Sim()->getMimicOS_VM()->getPageTable(app_id);
			PageTable* host_page_table = Sim()->getMimicOS()->getPageTable(app_id);

			// @hsongara: Perform gL4 to gL1
			const bool restart_walk = true;
			auto ptw_result = performPTW(address, modeled, count, false, eip, lock, guest_page_table, restart_walk);
			SubsecondTime total_ptw_latency = get<0>(ptw_result);

			// @hsongara: Charge PTW latency
			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time_for_pt + delay + total_ptw_latency);

			// @hsongara: Translate gPA to an hPA
			IntPtr gppn_result = get<2>(ptw_result);
			int page_size_in_bytes = pow(2, page_size);
			int base_page_size_in_bytes = pow(2, 12);
			IntPtr gpa_addr = (gppn_result * base_page_size_in_bytes) + (address % page_size_in_bytes);

			// For the gPA to hPA translation only 2 levels are cached
			MemoryManagementUnit* nested_mmu_cast = dynamic_cast<MemoryManagementUnit*>(nested_mmu);
			int m_pwc_max_level = nested_mmu_cast->getMaxPWCLevel();
			nested_mmu_cast->setMaxPWCLevel(2);

			auto gpa_ptw_result = nested_mmu->performPTW(gpa_addr, modeled, count, false, eip, lock, host_page_table, restart_walk);
			SubsecondTime total_gpa_ptw_latency = get<0>(gpa_ptw_result);
			IntPtr ppn_result = get<2>(gpa_ptw_result);
			nested_mmu_cast->setMaxPWCLevel(m_pwc_max_level);	// Reset the max PWC level

			// @hsongara: Charge gPA PTW latency
			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time_for_pt + delay + total_ptw_latency + total_gpa_ptw_latency);

			if (count)
				translation_stats.total_walk_latency += delay + total_ptw_latency + total_gpa_ptw_latency;

			pt_walker_entry.completion_time = time_for_pt + delay + total_ptw_latency + total_gpa_ptw_latency;
			pt_walkers->allocate(pt_walker_entry);

			#ifdef DEBUG_MMU
				log_file << "[MMU] New time after charging the PT walker completion time: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
			#endif
		}
		else
		{
			/* In this scenario, we have a TLB hit. We do not need to perform 
			a PTW and we can directly get the translation from the TLB block info */
			
			page_size = tlb_block_info_hit->getPageSize();
			ppn_result = tlb_block_info_hit->getPPN();
		}

#ifdef DEBUG_MMU
		log_file << "[MMU] Total Walk Latency: " << total_walk_latency << std::endl;
#endif

		if (instruction)
			tlbs = tlb_subsystem->getInstructionPath();
		else
			tlbs = tlb_subsystem->getDataPath();

		std::map<int, vector<tuple<IntPtr,IntPtr,int>>> evicted_translations;

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
						if (tlbs[i][j]->supportsPageSize(get<2>(evicted_translations[i - 1][k])))
						{
#ifdef DEBUG_MMU
							log_file << "[MMU] Allocating evicted entry in TLB: Level = " << i << " Index =  " << j << std::endl;
#endif

							auto result = tlbs[i][j]->allocate(get<0>(evicted_translations[i - 1][k]), time, count, lock, get<2>(evicted_translations[i - 1][k]), get<1>(evicted_translations[i - 1][k]));

							// If the allocation was successful and we have an evicted translation, 
							// we need to add it to the evicted translations vector for

							if (get<0>(result) == true)
							{
								evicted_translations[i].push_back(make_tuple(get<1>(result), get<2>(result), get<3>(result)));
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

					auto result = tlbs[i][j]->allocate(address, time, count, lock, page_size, ppn_result);
					if (get<0>(result) == true)
					{
						evicted_translations[i].push_back(make_tuple(get<1>(result), get<2>(result), get<3>(result)));
					}
				}

			}
		}


		// @hsongara: Finalize stats
		SubsecondTime total_translation_latency = charged_tlb_latency + total_walk_latency;
		translation_stats.total_translation_latency += total_translation_latency;

		int page_size_in_bytes = pow(2, page_size);
		int base_page_size_in_bytes = pow(2, 12);

		IntPtr final_physical_address = (ppn_result * base_page_size_in_bytes) + (address % page_size_in_bytes);

		// We return the total translation latency and the physical address
		return final_physical_address;
	}

	SubsecondTime MemoryManagementUnitVirt::accessCache(translationPacket packet, SubsecondTime t_start, bool is_prefetch)
	{

#ifdef DEBUG_MMU
		log_file_mmu << std::endl;
		log_file_mmu << "[MMU_VIRT] ---- Starting cache access from MMU " << std::endl;
#endif 
		SubsecondTime host_translation_latency = SubsecondTime::Zero();
		IntPtr host_physical_address = packet.address;
		
		// If there is a nested MMU, perform address translation to translate the guest physical address to the host physical address
		if (nested_mmu != nullptr){
			auto host_translation_result = nested_mmu->performAddressTranslation(packet.eip, packet.address, packet.instruction, packet.lock_signal , packet.modeled, packet.count);
			host_physical_address = host_translation_result;
			packet.address = host_physical_address;
		}	
		
		IntPtr cache_address = ((IntPtr)(packet.address)) & (~((64 - 1)));
		CacheCntlr *l1d_cache = memory_manager->getCacheCntlrAt(core->getId(), MemComponent::component_t::L1_DCACHE);

		// Update the elapsed time in the performance model so that we send the request to the cache at the correct time
		// Example: we need to access 4 levels of the page table:
		// 1. Access the first level of the page table at time t_start
		// 2. Access the second level of the page table at time t_start + latency of the first level
		// 3. Access the third level of the page table at time t_start + latency of the first level + latency of the second level
		// ..

#ifdef DEBUG_MMU
		log_file_mmu << "[MMU_VIRT] Accessing cache with  address: " << packet.address << " at time " << t_start << std::endl;
#endif 
		HitWhere::where_t hit_where = HitWhere::UNKNOWN;
		if(is_prefetch){

#ifdef DEBUG_MMU
			log_file_mmu << "Prefetching address: " << packet.address << " at time " << t_start << std::endl;
#endif
			IntPtr cache_address = ((IntPtr)(packet.address)) & (~((64 - 1)));

			CacheCntlr *l2_cache = memory_manager->getCacheCntlrAt(core->getId(), MemComponent::L2_CACHE);
			l2_cache->doPrefetch(packet.eip, cache_address, shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD), CacheBlockInfo::block_type_t::PAGE_TABLE);
		}
		else {
			hit_where = l1d_cache->processMemOpFromCore(
			packet.eip,
			packet.lock_signal,
			Core::mem_op_t::READ,
			cache_address, 0,
			NULL, 8,
			packet.modeled,
			packet.count, packet.type, host_translation_latency);

#ifdef DEBUG_MMU
			log_file_mmu << "[MMU_VIRT] Cache hit where: " << HitWhereString(hit_where) << std::endl;
#endif
			if (HitWhereString(hit_where) == "dram-local")
				dram_accesses_during_last_walk++;
		}

		SubsecondTime t_end = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

		// Restore the elapsed time in the performance model
		shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, t_start);

		// Tag the cache block with the block type (e.g., page table data)
		memory_manager->tagCachesBlockType(packet.address, packet.type);

#ifdef DEBUG_MMU
		log_file_mmu << "[MMU_VIRT] ---- Finished cache access from MMU " << std::endl;
		log_file_mmu << std::endl;
#endif

		return t_end - t_start;
	}

	PTWResult MemoryManagementUnitVirt::filterPTWResult(PTWResult ptw_result, PageTable *page_table, bool count)
	{
		return PTWResult();
	}
	
	/*
	We do not need to use VMAs in the current implementation of the MMU
	*/
	void MemoryManagementUnitVirt::discoverVMAs()
	{
		return ;
	}

}