
#include "mmu_range.h"
#include "memory_manager.h"
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "pagetable_factory.h"
#include "core.h"
#include "thread.h"
#include "mmu_base.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include "rangelb.h"
#include "simulator.h"
#include "physical_memory_allocator.h"
// #define DEBUG_MMU

using namespace std;

namespace ParametricDramDirectoryMSI
{

	RangeMMU::RangeMMU(Core *_core, MemoryManager *_memory_manager, ShmemPerfModel *_shmem_perf_model, MemoryManagementUnitBase *_host_mmu) : MemoryManagementUnitBase(_core, _memory_manager, _shmem_perf_model, _host_mmu)

	{
		instantiateRLB();
		instantiatePageTable();
		instantiateTLBSubsystem();
		registerMMUStats();
	}

	RangeMMU::~RangeMMU()
	{
		delete page_table;
		delete tlb_subsystem;
	}

	void RangeMMU::instantiateRLB()
	{
		int num_sets = Sim()->getCfg()->getInt("perf_model/mmu/range_lb/num_sets");
		ComponentLatency latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/mmu/range_lb/latency"));
		range_lb = new RLB(core, "range_lb", latency, num_sets);
	}

	void RangeMMU::instantiatePageTable()
	{
		String page_table_type = Sim()->getCfg()->getString("perf_model/mmu/page_table_type");
		String page_table_name = Sim()->getCfg()->getString("perf_model/mmu/page_table_name");
		page_table = PageTableFactory::createPageTable(page_table_type, page_table_name, core);
	}

	void RangeMMU::instantiateTLBSubsystem()
	{
		tlb_subsystem = new TLBHierarchy("tlb_subsystem", core, memory_manager, shmem_perf_model);
	}
	void RangeMMU::registerMMUStats()
	{
		bzero(&translation_stats, sizeof(translation_stats));

		// Statistics for the whole MMU

		registerStatsMetric("mmu", core->getId(), "page_faults", &translation_stats.page_faults);
		registerStatsMetric("mmu", core->getId(), "total_table_walk_latency", &translation_stats.total_walk_latency);
		registerStatsMetric("mmu", core->getId(), "total_tlb_latency", &translation_stats.total_tlb_latency);
		registerStatsMetric("mmu", core->getId(), "total_translation_latency", &translation_stats.total_translation_latency);

		translation_stats.tlb_latency_per_level = new SubsecondTime[tlb_subsystem->getTLBSubsystem().size()];
		for (int i = 0; i < tlb_subsystem->getTLBSubsystem().size(); i++)
			registerStatsMetric("mmu", core->getId(), "tlb_latency_" + itostr(i), &translation_stats.tlb_latency_per_level[i]);
	}

	pair<SubsecondTime, IntPtr> RangeMMU::performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count)
	{
		if (translation_stats.num_translations == 0)
		{
			discoverVMAs();
		}

		if (count)
			translation_stats.num_translations++;

		if (core->getInstructionCount() % 10000 == 0)
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
					translation_stats.tlb_latency_per_level[hit_level] += hit_tlb->getLatency();
					charged_tlb_latency += hit_tlb->getLatency();
#ifdef DEBUG_MMU
					std::cout << "Charging TLB Hit Latency: " << hit_tlb->getLatency() << " at level: " << hit_level << std::endl;
#endif
				}
			}
		}
		IntPtr ppn_result;
// RLB Access in parallel with L2 TLB
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

		SubsecondTime rlb_access_latency = SubsecondTime::Zero();
		if (!hit)
		{
			auto hit_rlb = range_lb->access(Core::mem_op_t::READ, address, count);

			if (!hit_rlb.first)
			{
				// find the key of vmas that address belongs to
				int key = -1;

				for (auto &pair : vmas)
				{
					if (address >= pair.second.vbase && address <= pair.second.vend)
					{
						if (pair.second.allocated)
						{
							for (const auto &range : pair.second.physical_ranges)
							{
								if (address >= range.vpn && address <= range.bounds)
								{
									range_lb->insert_entry(range);
									break;
								}
							}
						}
						else
						{
							int size = (pair.second.vend - pair.second.vbase);
							int pages = size / 4096;

							std::vector<Range> m_ranges;
							m_ranges = Sim()->getVirtuOS()->getMemoryAllocator()->allocate_eager_paging(size);

							int running_offset = 0;

							for (int i = 0; i < m_ranges.size(); i++)
							{
								if (m_ranges[i].bounds > pages)
									m_ranges[i].bounds = pages;
								Range range;
								range.vpn = pair.second.vbase + running_offset;
								range.bounds = range.vpn + m_ranges[i].bounds * 4096;
								range.offset = m_ranges[i].vpn * 4096;

								if (address >= range.vpn && address <= range.bounds)
									range_lb->insert_entry(range);

								pair.second.physical_ranges.push_back(range);
								running_offset += m_ranges[i].bounds * 4096;
							}

							pair.second.allocated = true;

							break;
						}
					}
				}
				charged_tlb_latency = max(charged_tlb_latency, range_lb->get_latency().getLatency());
			}
			else if (hit_rlb.first)
			{
				IntPtr physical_address = address - hit_rlb.second.vpn + hit_rlb.second.offset;
				ppn_result = physical_address;

				charged_tlb_latency = range_lb->get_latency().getLatency();
			}
		}

		SubsecondTime total_walk_latency = SubsecondTime::Zero();

		if (!hit)
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

		translation_stats.total_translation_latency += charged_tlb_latency + total_walk_latency;
		return make_pair(charged_tlb_latency + total_walk_latency, ppn_result);
	}

	SubsecondTime RangeMMU::accessCache(translationPacket packet)
	{
		SubsecondTime t_start = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
		IntPtr cache_address = ((IntPtr)(packet.address)) & (~((64 - 1)));
		CacheCntlr *l1d_cache = memory_manager->getCacheCntlrAt(core->getId(), MemComponent::component_t::L1_DCACHE);
		l1d_cache->processMemOpFromCore(
			packet.eip,
			packet.lock_signal,
			Core::mem_op_t::READ,
			cache_address, 0,
			NULL, 8,
			packet.modeled,
			packet.count, CacheBlockInfo::block_type_t::PAGE_TABLE, SubsecondTime::Zero());

		SubsecondTime t_end = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

		shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, t_start);
// memory_manager->tagCachesBlockType(packet.address,CacheBlockInfo::block_type_t::PAGE_TABLE);
#ifdef DEBUG_MMU
		std::cout << "Accessing Cache for PTW - Latency is:  " << t_end - t_start << std::endl;
#endif

		return t_end - t_start;
	}

	void RangeMMU::discoverVMAs()
	{
		std::map<IntPtr, VMA> new_vmas;

		String app = Sim()->getCfg()->getString("traceinput/thread_" + itostr(core->getThread()->getId()));
		size_t pos = app.rfind(".sift");

		// If found, extract the substring without the extension
		if (pos != std::string::npos)
		{
			app = app.substr(0, pos);
		}
		else
		{
#ifdef DEBUG_MMU
			std::cout << "No '.sift' found in the filename!" << std::endl;
#endif
		}
		app += ".vma";

		std::ifstream inFile(app.c_str());

		if (!inFile)
		{
#ifdef DEBUG_MMU
			std::cerr << "Unable to open file for reading: " << app.c_str() << std::endl;
#endif
			return; // Return empty map
		}

		int identifier = 0; // Counter to be used as the key
		std::string line;
		bool processVMAs = false;

		while (std::getline(inFile, line))
		{
			if (line.find("VMA:") != std::string::npos)
			{
				if (processVMAs)
					break;
				// Extract the number of instructions
				std::istringstream iss(line.substr(5));

				int instructions;
				iss >> instructions;
				// std::cout << "Number of instructions: " << instructions << std::endl;
				//  Decide whether to process the subsequent VMAs based on the number of instructions
				if ((translation_stats.num_translations == 0) || (instructions == core->getInstructionCount()))
				{
					processVMAs = true;
				}
			}
			else if (processVMAs)
			{
				size_t hyphenPos = line.find('-');
				if (hyphenPos != std::string::npos)
				{
					std::string startAddress = line.substr(0, hyphenPos);
					std::string endAddress = line.substr(hyphenPos + 1, line.find(' ') - hyphenPos - 1);
					// std::cout << "Start Address: " << startAddress << std::endl;
					// std::cout << "End Address: " << endAddress << std::endl;

					new_vmas[identifier].allocated = false;
					new_vmas[identifier].physical_ranges = std::vector<Range>();
					new_vmas[identifier].vbase = std::strtoull(startAddress.c_str(), nullptr, 16);
					new_vmas[identifier].vend = std::strtoull(endAddress.c_str(), nullptr, 16);
					identifier++;
				}
			}
		}
		if (translation_stats.num_translations == 0)
		{
			vmas = new_vmas;
			return;
		}
		for (auto new_pair : new_vmas)
		{
			bool exists = false;
			for (const auto &orig_pair : vmas)
			{
				if (new_pair.second.vbase == orig_pair.second.vbase && new_pair.second.vend == orig_pair.second.vend)
				{
					exists = true;
					break;
				}
			}
			if (!exists)
			{
				IntPtr size = vmas.size();
				vmas[size] = new_pair.second;
			}
		}

// print all elements of vmas
#ifdef DEBUG_MMU
		for (auto &pair : vmas)
		{
			std::cout << "VMA: " << pair.first << std::endl;
			std::cout << "VMA Start Address: " << std::hex << pair.second.vbase << std::endl;
			std::cout << "VMA End Address: " << std::hex << pair.second.vend << std::endl;
			std::cout << "VMA Allocated: " << pair.second.allocated << std::endl;
			std::cout << "VMA Physical Ranges: " << std::endl;
			for (const auto &range : pair.second.physical_ranges)
			{
				std::cout << "Range: " << std::hex << range.vpn << "-" << range.bounds << std::endl;
			}
		}
#endif

		inFile.close();
	}
}