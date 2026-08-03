
// #include "mmu_midgard.h"
// #include "memory_manager.h"
// #include "cache_cntlr.h"
// #include "subsecond_time.h"
// #include "fixed_types.h"
// #include "pagetable_factory.h"
// #include "core.h"
// #include "thread.h"
// #include <iostream>
// #include <fstream>
// #include <algorithm>
// #include "rangelb.h"
// #include "rangetable_btree.h"
// #include "rangetable_factory.h"
// #include "simulator.h"
// #include "mimicos.h"
// //#define DEBUG_MMU

// using namespace std;

// namespace ParametricDramDirectoryMSI
// {

// 	MemoryManagementUnitMidgard::MemoryManagementUnitMidgard(Core *_core, MemoryManager *_memory_manager, ShmemPerfModel *_shmem_perf_model, String _name, MemoryManagementUnitBase* _nested_mmu)
// 	: MemoryManagementUnitBase(_core, _memory_manager, _shmem_perf_model, _name, _nested_mmu),
// 		core(_core),
// 		memory_manager(_memory_manager),
// 		shmem_perf_model(_shmem_perf_model)
// 	{
// 		// In Midgard, all these components are used in the frontend
// 		instantiateFrontend();
// 		instantiateBackendPageTableWalker();
// 		instantiateBackendTLBSubsystem();
// 		registerMMUStats();
// 	}

// 	MemoryManagementUnitMidgard::~MemoryManagementUnitMidgard()
// 	{
// 	}
	
// 	void MemoryManagementUnitMidgard::instantiateFrontend()
// 	{
// 		int associativity = Sim()->getCfg()->getInt("perf_model/"+name+"/frontend_l1vlb/assoc");
// 		String type = Sim()->getCfg()->getString("perf_model/"+name+"/frontend_l1vlb/type");
// 		int size = Sim()->getCfg()->getInt("perf_model/"+name+"/frontend_l1vlb/size");
// 		int assoc = Sim()->getCfg()->getInt("perf_model/"+name+"/frontend_l1vlb/assoc");
// 		int page_sizes = Sim()->getCfg()->getInt("perf_model/"+name+"/frontend_l1vlb/page_size");
// 	 	int *page_size_list = (int *)malloc(sizeof(int) * (page_sizes));
// 		bool allocate_on_miss = Sim()->getCfg()->getBool("perf_model/"+name+"/frontend_l1vlb/allocate_on_miss");
// 		ComponentLatency latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/"+name+"/frontend_l1vlb/access_latency"));

// 		for (int i = 0; i < page_sizes; i++)
// 			page_size_list[i] = Sim()->getCfg()->getIntArray("perf_model/"+name+"/frontend_l1vlb/page_size_list", i);

// 		frontend_l1vlb = new TLB("frontendl1vlb", "perf_model/"+name+"/frontend_l1vlb", core->getId(), latency, size, assoc, page_size_list, page_sizes, type, allocate_on_miss);
						
//         int num_sets = Sim()->getCfg()->getInt("perf_model/" + name + "/frontend_l2vlb/num_sets");
// 		ComponentLatency latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/" + name + "/frontend_l2vlb/latency"));
// 		frontend_l2vlb = new RLB(core, "frontend_l2vlb", latency, num_sets);
// 	}



// 	void MemoryManagementUnitMidgard::instantiatePageTable()
// 	{
// 		String page_table_type = Sim()->getCfg()->getString("perf_model/"+name+"/page_table_type");
// 		String page_table_name = Sim()->getCfg()->getString("perf_model/"+name+"/page_table_name");
// 		page_table = PageTableFactory::createPageTable(page_table_type, page_table_name, core);
// 	}

// 	void MemoryManagementUnitMidgard::instantiateTLBSubsystem()
// 	{
// 		tlb_subsystem = new TLBHierarchy("tlb_subsystem", core, memory_manager, shmem_perf_model);
// 	}
// 	void MemoryManagementUnitMidgard::registerMMUStats()
// 	{
// 		bzero(&translation_stats, sizeof(translation_stats));

// 		// Statistics for the whole MMU

// 		registerStatsMetric(name, core->getId(), "page_faults", &translation_stats.page_faults);
// 		registerStatsMetric(name, core->getId(), "total_table_walk_latency", &translation_stats.total_walk_latency);
// 		registerStatsMetric(name, core->getId(), "total_fault_latency", &translation_stats.total_fault_latency);
// 		registerStatsMetric(name, core->getId(), "total_tlb_latency", &translation_stats.total_tlb_latency);
// 		registerStatsMetric(name, core->getId(), "num_frontend_translations", &translation_stats.num_frontend_translations);
// 		registerStatsMetric(name, core->getId(), "num_backend_translations", &translation_stats.num_backend_translations);
// 		registerStatsMetric(name, core->getId(), "frontend_latency", &translation_stats.frontend_latency);
// 		registerStatsMetric(name, core->getId(), "backend_latency", &translation_stats.backend_latency);
// 		registerStatsMetric(name, core->getId(), "frontend_memory_accesses", &translation_stats.num_frontend_memory_accesses);

// 		// Statistics for TLB subsystem
// 		translation_stats.tlb_latency_per_level = new SubsecondTime[tlb_subsystem->getTLBSubsystem().size()];
// 		std::cout << "TLB Subsystem Size: " << tlb_subsystem->getTLBSubsystem().size() << std::endl;
// 		for (int i = 0; i < tlb_subsystem->getTLBSubsystem().size(); i++)
// 			registerStatsMetric(name, core->getId(), "tlb_latency_" + itostr(i), &translation_stats.tlb_latency_per_level[i]);
// 	}
// 	pair<SubsecondTime, IntPtr> MemoryManagementUnitMidgard::performAddressTranslationFrontend(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count)
// 	{

// 		SubsecondTime range_walk_latency = SubsecondTime::Zero();

// 		if (translation_stats.num_frontend_translations == 0)
// 		{
// 			discoverVMAs();
// 		}

// 		if (core->getInstructionCount() % 1000000 == 0)
// 		{
// 			discoverVMAs();
// 		}

// 		if (count)
// 			translation_stats.num_frontend_translations++;

// #ifdef DEBUG_MMU
// 		std::cout << "Performing Address Translation Frontend: " << address << std::endl;
// #endif

// 		// Check if the address is in the frontend TLB

// 		auto result_l1vlb = frontend_l1vlb->lookup(address, shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD), count, lock, eip, modeled, count, page_table);

// 		if (result_l1vlb != NULL)
// 		{
		
// #ifdef DEBUG_MMU
// 			std::cout << "L1 VLB Hit" << std::endl;
// #endif
// 			translation_stats.frontend_latency += frontend_l1vlb->getLatency();
// 			translation_stats.total_translation_latency += frontend_l1vlb->getLatency();
// 			return std::make_pair(frontend_l1vlb->getLatency(), result_l1vlb->getPPN() * 4096 + address % 4096);
// 		}
// 		else
// 		{
// #ifdef DEBUG_MMU
// 			std::cout << "L1 VLB Miss" << std::endl;
// #endif
// 			//allocate the entry in the l1vlb 
// 			frontend_l1vlb->allocate(address, shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD), count, lock, 12, address / 4096);
// 			translation_stats.frontend_latency += frontend_l1vlb->getLatency();
// 			translation_stats.total_translation_latency += frontend_l1vlb->getLatency();
// 		}

// 		auto result = rlb->access(Core::mem_op_t::READ, address, count);

// 		if (result.first == true)
// 		{
// #ifdef DEBUG_MMU
// 			std::cout << "RLB Hit" << std::endl;
// #endif
// 			translation_stats.frontend_latency += rlb->get_latency().getLatency();
// 			translation_stats.total_translation_latency += rlb->get_latency().getLatency();

// 			return std::make_pair(rlb->get_latency().getLatency()+frontend_l1vlb->getLatency(), address);
// 		}
// 		else
// 		{
// 			// perform range walk by searching in the vmas
// #ifdef DEBUG_MMU
// 			std::cout << "RLB Miss" << std::endl;
// #endif
// 			for (auto &pair : vmas)
// 			{
// 				if (address >= pair.second.vbase && address <= pair.second.vend)
// 				{
// 					Range new_rng;
// 					new_rng.vpn = pair.second.vbase;
// 					new_rng.bounds = pair.second.vend;
// 					new_rng.offset = 0;
// 					rlb->insert_entry(new_rng);
// 					break;
// 				}
// 			}

// 			std::tuple<TreeNode *, int, std::vector<IntPtr>> result = range_table->lookup(address);

// #ifdef DEBUG_MMU
// 			std::cout << "Range Table Lookup Result: " << std::get<0>(result)->keys->first << " - " << std::get<0>(result)->keys->second << std::endl;
// #endif
// 			for (const auto &address : get<2>(result))
// 			{
// 				translationPacket packet;
// 				packet.address = address;
// 				packet.eip = eip;
// 				packet.instruction = false;
// 				packet.lock_signal = lock;
// 				packet.modeled = modeled;
// 				packet.count = count;
// 				packet.type = CacheBlockInfo::block_type_t::RANGE_TABLE;

// 				range_walk_latency += accessCache(packet);
// 				translation_stats.num_frontend_memory_accesses++;
// 			}
// 			translation_stats.frontend_latency += rlb->get_latency().getLatency() + range_walk_latency;
// 			translation_stats.total_translation_latency += rlb->get_latency().getLatency() + range_walk_latency;
// 			return std::make_pair(rlb->get_latency().getLatency() + range_walk_latency, address);
// 		}
// 	}
// 	pair<SubsecondTime, IntPtr> MemoryManagementUnitMidgard::performAddressTranslationBackend(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count)
// 	{
// 		if (count)
// 			translation_stats.num_backend_translations++;

// #ifdef DEBUG_MMU
// 		std::cout << "Starting address translation for virtual address: " << address << std::endl;
// #endif

// 		TLBSubsystem tlbs = tlb_subsystem->getTLBSubsystem();
// 		SubsecondTime time = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

// 		// metadata run
// 		SubsecondTime metadata_table_walk_latency = SubsecondTime::Zero();

// 		// std::cout << "Metadata table walk latency: " << metadata_table_walk_latency << std::endl;

// 		// metadata run end
// 		bool hit = false;
// 		TLB *hit_tlb = NULL;
// 		CacheBlockInfo *tlb_block_info = NULL;
// 		int hit_level = -1;

// 		int page_size;
// 		// TLB Access for all TLBs
// 		for (int i = 0; i < tlbs.size(); i++)
// 		{
// 			// std::cout << "TLB Level: " << i << std::endl;
// 			for (int j = 0; j < tlbs[i].size(); j++)
// 			{
// 				// std::cout << "TLB Index: " << j << std::endl;
// 				bool tlb_stores_instructions = (tlbs[i][j]->getType() == TLBtype::Instruction) || (tlbs[i][j]->getType() == TLBtype::Unified);

// 				if (tlb_stores_instructions && instruction)
// 				{
// 					tlb_block_info = tlbs[i][j]->lookup(address, time, count, lock, eip, modeled, count, page_table);
// 					if (tlb_block_info != NULL)
// 					{
// 						hit_tlb = tlbs[i][j];
// 						hit_level = i;
// 						hit = true;
// 						goto HIT;
// 					}
// 				}
// 				else if (!instruction)
// 				{
// 					bool tlb_stores_data = !(tlbs[i][j]->getType() == TLBtype::Instruction);
// 					if (tlb_stores_data)
// 					{
// 						tlb_block_info = tlbs[i][j]->lookup(address, time, count, lock, eip, modeled, count, page_table);
// 						if (tlb_block_info != NULL)
// 						{
// 							hit_tlb = tlbs[i][j];
// 							hit_level = i;
// 							hit = true;
// 							goto HIT;
// 						}
// 					}
// 				}
// 			}
// 		}

// 	HIT:

// #ifdef DEBUG_MMU
// 		std::cout << "TLB Hit ? " << hit << " at level: " << hit_level << std::endl;
// #endif
// 		SubsecondTime charged_tlb_latency = SubsecondTime::Zero();

// 		if (hit)
// 		{
// 			if (instruction)
// 				tlbs = tlb_subsystem->getInstructionPath();
// 			else
// 				tlbs = tlb_subsystem->getDataPath();

// 			SubsecondTime tlb_latency[hit_level + 1];

// 			for (int i = 0; i < hit_level; i++)
// 			{
// 				for (int j = 0; j < tlbs[i].size(); j++)
// 				{
// 					tlb_latency[i] = max(tlbs[i][j]->getLatency(), tlb_latency[i]);
// 				}
// #ifdef DEBUG_MMU
// 				std::cout << "Charging TLB Latency: " << tlb_latency[i] << " at level: " << i << std::endl;
// #endif
// 				translation_stats.total_tlb_latency += tlb_latency[i];
// 				translation_stats.tlb_latency_per_level[i] += tlb_latency[i];
// 				charged_tlb_latency += tlb_latency[i];
// 			}

// 			for (int j = 0; j < tlbs[hit_level].size(); j++)
// 			{
// 				if (tlbs[hit_level][j] == hit_tlb)
// 				{
// 					translation_stats.total_tlb_latency += hit_tlb->getLatency();
// 					charged_tlb_latency += hit_tlb->getLatency();
// 					translation_stats.tlb_latency_per_level[hit_level] += hit_tlb->getLatency();

// #ifdef DEBUG_MMU
// 					std::cout << "Charging TLB Hit Latency: " << hit_tlb->getLatency() << " at level: " << hit_level << std::endl;
// #endif
// 				}
// 			}
// 		}

// #ifdef DEBUG_MMU
// 		std::cout << "We have a TLB miss" << std::endl;
// #endif
// 		SubsecondTime tlb_latency[tlbs.size()];
// 		if (!hit)
// 		{
// 			for (int i = 0; i < tlbs.size(); i++)
// 			{
// 				for (int j = 0; j < tlbs[i].size(); j++)
// 				{
// 					tlb_latency[i] = max(tlbs[i][j]->getLatency(), tlb_latency[i]);
// 				}
// #ifdef DEBUG_MMU
// 				std::cout << "Charging TLB Latency: " << tlb_latency[i] << " at level: " << i << std::endl;
// #endif
// 				translation_stats.total_tlb_latency += tlb_latency[i];
// 				charged_tlb_latency += tlb_latency[i];
// 			}
// 		}

// 		SubsecondTime total_walk_latency = SubsecondTime::Zero();
// 		SubsecondTime total_fault_latency = SubsecondTime::Zero();
// 		IntPtr ppn_result;

// 		if (!hit)
// 		{
// 			auto ptw_result = performPTW(address, modeled, count, false, eip, lock);
// 			total_walk_latency = get<0>(ptw_result);
// 			if (count)
// 			{
// 				translation_stats.total_walk_latency += total_walk_latency;
// 				translation_stats.page_table_walks++;
// 			}
// 			if (get<1>(ptw_result) > SubsecondTime::Zero())
// 			{
// 				if (count)
// 				{
// 					translation_stats.page_faults++;
// 					translation_stats.total_fault_latency += get<1>(ptw_result);
// 				}
// 			}
// 			ppn_result = get<2>(ptw_result);
// 			page_size = get<3>(ptw_result);
// 		}
// 		else
// 		{
// 			page_size = tlb_block_info->getPageSize();
// 			ppn_result = tlb_block_info->getPPN();
// 		}

// 		// TLB Allocations
// 		if (instruction)
// 			tlbs = tlb_subsystem->getInstructionPath();
// 		else
// 			tlbs = tlb_subsystem->getDataPath();

// 		std::map<int, vector<tuple<IntPtr, int>>> evicted_translations;

// 		// We need to allocate the the entry in every "allocate on miss" TLB
// 		int tlb_levels = tlbs.size();

// 		if (tlb_subsystem->isPrefetchEnabled())
// 		{
// 			tlb_levels = tlbs.size() - 1;
// #ifdef DEBUG_MMU
// 			std::cout << "Prefetching is enabled" << std::endl;
// #endif
// 		}

// 		for (int i = 0; i < tlb_levels; i++)
// 		{
// 			// We will check where we need to allocate the page

// 			for (int j = 0; j < tlbs[i].size(); j++)
// 			{
// 				if ((i > 0) && (evicted_translations[i - 1].size() != 0))
// 				{
// 					tuple<bool, IntPtr, int> result;

// #ifdef DEBUG_MMU
// 					std::cout << "There are evicted translations from level: " << i - 1 << std::endl;
// #endif

// 					for (int k = 0; k < evicted_translations[i - 1].size(); k++)
// 					{
// #ifdef DEBUG_MMU
// 						std::cout << "Evicted Translation: " << get<0>(evicted_translations[i - 1][k]) << std::endl;
// #endif
// 						if (tlbs[i][j]->supportsPageSize(page_size))
// 						{
// #ifdef DEBUG_MMU
// 							std::cout << "Allocating evicted entry in TLB: Level = " << i << " Index =  " << j << std::endl;
// #endif

// 							result = tlbs[i][j]->allocate(get<0>(evicted_translations[i - 1][k]), time, count, lock, get<1>(evicted_translations[i - 1][k]), ppn_result);
// 							if (get<0>(result) == true)
// 							{
// 								evicted_translations[i].push_back(make_tuple(get<1>(result), get<2>(result)));
// 							}
// 						}
// 					}
// 				}

// 				if (tlbs[i][j]->supportsPageSize(page_size) && tlbs[i][j]->getAllocateOnMiss() && (!hit || (hit && hit_level > i)))
// 				{
// #ifdef DEBUG_MMU
// 					std::cout << "Allocating in TLB: Level = " << i << " Index = " << j << std::endl;
// #endif
// 					tuple<bool, IntPtr, int> result;

// 					result = tlbs[i][j]->allocate(address, time, count, lock, page_size, ppn_result);
// 					if (get<0>(result) == true)
// 					{
// 						evicted_translations[i].push_back(make_tuple(get<1>(result), get<2>(result)));
// 					}
// 				}
// 			}
// 		}
// 		// Free the evicted translations

// 		// std::cout << "Finished address translation for virtual address: " << std::hex << address << std::endl;
// 		translation_stats.total_translation_latency += charged_tlb_latency + total_walk_latency + total_fault_latency;
// 		int page_size_in_bytes = pow(2, page_size);
// 		IntPtr physical_address = ppn_result * page_size_in_bytes + address % page_size_in_bytes;
// 		translation_stats.backend_latency += charged_tlb_latency + total_walk_latency + total_fault_latency;

// 		// std::cout << "Physical address: " << std::hex << physical_address << std::endl;
// 		return std::make_pair(charged_tlb_latency + total_walk_latency + total_fault_latency, physical_address);
// 	}

// 	void MemoryManagementUnitMidgard::discoverVMAs()
// 	{
// 		std::map<IntPtr, VMA> new_vmas;

// 		String app = Sim()->getCfg()->getString("traceinput/thread_" + itostr(core->getThread()->getId()));
// 		size_t pos = app.rfind(".sift");

// 		// If found, extract the substring without the extension
// 		if (pos != std::string::npos)
// 		{
// 			app = app.substr(0, pos);
// 		}
// 		else
// 		{
// #ifdef DEBUG_MMU
// 			std::cout << "No '.sift' found in the filename!" << std::endl;
// #endif
// 		}
// 		app += ".vma";

// 		std::ifstream inFile(app.c_str());

// 		if (!inFile)
// 		{
// #ifdef DEBUG_MMU
// 			std::cerr << "Unable to open file for reading: " << app.c_str() << std::endl;
// #endif
// 			return; // Return empty map
// 		}

// 		int identifier = 0; // Counter to be used as the key
// 		std::string line;
// 		bool processVMAs = false;

// 		while (std::getline(inFile, line))
// 		{
// 			if (line.find("VMA:") != std::string::npos)
// 			{
// 				if (processVMAs)
// 					break;
// 				// Extract the number of instructions
// 				std::istringstream iss(line.substr(5));

// 				int instructions;
// 				iss >> instructions;
// 				// std::cout << "Number of instructions: " << instructions << std::endl;
// 				//  Decide whether to process the subsequent VMAs based on the number of instructions
// 				if ((translation_stats.num_frontend_translations == 0) || (instructions == core->getInstructionCount()))
// 				{
// 					processVMAs = true;
// 				}
// 			}
// 			else if (processVMAs)
// 			{
// 				size_t hyphenPos = line.find('-');
// 				if (hyphenPos != std::string::npos)
// 				{
// 					std::string startAddress = line.substr(0, hyphenPos);
// 					std::string endAddress = line.substr(hyphenPos + 1, line.find(' ') - hyphenPos - 1);
// 					// std::cout << "Start Address: " << startAddress << std::endl;
// 					// std::cout << "End Address: " << endAddress << std::endl;

// 					new_vmas[identifier].allocated = false;
// 					new_vmas[identifier].physical_ranges = std::vector<Range>();
// 					new_vmas[identifier].vbase = std::strtoull(startAddress.c_str(), nullptr, 16);
// 					new_vmas[identifier].vend = std::strtoull(endAddress.c_str(), nullptr, 16);
// 					identifier++;
// 				}
// 			}
// 		}
// 		if (translation_stats.num_frontend_translations == 0)
// 		{
// 			for (auto new_pair : new_vmas)
// 			{

// 				range_table->insert(make_pair(new_pair.second.vbase, new_pair.second.vend), RangeEntry());
// 			}

// 			vmas = new_vmas;
// 		};

// 		for (auto new_pair : new_vmas)
// 		{
// 			bool exists = false;
// 			for (const auto &orig_pair : vmas)
// 			{
// 				if (new_pair.second.vbase == orig_pair.second.vbase && new_pair.second.vend == orig_pair.second.vend)
// 				{
// 					exists = true;
// 					break;
// 				}
// 			}
// 			if (!exists)
// 			{
// 				IntPtr size = vmas.size();
// 				vmas[size] = new_pair.second;

// 				range_table->insert(make_pair(new_pair.second.vbase, new_pair.second.vend), RangeEntry());
// 			}
// 		}

// // print all elements of vmas
// #ifdef DEBUG_MMU
// 		for (auto &pair : vmas)
// 		{
// 			std::cout << "VMA: " << pair.first << std::endl;
// 			std::cout << "VMA Start Address: " << std::hex << pair.second.vbase << std::endl;
// 			std::cout << "VMA End Address: " << std::hex << pair.second.vend << std::endl;
// 			std::cout << "VMA Allocated: " << pair.second.allocated << std::endl;
// 			std::cout << "VMA Physical Ranges: " << std::endl;
// 			for (const auto &range : pair.second.physical_ranges)
// 			{
// 				std::cout << "Range: " << std::hex << range.vpn << "-" << range.bounds << std::endl;
// 			}
// 		}
// #endif

// 		inFile.close();
// 	}


// }
