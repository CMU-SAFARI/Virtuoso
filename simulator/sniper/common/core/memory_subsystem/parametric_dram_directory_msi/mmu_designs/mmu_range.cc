
#include "mmu_range.h"
#include "memory_manager.h"
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "core.h"
#include "thread.h"
#include "mmu_base.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include "rangelb.h"
#include "simulator.h"
#include "physical_memory_allocator.h"
#include "mimicos.h"
#include "vma.h"

//#define DEBUG_MMU

using namespace std;

namespace ParametricDramDirectoryMSI
{

	RangeMMU::RangeMMU(Core *_core, MemoryManager *_memory_manager, ShmemPerfModel *_shmem_perf_model, String _name, MemoryManagementUnitBase *_nested_mmu)
		: MemoryManagementUnitBase(_core, _memory_manager, _shmem_perf_model, _name, _nested_mmu)

	{
		log_file_name = "mmu_range.log";
		log_file_name = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + log_file_name;
		log_file.open(log_file_name);

		instantiateRangeTableWalker();
		instantiatePageTableWalker();
		instantiateTLBSubsystem();
		registerMMUStats();
	}

	RangeMMU::~RangeMMU()
	{
		delete tlb_subsystem;
		delete range_lb;
	}

	void RangeMMU::instantiateRangeTableWalker()
	{
		int num_sets = Sim()->getCfg()->getInt("perf_model/" + name + "/rlb/num_sets");
		ComponentLatency latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/" + name + "/rlb/latency"));
		range_lb = new RLB(core, "rlb", latency, num_sets);
	}

	void RangeMMU::instantiatePageTableWalker()
	{
		String mimicos_name = Sim()->getMimicOS()->getName();
		String page_table_type = Sim()->getCfg()->getString("perf_model/" + mimicos_name + "/page_table_type");
		String page_table_name = Sim()->getCfg()->getString("perf_model/" + mimicos_name + "/page_table_name");

		if (page_table_type == "radix")
		{
			int levels = Sim()->getCfg()->getInt("perf_model/" + mimicos_name + "/" + page_table_name + "/levels");
			m_pwc_enabled = Sim()->getCfg()->getBool("perf_model/" + name + "/pwc/enabled");

			if (m_pwc_enabled)
			{

				std::cout << "[MMU] Page walk caches are enabled" << std::endl;

				UInt32 *entries = (UInt32 *)malloc(sizeof(UInt64) * (levels - 1));
				UInt32 *associativities = (UInt32 *)malloc(sizeof(UInt64) * (levels - 1));
				for (int i = 0; i < levels - 1; i++)
				{
					entries[i] = Sim()->getCfg()->getIntArray("perf_model/" + name + "/pwc/entries", i);
					associativities[i] = Sim()->getCfg()->getIntArray("perf_model/" + name + "/pwc/associativity", i);
				}

				ComponentLatency pwc_access_latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/" + name + "/pwc/access_penalty"));
				ComponentLatency pwc_miss_latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/" + name + "/pwc/miss_penalty"));
				pwc = new PWC("pwc", "perf_model/" + name + "/pwc", core->getId(), associativities, entries, levels - 1, pwc_access_latency, pwc_miss_latency, false);
			}
		}

		pt_walkers = new MSHR(Sim()->getCfg()->getInt("perf_model/" + name + "/page_table_walkers"));
	}

	void RangeMMU::instantiateTLBSubsystem()
	{
		tlb_subsystem = new TLBHierarchy(name, core, memory_manager, shmem_perf_model);
	}

	void RangeMMU::registerMMUStats()
	{

		bzero(&translation_stats, sizeof(translation_stats));

		translation_stats.tlb_latency_per_level = new SubsecondTime[tlb_subsystem->getTLBSubsystem().size()];
		// Statistics for the whole MMU
		registerStatsMetric(name, core->getId(), "num_translations", &translation_stats.num_translations);
		registerStatsMetric(name, core->getId(), "requests_resolved_by_rlb", &translation_stats.requests_resolved_by_rlb);
		registerStatsMetric(name, core->getId(), "requests_resolved_by_rlb_latency", &translation_stats.requests_resolved_by_rlb_latency);
		registerStatsMetric(name, core->getId(), "total_range_walk_latency", &translation_stats.total_range_walk_latency);
		registerStatsMetric(name, core->getId(), "page_table_walks", &translation_stats.page_table_walks);
		registerStatsMetric(name, core->getId(), "total_walk_latency", &translation_stats.total_walk_latency);
		registerStatsMetric(name, core->getId(), "page_faults", &translation_stats.page_faults);
		registerStatsMetric(name, core->getId(), "total_fault_latency", &translation_stats.total_fault_latency);
		registerStatsMetric(name, core->getId(), "total_tlb_latency", &translation_stats.total_tlb_latency);
		registerStatsMetric(name, core->getId(), "requests_resolved_by_tlb_latency", &translation_stats.requests_resolved_by_tlb_latency);
		registerStatsMetric(name, core->getId(), "total_translation_latency", &translation_stats.total_translation_latency);

		for (UInt32 i = 0; i < tlb_subsystem->getTLBSubsystem().size(); i++)
			registerStatsMetric(name, core->getId(), "tlb_latency_" + itostr(i), &translation_stats.tlb_latency_per_level[i]);

		std::cout << "[MMU] Registered MMU Stats" << std::endl;
	}

	IntPtr RangeMMU::performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count)
	{

#ifdef DEBUG_MMU
		log_file << std::endl;
		log_file << "[MMU] ---- Starting address translation for virtual address: " << address << " ---- at time " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
#endif

		// We need to check if the address is in the VMAs of the application

		findVMA(address);

		SubsecondTime time = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

		// Cleanup so that we do not have any completed entries left inside the MSHRs of the PT walkers
		pt_walkers->removeCompletedEntries(time);

		if (count)
			translation_stats.num_translations++;

		TLBSubsystem tlbs = tlb_subsystem->getTLBSubsystem(); // Get the TLB hierarchy

		//  This is the time we start the translation process
		// If there is a metadata table, we need to walk it to get the metadata information

		bool tlb_hit = false; // Variables to keep track of TLB hits
		TLB *hit_tlb = NULL;  // We need to keep track of the TLB that hit

		CacheBlockInfo *tlb_block_info_hit = NULL; // If there is a TLB hit, we need to keep track of the block info (which eventually contains the translation)
		CacheBlockInfo *tlb_block_info = NULL;	   // This is the block info that we get from the TLB lookup

		int hit_level = -1; // This is the level of the TLB that hit
		int page_size = -1; // This is the page size in bits (4KB). This variable will reflect if the virtual address is mapped to a 4KB page or a 2MB page
		IntPtr ppn_result;	// This is the physical page number that we get when we resolve address translation

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
						hit_level = i;		  // Keep track of the level of the TLB that hit
						tlb_hit = true;		  // We have a hit
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
			if (tlb_hit) // We search every TLB in the level until we find a hit but we break if we find a hit in the level
			{
#ifdef DEBUG_MMU
				log_file << "[MMU] TLB Hit at level: " << hit_level << " at TLB " << hit_tlb->getName() << std::endl;
#endif
				break;
			}
		}

		SubsecondTime charged_tlb_latency = SubsecondTime::Zero();

		// If we have a TLB hit, we need to charge the TLB hit latency
		if (tlb_hit)
		{
#ifdef DEBUG_MMU
			log_file << "[MMU] TLB Hit ? " << tlb_hit << " at level: " << hit_level << " at TLB: " << hit_tlb->getName() << std::endl;
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

			// We need to keep track of the page size and the physical page number that we get from the TLB
			page_size = tlb_block_info_hit->getPageSize();
			ppn_result = tlb_block_info_hit->getPPN();

			// Progress the clock to the time after the TLB latency
			// This is done so that the PTW starts after the TLB latency
			// @kanellok: Be very careful if you want to play around with the timing model
			if (count){

				translation_stats.requests_resolved_by_tlb_latency += charged_tlb_latency;
				translation_stats.total_translation_latency += charged_tlb_latency;
			}

			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time + charged_tlb_latency);
#ifdef DEBUG_MMU
			log_file << "[MMU] New time after charging TLB latency: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
#endif
		}

		// If we have a TLB miss, we need to charge the TLB latency based on the slowest component
		// at each level of the hierarchy

		SubsecondTime tlb_latency[tlbs.size()];

		// If we have a TLB miss, we need to charge the TLB latency based on the slowest component
		// at each level of the hierarchy, across all levels of the hierarchy

		if (!tlb_hit)
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

			if(count) translation_stats.total_translation_latency += charged_tlb_latency;

			// We progress the time by the charged TLB latency so that the PTW starts after the TLB latency
			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time + charged_tlb_latency);
#ifdef DEBUG_MMU
			log_file << "[MMU] New time after charging TLB latency: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
#endif
		}

		bool range_hit = false;
		SubsecondTime range_latency = SubsecondTime::Zero();

		if (!tlb_hit)
		{
			auto range_walk_result = performRangeWalk(address, eip, lock, modeled, count);
			range_latency = range_lb->get_latency().getLatency();	

			if (get<1>(range_walk_result) != static_cast<IntPtr>(-1))
			{
				range_hit = true;
				IntPtr vpn_start = get<1>(range_walk_result)/4096;
				IntPtr ppn_offset = get<2>(range_walk_result);

				IntPtr current_vpn = address >> 12;
				
				ppn_result = (current_vpn - vpn_start) + ppn_offset;
				page_size = 12;
#ifdef DEBUG_MMU
				log_file << "[MMU] Range Hit: " << range_hit << " at time: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
				log_file << "[MMU] VPN Start: " << vpn_start << std::endl;
				log_file << "[MMU] PPN Offset: " << ppn_offset << std::endl;
				log_file << "[MMU] Current VPN: " << current_vpn << std::endl;
				log_file << "[MMU] Final PPN: " << (current_vpn - vpn_start) + ppn_offset << std::endl;
#endif
				if (count)
				{
					translation_stats.requests_resolved_by_rlb++;
					translation_stats.requests_resolved_by_rlb_latency += range_latency;
					translation_stats.total_range_walk_latency += range_latency;
					translation_stats.total_translation_latency += charged_tlb_latency;
				}
				// We progress the time by L1 TLB latency + range latency
				// This is done so that the PTW starts after the TLB latency and the range latency

				shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time + tlb_latency[0] + range_latency); 
#ifdef DEBUG_MMU
				log_file << "[MMU] New time after charging TLB and Range latency: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
#endif
			}
			else{
				if (count)
				{
					translation_stats.total_translation_latency += range_lb->get_latency().getLatency();
				}
#ifdef DEBUG_MMU
				log_file << "[MMU] Range Miss: " << range_hit << " at time: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
#endif
				// We progress the time by the max of the TLB latency and the range latency
				shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time + max(range_latency, charged_tlb_latency));
#ifdef DEBUG_MMU
				log_file << "[MMU] New time after charging TLB and Range latency: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
#endif
			}
		}

		SubsecondTime total_walk_latency = SubsecondTime::Zero();
		SubsecondTime total_fault_latency = SubsecondTime::Zero();

		// We only trigger the PTW if there was a TLB miss
		if (!tlb_hit && !range_hit)
		{
			// Keep track of the time before the PTW starts
			SubsecondTime time_for_pt = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

			// We will occupy an entry in the MSHRs for the PT walker
			struct MSHREntry pt_walker_entry;
			pt_walker_entry.request_time = time_for_pt;

			// The system has N walkers that can be used to perform page table walks in parallel
			// We need to find if there is any delay because of all the walkers being busy
			SubsecondTime delay = pt_walkers->getSlotAllocationDelay(pt_walker_entry.request_time);

			// We switch the time to the time when the PT walker is allocated so that we start the PTW at that time
			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time_for_pt + delay);
#ifdef DEBUG_MMU
			log_file << "[MMU] New time after charging the PT walker allocation delay: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
#endif

			// returns PTW latency, PF latency, Physical Address, Page Size as a tuple
			int app_id = core->getThread()->getAppId();
			PageTable *page_table = Sim()->getMimicOS()->getPageTable(app_id);

			const bool restart_walk_upon_page_fault = false;

			auto ptw_result = performPTW(address, modeled, count, false, eip, lock, page_table, restart_walk_upon_page_fault);
			total_walk_latency = get<0>(ptw_result); // Total walk latency is only the time it takes to walk the page table (excluding page faults)

			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time_for_pt + delay + total_walk_latency);

			if (count)
			{
				translation_stats.total_walk_latency += total_walk_latency;
				translation_stats.page_table_walks++;
			}

			// If the page fault latency is greater than zero, we need to charge the page fault latency
			bool caused_page_fault = get<1>(ptw_result);
			SubsecondTime restartedWalkLatency = SubsecondTime::Zero();

			// If the PTW caused a page fault, we need to charge the page fault latency and restart the walk
			if (caused_page_fault)
			{
				int frames = Sim()->getMimicOS()->getPageTable(app_id)->getMaxLevel();
				Sim()->getMimicOS()->handle_page_fault(address, app_id, frames);
#ifdef DEBUG_MMU
				log_file << "[RangeMMU] Page Fault has occured" << std::endl;
#endif
				SubsecondTime m_page_fault_latency = Sim()->getMimicOS()->getPageFaultLatency();

#ifdef DEBUG_MMU
				log_file << "[RangeMMU] Charging Page Fault Latency: " << m_page_fault_latency << std::endl;
#endif
				total_fault_latency = m_page_fault_latency;

#ifdef DEBUG_MMU
				log_file << "[RangeMMU] Restarting the PTW to resolve address translation" << std::endl;
#endif
				ptw_result = performPTW(address, modeled, count, false, eip, lock, page_table, restart_walk_upon_page_fault);
				restartedWalkLatency = get<0>(ptw_result);

				if (count)
				{
					translation_stats.total_walk_latency += restartedWalkLatency;
					translation_stats.page_faults++;
					translation_stats.total_fault_latency += m_page_fault_latency;
				}
			}

			/*
			We need to set the completion time:
			1) Time before PTW starts
			2) Delay because of all the walkers being busy
			3) Total walk latency
			4) Total fault latency
			*/
#ifdef DEBUG_MMU
			log_file << "[MMU] Charging PT Walker Completion Time: " << time_for_pt + delay + total_walk_latency + total_fault_latency + restartedWalkLatency << std::endl;
#endif
			pt_walker_entry.completion_time = time_for_pt + delay + total_walk_latency + total_fault_latency + restartedWalkLatency;
			pt_walkers->allocate(pt_walker_entry);

			/*
			We need to set the time to the time after the PTW is completed.
			This is done so that the memory manager sends the request to the cache hierarchy after the PTW is completed
			*/

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

			ppn_result = get<2>(ptw_result);
			page_size = get<3>(ptw_result);
#ifdef DEBUG_MMU
			log_file << "[RangeMMU] Final PPN: " << ppn_result << std::endl;
			log_file << "[RangeMMU] Final Page Size: " << page_size << std::endl;
#endif

#ifdef DEBUG_MMU
			log_file << "[MMU] New time after charging the PT walker completion time: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
#endif
		}

		// instruction path: follows the instruction TLB path
		// data path: follows the data TLB path
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
				
				if (tlbs[i][j]->supportsPageSize(page_size) && tlbs[i][j]->getAllocateOnMiss() && (!tlb_hit || (tlb_hit && hit_level > i)))
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

		IntPtr base_page_size = 4096;
		IntPtr physical_address = ppn_result * base_page_size + (address % page_size);

		return physical_address;
	}
	/*
	 * filterPTWResult(...):
	 *   - A helper function that "filters" out PTW accesses that are served by the Page Walk Cache (PWC),
	 *     removing them from the list of actual memory accesses needed for the page table walk.
	 *   - If m_pwc_enabled is true, it checks each PT walk-level address. If found in PWC, we skip it.
	 *   - This can reduce the number of memory accesses needed by the final PTW result.
	 */
	PTWResult RangeMMU::filterPTWResult(PTWResult ptw_result,
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

	std::tuple<SubsecondTime, IntPtr, int> RangeMMU::performRangeWalk(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count)
	{

		SubsecondTime time = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

		SubsecondTime charged_range_walk_latency = SubsecondTime::Zero();
		auto hit_rlb = range_lb->access(Core::mem_op_t::READ, address, count);

		RangeTable *range_table = Sim()->getMimicOS()->getRangeTable(core->getThread()->getAppId());

		if (!hit_rlb.first) // Miss in RLB
		{
#ifdef DEBUG_MMU
			log_file << "Miss in RLB for address: " << address << std::endl;
#endif
			// Check if the address is in the range table
			auto result = range_table->lookup(address);
			if (get<0>(result) != NULL) // TreeNode* is not NULL
			{
				// We found the key inside the range table
#ifdef DEBUG_MMU
				log_file << "Key found for address: " << address << " in the range table" << std::endl;
#endif
				Range range;
				range.vpn = get<0>(result)->keys[get<1>(result)].first;
				range.bounds = get<0>(result)->keys[get<1>(result)].second;
				range.offset = get<0>(result)->values[get<1>(result)].offset;

#ifdef DEBUG_MMU
				log_file << "VPN: " << range.vpn << " Bounds: " << range.bounds << " Offset: " << range.offset << std::endl;
#endif
				// Insert the entry in the RLB
				for (auto &address : get<2>(result))
				{

					translationPacket packet;
					packet.address = address;
					packet.eip = eip;
					packet.instruction = false;
					packet.lock_signal = lock;
					packet.modeled = modeled;
					packet.count = count;
					packet.type = CacheBlockInfo::block_type_t::RANGE_TABLE;

					charged_range_walk_latency += accessCache(packet, charged_range_walk_latency);
				}
				range_lb->insert_entry(range);
			}
			else // We did not find the key inside the range table
			{
		
#ifdef DEBUG_MMU
				log_file << "No key found for address: " << address << " in the range table" << std::endl;
#endif
				return std::make_tuple(charged_range_walk_latency, -1, -1);
			}
			return std::make_tuple(charged_range_walk_latency, hit_rlb.second.vpn, hit_rlb.second.offset);
		}
		else
		{
#ifdef DEBUG_MMU
			log_file << "Hit in RLB for address: " << address << std::endl;
			log_file << "VPN: " << hit_rlb.second.vpn << " Bounds: " << hit_rlb.second.bounds << " Offset: " << hit_rlb.second.offset << std::endl;
#endif
			return std::make_tuple(charged_range_walk_latency, hit_rlb.second.vpn, hit_rlb.second.offset);
		}
	}

	void RangeMMU::discoverVMAs()
	{
		// We need to discover the VMAs in the application
	}

	VMA RangeMMU::findVMA(IntPtr address)
	{
		int app_id = core->getThread()->getAppId();
		std::vector<VMA> vma_list = Sim()->getMimicOS()->getVMA(app_id);

		for (UInt32 i = 0; i < vma_list.size(); i++)
		{
			if (address >= vma_list[i].getBase() && address < vma_list[i].getEnd())
			{
#ifdef DEBUG_MMU
				log_file << "VMA found for address: " << address << " in VMA: " << vma_list[i].getBase() << " - " << vma_list[i].getEnd() << std::endl;
#endif
				return vma_list[i];
			}
		}
		assert(false);
		return VMA(-1, -1);
	}

} // namespace ParametricDramDirectoryMSI
