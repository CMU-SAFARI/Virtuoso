
#include "mmu_pomtlb.h"
#include "mmu_base.h"
#include "memory_manager.h"
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "pagetable_factory.h"
#include "core.h"
#include "thread.h"
#include <iostream>
#include <fstream>
#include <algorithm>

// #define DEBUG_MMU

using namespace std;

namespace ParametricDramDirectoryMSI
{

	MemoryManagementUnitPOMTLB::MemoryManagementUnitPOMTLB(Core *_core, MemoryManager *_memory_manager, ShmemPerfModel *_shmem_perf_model, MemoryManagementUnitBase *_host_mmu) : MemoryManagementUnitBase(_core, _memory_manager, _shmem_perf_model, _host_mmu)
	{
		instantiatePageTable();
		instantiateTLBSubsystem();
		registerMMUStats();
	}

	void MemoryManagementUnitPOMTLB::instantiatePageTable()
	{
		String page_table_type = Sim()->getCfg()->getString("perf_model/mmu/page_table_type");
		String page_table_name = Sim()->getCfg()->getString("perf_model/mmu/page_table_name");
		std::cout << "Core ID: " << core->getId() << std::endl;
		page_table = PageTableFactory::createPageTable(page_table_type, page_table_name, core);
	}

	void MemoryManagementUnitPOMTLB::instantiateTLBSubsystem()
	{
		tlb_subsystem = new TLBHierarchy("tlb_subsystem", core, memory_manager, shmem_perf_model);
		String type = Sim()->getCfg()->getString("perf_model/pom_tlb/type");
		int size = Sim()->getCfg()->getInt("perf_model/pom_tlb/size");
		int assoc = Sim()->getCfg()->getInt("perf_model/pom_tlb/assoc");
		page_sizes = Sim()->getCfg()->getInt("perf_model/pom_tlb/page_size");
		page_size_list = (int *)malloc(sizeof(int) * (page_sizes));
		bool allocate_on_miss = Sim()->getCfg()->getBool("perf_model/pom_tlb/allocate_on_miss");
		m_size = size;
		m_associativity = assoc;
		software_tlb = (char *)malloc(size * (8 + 8)); // 8 bytes for VPN and 8 bytes for PPN

		m_pom_tlb = new TLB("pom_tlb", "perf_model/pom_tlb", core->getId(), ComponentLatency(core->getDvfsDomain(), 0), size, assoc, page_size_list, page_sizes, type, allocate_on_miss);
	}
	void MemoryManagementUnitPOMTLB::registerMMUStats()
	{
		bzero(&translation_stats, sizeof(translation_stats));

		// Statistics for the whole MMU

		registerStatsMetric("mmu", core->getId(), "page_faults", &translation_stats.page_faults);
		registerStatsMetric("mmu", core->getId(), "total_table_walk_latency", &translation_stats.total_walk_latency);
		registerStatsMetric("mmu", core->getId(), "total_tlb_latency", &translation_stats.total_tlb_latency);
		registerStatsMetric("mmu", core->getId(), "total_translation_latency", &translation_stats.total_translation_latency);
		registerStatsMetric("mmu", core->getId(), "total_software_tlb_latency", &translation_stats.software_tlb_latency);

		// Statistics for TLB subsystem
		translation_stats.tlb_latency_per_level = new SubsecondTime[tlb_subsystem->getTLBSubsystem().size()];
		for (int i = 0; i < tlb_subsystem->getTLBSubsystem().size(); i++)
			registerStatsMetric("mmu", core->getId(), "tlb_latency_" + itostr(i), &translation_stats.tlb_latency_per_level[i]);
	}

	pair<SubsecondTime, IntPtr> MemoryManagementUnitPOMTLB::performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count)
	{
		if (count)
			translation_stats.num_translations++;

		if (translation_stats.num_translations % 100000 == 0)
		{
			discoverVMAs();
		}
		TLBSubsystem tlbs = tlb_subsystem->getTLBSubsystem();
		SubsecondTime time = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

		bool hit = false;
		TLB *hit_tlb = NULL;
		CacheBlockInfo *tlb_block_info = NULL;
		int hit_level = -1;

		int page_size;
		// TLB Access for all TLBs
		for (int i = 0; i < tlbs.size(); i++)
		{
			for (int j = 0; j < tlbs[i].size(); j++)
			{
				bool tlb_stores_instructions = (tlbs[i][j]->getType() == TLBtype::Instruction) || (tlbs[i][j]->getType() == TLBtype::Unified);

				if (tlb_stores_instructions && instruction)
				{
					tlb_block_info = tlbs[i][j]->lookup(address, time, count, lock, eip, modeled, count, page_table);
					if (tlb_block_info != NULL)
					{
						hit_tlb = tlbs[i][j];
						hit_level = i;
						hit = true;
						goto HIT;
					}
				}
				else if (!instruction)
				{
					bool tlb_stores_data = !(tlbs[i][j]->getType() == TLBtype::Instruction);
					if (tlb_stores_data)
					{
						tlb_block_info = tlbs[i][j]->lookup(address, time, count, lock, eip, modeled, count, page_table);
						if (tlb_block_info != NULL)
						{
							hit_tlb = tlbs[i][j];
							hit_level = i;
							hit = true;
							goto HIT;
						}
					}
				}
			}
		}

	HIT:

#ifdef DEBUG_MMU
		std::cout << "TLB Hit ? " << hit << " at level: " << hit_level << std::endl;
#endif
		SubsecondTime charged_tlb_latency = SubsecondTime::Zero();

		if (hit)
		{
			if (instruction)
				tlbs = tlb_subsystem->getInstructionPath();
			else
				tlbs = tlb_subsystem->getDataPath();

			SubsecondTime tlb_latency[hit_level + 1];

			for (int i = 0; i < hit_level; i++)
			{
				for (int j = 0; j < tlbs[i].size(); j++)
				{
					tlb_latency[i] = max(tlbs[i][j]->getLatency(), tlb_latency[i]);
				}
#ifdef DEBUG_MMU
				std::cout << "Charging TLB Latency: " << tlb_latency[i] << " at level: " << i << std::endl;
#endif
				translation_stats.total_tlb_latency += tlb_latency[i];
				translation_stats.tlb_latency_per_level[i] += tlb_latency[i];
				charged_tlb_latency += tlb_latency[i];
			}

			for (int j = 0; j < tlbs[hit_level].size(); j++)
			{
				if (tlbs[hit_level][j] == hit_tlb)
				{
					translation_stats.total_tlb_latency += hit_tlb->getLatency();
					charged_tlb_latency += hit_tlb->getLatency();
					translation_stats.tlb_latency_per_level[hit_level] += hit_tlb->getLatency();

#ifdef DEBUG_MMU
					std::cout << "Charging TLB Hit Latency: " << hit_tlb->getLatency() << " at level: " << hit_level << std::endl;
#endif
				}
			}
		}

#ifdef DEBUG_MMU
		std::cout << "We have a TLB miss" << std::endl;
#endif
		SubsecondTime tlb_latency[tlbs.size()];
		if (!hit)
		{
			for (int i = 0; i < tlbs.size(); i++)
			{
				for (int j = 0; j < tlbs[i].size(); j++)
				{
					tlb_latency[i] = max(tlbs[i][j]->getLatency(), tlb_latency[i]);
				}
#ifdef DEBUG_MMU
				std::cout << "Charging TLB Latency: " << tlb_latency[i] << " at level: " << i << std::endl;
#endif
				translation_stats.total_tlb_latency += tlb_latency[i];
				charged_tlb_latency += tlb_latency[i];
			}
		}

		IntPtr ppn_result;

		// Access to software-managed TLB - if we miss, we need to perform the page table walk
		SubsecondTime software_tlb_latency = SubsecondTime::Zero();
		SubsecondTime total_walk_latency = SubsecondTime::Zero();

		if (!hit)
		{

			TLB *potm = m_pom_tlb;
			CacheBlockInfo *hit = NULL;
			hit = potm->lookup(address, time, count, lock, eip, modeled, count, page_table); // @kanellok @tlb_address_access

			IntPtr tlb_address_table[page_sizes];

			for (int i = 0; i < page_sizes; i++)
			{
				tlb_address_table[i] = (IntPtr)software_tlb + ((((address >> (page_size_list[i]))) % m_size) * m_associativity) * 16;
			}

			translationPacket packet;
			packet.eip = eip;
			packet.instruction = instruction;
			packet.lock_signal = lock;
			packet.modeled = modeled;
			packet.count = count;
			packet.type = CacheBlockInfo::block_type_t::TLB_ENTRY;

			SubsecondTime latency[page_sizes];

			if (hit != NULL)
			{
				ppn_result = hit->getPPN();
				IntPtr result_page_size = hit->getPageSize();
				int found_level_id = -1;
				for (int i = 0; i < page_sizes; i++)
				{

					packet.address = tlb_address_table[i];
					latency[i] = accessCache(packet);
					if (page_size_list[i] == result_page_size)
					{
						page_size = page_size_list[i];
						found_level_id = i;
						break;
					}
				}
				translation_stats.software_tlb_latency += latency[found_level_id];
				software_tlb_latency = latency[found_level_id];
			}
			else
			{
				for (int i = 0; i < page_sizes; i++)
				{
					IntPtr cache_address = tlb_address_table[i] & (~((64 - 1)));
					packet.address = cache_address;
					latency[i] = accessCache(packet);
				}
				int max_id = -1;
				SubsecondTime max = SubsecondTime::Zero();
				for (int i = 0; i < page_sizes; i++)
				{
					if (max <= latency[i])
					{
						max_id = i;
						max = latency[i];
					}
				}
				translation_stats.software_tlb_latency += max;
				software_tlb_latency = max;
			}

			if (hit == NULL)
			{
				PTWResult page_table_walk_result = page_table->initializeWalk(address, count);
				ppn_result = get<2>(page_table_walk_result);
				page_size = get<0>(page_table_walk_result);
				accessedAddresses accesses = get<1>(page_table_walk_result);

#ifdef DEBUG_MMU
				std::cout << "[PTW Result] Page_size: " << get<0>(page_table_walk_result) << std::endl;
				std::cout << "[PTW Result] Accessed addresses: " << get<1>(page_table_walk_result).size() << std::endl;

				for (int i = 0; i < accesses.size(); i++)
				{
					std::cout << "[PTW Result] Accessed physical address: " << get<2>(accesses[i]) << std::endl;
				}
#endif

				translationPacket packet;
				packet.eip = eip;
				packet.instruction = instruction;
				packet.lock_signal = lock;
				packet.modeled = modeled;
				packet.count = count;
				packet.type = CacheBlockInfo::block_type_t::PAGE_TABLE;

				SubsecondTime latency = SubsecondTime::Zero();

				int levels = 0;
				int tables = 0;

				for (int i = 0; i < accesses.size(); i++)
				{
					int level = get<1>(accesses[i]);
					int table = get<0>(accesses[i]);
					if (level > levels)
						levels = level;
					if (table > tables)
						tables = table;
				}

				SubsecondTime latency_per_table_per_level[tables + 1][levels + 1] = {SubsecondTime::Zero()};

				bool is_page_fault = true;
				int correct_table = -1;

				for (int i = 0; i < accesses.size(); i++)
				{
#ifdef DEBUG_MMU
					std::cout << "[PTW Accesses] Accessing physical address: " << get<2>(accesses[i]) << " for table " << get<0>(accesses[i]) << " for level: " << get<1>(accesses[i]) << std::endl;
#endif

					packet.address = get<2>(accesses[i]);

					latency = accessCache(packet);

					if (latency_per_table_per_level[get<0>(accesses[i])][get<1>(accesses[i])] < latency)
						latency_per_table_per_level[get<0>(accesses[i])][get<1>(accesses[i])] = latency;

					if (get<3>(accesses[i]) == true)
					{
						is_page_fault = false;
						correct_table = get<0>(accesses[i]);
						break;
					}
				}

				SubsecondTime table_latency = SubsecondTime::Zero();

				if (!is_page_fault)
				{

					for (int j = 0; j < levels + 1; j++)
					{
						total_walk_latency += latency_per_table_per_level[correct_table][j];
					}
				}
				else
				{
#ifdef DEBUG_MMU
					std::cout << "Page Fault " << is_page_fault << std::endl;
#endif
					if (count)
						translation_stats.page_faults++;

					table_latency = SubsecondTime::Zero();
					for (int i = 0; i < tables + 1; i++)
					{
						for (int j = 0; j < levels + 1; j++)
						{
							table_latency += latency_per_table_per_level[i][j];
						}
					}
					total_walk_latency = table_latency;
				}
			}
			if (count)
				translation_stats.total_walk_latency += total_walk_latency;
		}
		else
		{
			page_size = tlb_block_info->getPageSize();
		}

#ifdef DEBUG_MMU
		std::cout << "Total Walk Latency: " << total_walk_latency << std::endl;
#endif

		// TLB Allocations
		if (instruction)
			tlbs = tlb_subsystem->getInstructionPath();
		else
			tlbs = tlb_subsystem->getDataPath();
		std::map<int, vector<tuple<IntPtr, int>>> evicted_translations;

		// We need to allocate the the entry in every "allocate on miss" TLB

		for (int i = 0; i < tlbs.size(); i++)
		{
			// We will check where we need to allocate the page

			for (int j = 0; j < tlbs[i].size(); j++)
			{
				if ((i > 0) && (evicted_translations[i - 1].size() != 0))
				{
					tuple<bool, IntPtr, int> result;

#ifdef DEBUG_MMU
					std::cout << "There are evicted translations from level: " << i - 1 << std::endl;
#endif

					for (int k = 0; k < evicted_translations[i - 1].size(); k++)
					{
#ifdef DEBUG_MMU
						std::cout << "Evicted Translation: " << std::hex << get<0>(evicted_translations[i - 1][k]) << std::endl;
#endif
						if (tlbs[i][j]->supportsPageSize(page_size))
						{
#ifdef DEBUG_MMU
							std::cout << "Allocating evicted entry in TLB: Level = " << i << " Index =  " << j << std::endl;
#endif

							result = tlbs[i][j]->allocate(get<0>(evicted_translations[i - 1][k]), time, count, lock, get<1>(evicted_translations[i - 1][k]), ppn_result);
							if (get<0>(result) == true)
							{
								evicted_translations[i].push_back(make_tuple(get<1>(result), get<2>(result)));
							}
						}
					}
				}

				if (tlbs[i][j]->supportsPageSize(page_size) && tlbs[i][j]->getAllocateOnMiss() && (!hit || (hit && hit_level > i)))
				{
#ifdef DEBUG_MMU
					std::cout << "Allocating in TLB: Level = " << i << " Index = " << j << std::endl;
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
		// allocate tlb in pom
		for (auto pair : evicted_translations)
		{
			if (pair.first > tlbs.size() - 2)
			{
				for (int k = 0; k < evicted_translations[pair.first].size(); k++)
				{
					m_pom_tlb->allocate(get<0>(evicted_translations[pair.first][k]), time, count, lock, page_size, ppn_result);
				}
			}
		}
		return make_pair(charged_tlb_latency + software_tlb_latency + total_walk_latency, ppn_result);
	}

	void MemoryManagementUnitPOMTLB::discoverVMAs()
	{
	}
}