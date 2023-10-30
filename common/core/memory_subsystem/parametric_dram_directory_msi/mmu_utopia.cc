
#include "mmu_utopia.h"
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
#include "utopia_cache_template.h"
// #define DEBUG_MMU

using namespace std;

namespace ParametricDramDirectoryMSI
{

	MemoryManagementUnitUtopia::MemoryManagementUnitUtopia(Core *_core, MemoryManager *_memory_manager, ShmemPerfModel *_shmem_perf_model, MemoryManagementUnitBase *_host_mmu) : MemoryManagementUnitBase(_core, _memory_manager, _shmem_perf_model, _host_mmu)
	{
		instantiatePageTable();
		instantiateTLBSubsystem();
		instantiateRestSegWalker();
		registerMMUStats();
	}

	void MemoryManagementUnitUtopia::instantiatePageTable()
	{
		String page_table_type = Sim()->getCfg()->getString("perf_model/mmu/page_table_type");
		String page_table_name = Sim()->getCfg()->getString("perf_model/mmu/page_table_name");
		std::cout << "Core ID: " << core->getId() << std::endl;
		page_table = PageTableFactory::createPageTable(page_table_type, page_table_name, core);
	}

	void MemoryManagementUnitUtopia::instantiateTLBSubsystem()
	{
		tlb_subsystem = new TLBHierarchy("tlb_subsystem", core, memory_manager, shmem_perf_model);
	}
	void MemoryManagementUnitUtopia::instantiateRestSegWalker()
	{

		int permission_cache_size = Sim()->getCfg()->getInt("perf_model/utopia/pcache/size");
		int permission_cache_assoc = Sim()->getCfg()->getInt("perf_model/utopia/pcache/assoc");

		ComponentLatency permission_cache_access_latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/utopia/pcache/access_penalty"));
		ComponentLatency permission_cache_miss_latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/utopia/pcache/miss_penalty"));

		sf_cache = new UtopiaCache("sfcache", "perf_model/utopia/sfcache",
								   core->getId(),
								   64,
								   permission_cache_size,
								   permission_cache_assoc,
								   permission_cache_access_latency,
								   permission_cache_miss_latency);

		int tag_cache_size = Sim()->getCfg()->getInt("perf_model/utopia/tagcache/size");
		int tag_cache_assoc = Sim()->getCfg()->getInt("perf_model/utopia/tagcache/assoc");

		ComponentLatency tag_cache_access_latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/utopia/tagcache/access_penalty"));
		ComponentLatency tag_cache_miss_latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/utopia/tagcache/miss_penalty"));

		tar_cache = new UtopiaCache("tarcache",
									"perf_model/utopia/tarcache",
									core->getId(),
									64,
									tag_cache_size,
									tag_cache_assoc,
									tag_cache_access_latency,
									tag_cache_miss_latency);
	}
	void MemoryManagementUnitUtopia::registerMMUStats()
	{
		bzero(&translation_stats, sizeof(translation_stats));

		// Statistics for the whole MMU

		registerStatsMetric("mmu", core->getId(), "page_faults", &translation_stats.page_faults);
		registerStatsMetric("mmu", core->getId(), "total_table_walk_latency", &translation_stats.total_walk_latency);
		registerStatsMetric("mmu", core->getId(), "total_tlb_latency", &translation_stats.total_tlb_latency);
		registerStatsMetric("mmu", core->getId(), "total_translation_latency", &translation_stats.total_translation_latency);
		registerStatsMetric("mmu", core->getId(), "flextorest_migrations", &translation_stats.flextorest_migrations);
		registerStatsMetric("mmu", core->getId(), "total_rsw_latency", &translation_stats.total_rsw_latency);
		registerStatsMetric("mmu", core->getId(), "data_in_restseg", &translation_stats.data_in_restseg);
		registerStatsMetric("mmu", core->getId(), "tlb_hierarchy_misses", &translation_stats.tlb_hierarchy_misses);
		registerStatsMetric("mmu", core->getId(), "total_rsw_requests", &translation_stats.total_rsw_requests);
		registerStatsMetric("mmu", core->getId(), "requests_affected_by_migration", &translation_stats.requests_affected_by_migration);
		registerStatsMetric("mmu", core->getId(), "migration_stall_cycles", &translation_stats.migration_stall_cycles);
		// Statistics for TLB subsystem
		translation_stats.tlb_latency_per_level = new SubsecondTime[tlb_subsystem->getTLBSubsystem().size()];
		for (int i = 0; i < tlb_subsystem->getTLBSubsystem().size(); i++)
			registerStatsMetric("mmu", core->getId(), "tlb_latency_" + itostr(i), &translation_stats.tlb_latency_per_level[i]);
	}

	pair<SubsecondTime, IntPtr> MemoryManagementUnitUtopia::performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count)
	{
		if (count)
			translation_stats.num_translations++;

		SubsecondTime migration_latency = SubsecondTime::Zero();
		int *page_size_list = page_table->getPageSizes();
		int page_sizes = page_table->getPageSizesCount();
		for (int i = 0; i < page_sizes; i++)
		{
			int vpn = address >> page_size_list[i];
			if (migration_queue.find(vpn) != migration_queue.end())
			{
				if (migration_queue[vpn] > shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD))
				{
					migration_latency = migration_queue[vpn] - shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
					translation_stats.migration_stall_cycles += migration_latency;
					if (count)
						translation_stats.requests_affected_by_migration++;
				}
			}
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
		if (hit == false && count)
			translation_stats.tlb_hierarchy_misses++;

		bool rsw_hit = false;
		SubsecondTime rsw_latency = SubsecondTime::Zero();
		if (!hit) // L1 TLB hit - RSW in parallel with TLB access
		{
#ifdef DEBUG_MMU
			std::cout << "[MMU::Utopia] RSW in parallel with TLB access" << std::endl;
#endif
			std::tuple<bool, int, vector<pair<IntPtr, int>>> rsw_result = RestSegWalk(address, count);

			rsw_hit = get<0>(rsw_result);
			if (count)
				translation_stats.data_in_restseg += rsw_hit;
			page_size = get<1>(rsw_result);

			SubsecondTime total_latency = SubsecondTime::Zero();
			SubsecondTime latency = SubsecondTime::Zero();

			for (int i = 0; i < get<2>(rsw_result).size(); i++)
			{
				translationPacket packet;
				packet.eip = eip;
				packet.address = get<2>(rsw_result)[i].first;
				// std::cout << "RSW Address: " << std::hex << packet.address << std::endl;
				packet.instruction = instruction;
				packet.lock_signal = lock;
				packet.modeled = modeled;
				packet.count = count;
				packet.type = CacheBlockInfo::block_type_t::UTOPIA;

				latency = accessCache(packet);
				total_latency += latency;
			}
			if (get<2>(rsw_result).size() != 0)
				total_latency = total_latency / get<2>(rsw_result).size();

			if (count)
				translation_stats.total_rsw_requests += get<2>(rsw_result).size();
			if (count)
				translation_stats.total_rsw_latency += total_latency;
			rsw_latency = total_latency;
#ifdef DEBUG_MMU
			std::cout << "[MMU::Utopia] RSW latency: " << rsw_latency << std::endl;
#endif
		}

#ifdef DEBUG_MMU
		std::cout
			<< "TLB Hit ? " << hit << " at level: " << hit_level << std::endl;
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

		if (!hit && rsw_hit)
		{
			charged_tlb_latency += rsw_latency;
		}

#ifdef DEBUG_MMU
		std::cout << "We have a TLB miss" << std::endl;
#endif
		SubsecondTime tlb_latency[tlbs.size()];
		if (!hit && !rsw_hit)
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

		SubsecondTime total_walk_latency = SubsecondTime::Zero();
		IntPtr ppn_result;

		if (!hit) // We need to walk the page table
		{
			PTWResult page_table_walk_result = page_table->initializeWalk(address, count);
			ppn_result = get<2>(page_table_walk_result);
			page_size = get<0>(page_table_walk_result);
			accessedAddresses accesses = get<1>(page_table_walk_result);

			int vpn = address >> page_size;
			if (ptw_stats.find(vpn) == ptw_stats.end())
			{
				ptw_stats[vpn] = make_pair(1, 0);
			}
			else
			{
				ptw_stats[vpn].first++;
			}
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
			if (is_page_fault)
			{
				// std::cout << "[MMU::Utopia] It was a page fault " << std::endl;

				Utopia *m_utopia = Sim()->getVirtuOS()->getUtopia();
				for (int i = 0; i < m_utopia->RestSegs; i++)
				{
					RestSeg *restseg = m_utopia->getRestSeg(i);
					if (restseg->getPageSize() == page_size)
					{
#ifdef DEBUG_MMU
						std::cout << "[MMU::Utopia] Page fault - Try to allocate in RestSeg" << std::endl;
#endif
						restseg->allocate(address, SubsecondTime::Zero(), core->getId());
					}
				}
			}

			SubsecondTime table_latency = SubsecondTime::Zero();

			if (!is_page_fault)
			{

				for (int j = 0; j < levels + 1; j++)
				{
					total_walk_latency += latency_per_table_per_level[correct_table][j];
				}
				if (total_walk_latency > ComponentLatency(core->getDvfsDomain(), 50).getLatency())
				{
					ptw_stats[vpn].second++;
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
#ifdef DEBUG_MMU
			std::cout << "[MMU::Utopia] Page PTW stats: " << ptw_stats[vpn].first << " " << ptw_stats[vpn].second << std::endl;
#endif
			if (ptw_stats[vpn].first > 4 && ptw_stats[vpn].second > 3)
			{
#ifdef DEBUG_MMU

				std::cout << "[MMU::Utopia] This page is really costly-to-translate, migrate it to RestSeg " << std::endl;
#endif
				migration_queue[vpn] = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) + ComponentLatency(core->getDvfsDomain(), 200).getLatency();
				if (count)
					translation_stats.flextorest_migrations++;
				Utopia *m_utopia = Sim()->getVirtuOS()->getUtopia();
				for (int i = 0; i < m_utopia->RestSegs; i++)
				{
					RestSeg *restseg = m_utopia->getRestSeg(i);
					if (restseg->getPageSize() == page_size)
					{
						restseg->allocate(address, SubsecondTime::Zero(), core->getId());
					}
				}
			}
		}
		else
		{
			page_size = tlb_block_info->getPageSize();
			ppn_result = tlb_block_info->getPPN();
		}

#ifdef DEBUG_MMU
		std::cout << "Total Walk Latency: " << total_walk_latency << std::endl;
#endif
		if (count)
			translation_stats.total_walk_latency += total_walk_latency;

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

		return make_pair(charged_tlb_latency + total_walk_latency + migration_latency, ppn_result);
	}

	std::tuple<bool, int, vector<pair<IntPtr, int>>> MemoryManagementUnitUtopia::RestSegWalk(IntPtr address, bool count) // Returns true if the address is in the RestSeg, false otherwise, and the addresses which were accessed in the cache hierarchy
	{
		vector<pair<IntPtr, int>> memory_accesses;
		SubsecondTime t_start = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

		Utopia *m_utopia = Sim()->getVirtuOS()->getUtopia();
		RestSeg *restseg;
		bool tag_match_skip = false;
		bool restsegwalk_skip = false;
		int page_size = 0;
		bool restseg_hit = false;
		for (int i = 0; i < m_utopia->RestSegs; i++) // One walk for every RestSeg
		{

			restseg = m_utopia->getRestSeg(i);
			int vpn = address >> restseg->getPageSize();

			if (restseg->inRestSeg(address, count, t_start, core->getId()))
			{
				page_size = restseg->getPageSize();
				restseg_hit = true;
			}

			if (restseg->permission_filter(address, core->getId()))
				tag_match_skip = true; // Skip tag match if the permission filter is larger than 0
			else
				tag_match_skip = false;

			UInt64 permission_filter = restseg->calculate_permission_address(address, core->getId());

			UtopiaCache::where_t permcache_hitwhere;

			permcache_hitwhere = sf_cache->lookup((IntPtr)permission_filter, t_start, true, count); // Lookup SF cache

			if (permcache_hitwhere == UtopiaCache::where_t::MISS)
			{
				memory_accesses.push_back(make_pair(permission_filter, 0));
				// std::cout << "[MMU::Utopia] Set Filter cache miss" << permission_filter << std::endl;
			}

			if (tag_match_skip == false)
			{

				UInt64 tag = restseg->calculate_tag_address(address, core->getId());

				UtopiaCache::where_t tagcache_hitwhere;

				tagcache_hitwhere = tar_cache->lookup((IntPtr)tag, t_start, true, count); // Lookup the TAR cache

				if (tagcache_hitwhere == UtopiaCache::where_t::MISS)
				{
					// std::cout << "[MMU::Utopia] Tag cache miss" << tag << std::endl;
					memory_accesses.push_back(make_pair(tag, 0));
				}
			}
		}
		return make_tuple(restseg_hit, page_size, memory_accesses);
	}

	void MemoryManagementUnitUtopia::discoverVMAs()
	{
	}
}