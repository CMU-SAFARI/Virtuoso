// ============================================================================
// MMU Part-of-Memory TLB (POM-TLB) Implementation
// ============================================================================
//
// This file implements an MMU variant with a Part-of-Memory TLB (POM-TLB)
// for the Sniper multi-core simulator. The POM-TLB is a software-managed TLB
// that stores translations in main memory, providing much larger capacity
// than hardware TLBs at the cost of memory access latency.
//
// Reference:
// ~~~~~~~~~~
//   Ryoo et al. "Rethinking TLB designs in virtualized environments:
//   A very large part-of-memory TLB" - ISCA 2017
//   https://ieeexplore.ieee.org/document/8192494
//
// Key Features:
// ~~~~~~~~~~~~~
//   1. Standard hardware TLB hierarchy (L1 iTLB, L1 dTLB, L2 TLB)
//   2. Software-managed TLB stored in main memory (POM-TLB)
//   3. One POM-TLB per supported page size (4KB, 2MB, etc.)
//   4. Falls back to full page table walk only on POM-TLB miss
//
// Architecture Overview:
// ~~~~~~~~~~~~~~~~~~~~~~
//   +--------+     +-----------+     +------------+
//   |  Core  | --> |  MMU POM  | --> |   Memory   |
//   +--------+     +-----------+     +------------+
//                       |
//         +-------------+-------------+
//         |             |             |
//    +----v----+   +----v----+   +----v----+
//    |   TLB   |   | POM-TLB |   |   PTW   |
//    | Subsys  |   | (DRAM)  |   |         |
//    +---------+   +---------+   +---------+
//         |             |             |
//         +------+------+             |
//                |                    |
//         Fast   |                    |  Slow
//         (SRAM) |                    |  (DRAM)
//                v                    v
//
// Translation Flow:
// ~~~~~~~~~~~~~~~~~
//   1. Lookup hardware TLB hierarchy
//   2. On HW TLB miss: Lookup POM-TLB (parallel lookup for each page size)
//   3. On POM-TLB miss: Perform full page table walk
//   4. Allocate translation in both HW TLBs and POM-TLBs
//
// POM-TLB Memory Layout:
// ~~~~~~~~~~~~~~~~~~~~~~
//   - Each POM-TLB is set-associative, stored contiguously in DRAM
//   - Entry size = tag_size + 52 bits (physical address + metadata)
//   - Tag size = 48 - page_offset - log2(num_sets)
//   - Base address stored in software_tlb_base_register[]
//
// ============================================================================

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
#include "misc/exception_handler_base.h"
#include "sniper_space_exception_handler.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include "filter_factory.h"

// #define DEBUG_MMU  // Uncomment for verbose debug logging

using namespace std;

namespace ParametricDramDirectoryMSI
{

// ============================================================================
// Construction / Destruction
// ============================================================================

    /**
     * @brief Construct the POM-TLB MMU.
     *
     * Initializes an MMU with a Part-of-Memory TLB that stores translations
     * in main memory. This provides much larger translation coverage than
     * hardware TLBs, at the cost of DRAM access latency.
     *
     * @param _core           Pointer to the core this MMU belongs to
     * @param _memory_manager Memory manager for cache access
     * @param _shmem_perf_model Performance model for timing
     * @param _name           Configuration name prefix
     * @param _nested_mmu     Optional nested MMU for virtualization
     */
	MemoryManagementUnitPOMTLB::MemoryManagementUnitPOMTLB(Core *_core, 
	                                                       MemoryManagerBase *_memory_manager, 
	                                                       ShmemPerfModel *_shmem_perf_model, 
	                                                       String _name, 
	                                                       MemoryManagementUnitBase *_nested_mmu)
	: MemoryManagementUnitBase(_core, _memory_manager, _shmem_perf_model, _name, _nested_mmu),
	  m_pom_tlb(nullptr),
	  software_tlb_base_register(nullptr),
	  m_size(nullptr),
	  m_associativity(nullptr)
	{
		// SimLog for debug output
		mmu_pomtlb_log = new SimLog("MMU_POMTLB", core->getId(), DEBUG_MMU_POMTLB);

		instantiatePageTableWalker();  // Creates PWC filter and PTW MSHRs
		instantiateTLBSubsystem();     // Creates HW TLB hierarchy and POM-TLBs
		registerMMUStats();            // Registers statistics with Sniper
	}

    /**
     * @brief Destroy the POM-TLB MMU.
     *
     * Cleans up all allocated resources including TLBs, POM-TLBs, and walkers.
     */
	MemoryManagementUnitPOMTLB::~MemoryManagementUnitPOMTLB()
	{
		delete mmu_pomtlb_log;
		delete tlb_subsystem;
		delete pt_walkers;
		delete[] translation_stats.tlb_latency_per_level;
		delete[] translation_stats.tlb_hit_page_sizes;
		delete[] m_pom_tlb;
		delete[] software_tlb_base_register;
		delete[] m_size;
		delete[] m_associativity;
	}

// ============================================================================
// Initialization Methods
// ============================================================================

    /**
     * @brief Initialize the Page Table Walker and Page Walk Cache.
     *
     * Creates the PWC filter for caching intermediate page table entries
     * and the MSHR structure for tracking concurrent page table walks.
     */
	void MemoryManagementUnitPOMTLB::instantiatePageTableWalker()
	{
		String mimicos_name = Sim()->getMimicOS()->getName();
		String page_table_type = Sim()->getCfg()->getString("perf_model/"+mimicos_name+"/page_table_type");
		String page_table_name = Sim()->getCfg()->getString("perf_model/"+mimicos_name+"/page_table_name");

		// Create Page Walk Cache filter
		String filter_type = Sim()->getCfg()->getString("perf_model/" + name + "/ptw_filter_type");
		ptw_filter = FilterPTWFactory::createFilterPTWBase(filter_type, name, core);

		// Create MSHRs for N concurrent page table walks
		pt_walkers = new MSHR(Sim()->getCfg()->getInt("perf_model/"+name+"/page_table_walkers"));
	}

    /**
     * @brief Instantiate metadata table (not used in POM-TLB variant).
     */
	void MemoryManagementUnitPOMTLB::instantiateMetadataTable()
	{
		// No metadata table for this MMU variant
	}

    /**
     * @brief Initialize the TLB hierarchy and POM-TLBs.
     *
     * Creates two components:
     * 1. Hardware TLB hierarchy (L1 iTLB, L1 dTLB, L2 TLB, etc.)
     * 2. Part-of-Memory TLBs - one per page size
     *
     * Each POM-TLB is allocated in physical memory and configured with:
     * - Number of entries and associativity from config
     * - Entry size calculated from tag bits + 52 bits for PPN
     * - Base address stored in software_tlb_base_register[]
     */
	void MemoryManagementUnitPOMTLB::instantiateTLBSubsystem()
	{
		// Create standard hardware TLB hierarchy
		tlb_subsystem = new TLBHierarchy(name, core, memory_manager, shmem_perf_model);

		// Read POM-TLB configuration
		int page_sizes = Sim()->getCfg()->getInt("perf_model/pom_tlb/page_sizes");

		// Allocate arrays for POM-TLB tracking
		software_tlb_base_register = new IntPtr[page_sizes];
		m_pom_tlb = new TLB*[page_sizes];

		// Create one POM-TLB per page size
		for (int i = 0; i < page_sizes; i++)
		{
			int page_size = Sim()->getCfg()->getIntArray("perf_model/pom_tlb/page_size_list", i);
			int entries   = Sim()->getCfg()->getIntArray("perf_model/pom_tlb/entries", i);
			int assoc     = Sim()->getCfg()->getIntArray("perf_model/pom_tlb/assoc", i);
			String type   = Sim()->getCfg()->getStringArray("perf_model/pom_tlb/type", i);
			bool allocate_on_miss = Sim()->getCfg()->getBoolArray("perf_model/pom_tlb/allocate_on_miss", i);

			String name = "pom_tlb_" + itostr(i);

			int num_sets = entries / assoc;

			// Calculate entry size for POM-TLB
			// Tag = 48-bit VA - page_offset - log2(num_sets)
			// Entry = tag + 52 bits (PPN + metadata)
			int tag_size   = (48 - page_size - log2(num_sets)); 
			int entry_size = tag_size + 52;

			// Allocate memory for POM-TLB in physical memory
			// Size = entries * entry_size bits, converted to bytes
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

			// Create the POM-TLB object (no fixed latency - latency comes from DRAM access)
			m_pom_tlb[i] = new TLB(name,
			                       "perf_model/pom_tlb",
			                       core->getId(),
			                       ComponentLatency(core->getDvfsDomain(), 0),
			                       entries,
			                       assoc,
			                       page_size_list,
			                       1,  // #page sizes in this TLB
			                       type,
			                       allocate_on_miss);
		}
	}

// ============================================================================
// Statistics Registration
// ============================================================================

    /**
     * @brief Register all MMU statistics with Sniper's statistics framework.
     *
     * Statistics include:
     * - page_faults: Pages not present in memory
     * - total_table_walk_latency: Cumulative PTW time
     * - total_fault_latency: Time handling page faults
     * - total_tlb_latency: Cumulative hardware TLB lookup time
     * - total_translation_latency: End-to-end translation time
     * - total_software_tlb_latency: Time spent in POM-TLB lookups
     * - total_walk_delay_latency: Time waiting for PTW MSHRs
     */
	void MemoryManagementUnitPOMTLB::registerMMUStats()
	{
		bzero(&translation_stats, sizeof(translation_stats));

		registerStatsMetric(name, core->getId(), "page_faults", &translation_stats.page_faults);
		registerStatsMetric(name, core->getId(), "total_table_walk_latency", &translation_stats.total_walk_latency);
		registerStatsMetric(name, core->getId(), "total_fault_latency", &translation_stats.total_fault_latency);
		registerStatsMetric(name, core->getId(), "total_tlb_latency", &translation_stats.total_tlb_latency);
		registerStatsMetric(name, core->getId(), "total_translation_latency", &translation_stats.total_translation_latency);
		registerStatsMetric(name, core->getId(), "total_software_tlb_latency", &translation_stats.software_tlb_latency);
		registerStatsMetric(name, core->getId(), "total_walk_delay_latency", &translation_stats.total_walk_delay_latency);

		// Per-level TLB latency statistics
		translation_stats.tlb_latency_per_level = new SubsecondTime[tlb_subsystem->getTLBSubsystem().size()];
		for (UInt32 i = 0; i < tlb_subsystem->getTLBSubsystem().size(); i++)
			registerStatsMetric(name, core->getId(), "tlb_latency_" + itostr(i), &translation_stats.tlb_latency_per_level[i]);
	}

// ============================================================================
// Address Translation - Core MMU Operation
// ============================================================================

    /**
     * @brief Perform virtual-to-physical address translation with POM-TLB.
     *
     * This is the main entry point for address translation. The POM-TLB MMU
     * extends the standard translation flow with a software-managed TLB layer.
     *
     * ┌─────────────────────────────────────────────────────────────────────┐
     * │                  POM-TLB TRANSLATION FLOW                            │
     * ├─────────────────────────────────────────────────────────────────────┤
     * │                                                                      │
     * │   Virtual Addr ──► HW TLB Lookup ──┬──► HIT ──► Physical Address     │
     * │                                    │                                 │
     * │                                    └──► MISS                         │
     * │                                          │                           │
     * │                                          ▼                           │
     * │                                    POM-TLB Lookup                    │
     * │                                    (parallel for each page size)     │
     * │                                          │                           │
     * │                              +-----------+----------+                │
     * │                              ▼                      ▼                │
     * │                            HIT                    MISS               │
     * │                              │                      │                │
     * │                              │                      ▼                │
     * │                              │             Page Table Walk           │
     * │                              │                      │                │
     * │                              │                      ▼                │
     * │                              │           Allocate in POM-TLB         │
     * │                              │                      │                │
     * │                              +----------+-----------+                │
     * │                                         │                            │
     * │                                         ▼                            │
     * │                              Allocate in HW TLBs                     │
     * │                                         │                            │
     * │                                         ▼                            │
     * │                                  Physical Address                    │
     * └─────────────────────────────────────────────────────────────────────┘
     *
     * @param eip          Instruction pointer causing this access
     * @param address      Virtual address to translate
     * @param instruction  True if this is an instruction fetch
     * @param lock         Cache coherence lock signal
     * @param modeled      True to model timing
     * @param count        True to update statistics
     * @return Physical address
     */
	IntPtr MemoryManagementUnitPOMTLB::performAddressTranslation(IntPtr eip,
	                                                             IntPtr address,
	                                                             bool instruction,
	                                                             Core::lock_signal_t lock,
	                                                             bool modeled,
	                                                             bool count)
	{
		// Track DRAM accesses for power/performance analysis
		dram_accesses_during_last_walk = 0;

		mmu_pomtlb_log->debug("[MMU:POM-TLB] ---- Starting address translation for virtual address: %lx ---- at time %s",
			address, shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD).getNS());

		int number_of_page_sizes = Sim()->getMimicOS()->getNumberOfPageSizes();
		int *page_size_list = Sim()->getMimicOS()->getPageSizeList();

		SubsecondTime time = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

		// Cleanup completed PTW entries from MSHR
		pt_walkers->removeCompletedEntries(time);

		if (count)
			translation_stats.num_translations++;

		// Get reference to hardware TLB hierarchy
		TLBSubsystem tlbs = tlb_subsystem->getTLBSubsystem();

		// ====================================================================
		// PHASE 1: Hardware TLB Lookup
		// ====================================================================

		bool tlb_hit   = false;
		TLB *hit_tlb   = NULL;
		CacheBlockInfo *tlb_block_info_hit = NULL; 
		CacheBlockInfo *tlb_block_info     = NULL; 
		int hit_level  = -1;

		int page_size_result = -1;
		IntPtr ppn_result    = 0;

		// Search hardware TLB hierarchy
		for (UInt32 i = 0; i < tlbs.size(); i++)
		{
			mmu_pomtlb_log->debug("[MMU] Searching TLB at level: %u", i);
			for (UInt32 j = 0; j < tlbs[i].size(); j++)
			{
				bool tlb_stores_instructions = ((tlbs[i][j]->getType() == TLBtype::Instruction) ||
				                                 (tlbs[i][j]->getType() == TLBtype::Unified));

				if (tlb_stores_instructions && instruction)
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
				mmu_pomtlb_log->debug("[MMU] TLB Hit at level: %d at TLB %s", hit_level, hit_tlb->getName().c_str());
				break;
			}
		}

		// ====================================================================
		// PHASE 2: Charge Hardware TLB Latency
		// ====================================================================

		SubsecondTime charged_tlb_latency = SubsecondTime::Zero();

		if (tlb_hit)
		{
			// ----- HW TLB HIT: Charge latency up to hit level -----
			page_size_result = tlb_block_info_hit->getPageSize();
			ppn_result       = tlb_block_info_hit->getPPN();

			mmu_pomtlb_log->debug("[MMU] TLB Hit ? %d at level: %d at TLB: %s", tlb_hit, hit_level, hit_tlb->getName().c_str());

			if (instruction)
				tlbs = tlb_subsystem->getInstructionPath();
			else
				tlbs = tlb_subsystem->getDataPath();

			SubsecondTime tlb_latency[hit_level + 1];

			for (int i = 0; i < hit_level; i++)
			{
				for (UInt32 j = 0; j < tlbs[i].size(); j++)
				{
					tlb_latency[i] = max(tlbs[i][j]->getLatency(), tlb_latency[i]);
				}
				mmu_pomtlb_log->debug("[MMU] Charging TLB Latency: %lu at level: %d", tlb_latency[i].getNS(), i);
				translation_stats.total_tlb_latency += tlb_latency[i];
				translation_stats.tlb_latency_per_level[i] += tlb_latency[i];
				charged_tlb_latency += tlb_latency[i];
			}

			for (UInt32 j = 0; j < tlbs[hit_level].size(); j++)
			{
				if (tlbs[hit_level][j] == hit_tlb)
				{
					translation_stats.total_tlb_latency += hit_tlb->getLatency();
					charged_tlb_latency += hit_tlb->getLatency();
					translation_stats.tlb_latency_per_level[hit_level] += hit_tlb->getLatency();
					mmu_pomtlb_log->debug("[MMU] Charging TLB Hit Latency: %lu at level: %d", hit_tlb->getLatency().getNS(), hit_level);
				}
			}

			// Advance the clock to reflect TLB-latency consumption
			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time + charged_tlb_latency);
			if (count)
				translation_stats.total_translation_latency += charged_tlb_latency;

			mmu_pomtlb_log->debug("[MMU] New time after charging TLB latency: %lu", shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD).getNS());
		}
		else
		{
			/*
			 * 2a) TLB Miss => sum the latency for all TLB levels. We do not break out at a 
			 *     partial level because we "visited" them all searching for a hit.
			 */
			mmu_pomtlb_log->debug("[MMU] TLB Miss");
			SubsecondTime tlb_latency[tlbs.size()];

			for (UInt32 i = 0; i < tlbs.size(); i++)
			{
				for (UInt32 j = 0; j < tlbs[i].size(); j++)
				{
					tlb_latency[i] = max(tlbs[i][j]->getLatency(), tlb_latency[i]);
				}
				mmu_pomtlb_log->debug("[MMU] Charging TLB Latency: %lu at level: %u", tlb_latency[i].getNS(), i);
				translation_stats.total_tlb_latency += tlb_latency[i];
				charged_tlb_latency += tlb_latency[i];
			}

			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time + charged_tlb_latency);
			if (count)
				translation_stats.total_translation_latency += charged_tlb_latency;

			mmu_pomtlb_log->debug("[MMU] New time after charging TLB latency: %lu", shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD).getNS());
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
			mmu_pomtlb_log->debug("[MMU] TLB Miss, checking software TLB");
			for (int page_size = 0; page_size < number_of_page_sizes; page_size++)
			{
				mmu_pomtlb_log->debug("[MMU] Searching software TLB for page size: %d", page_size_list[page_size]);
				TLB* pom = m_pom_tlb[page_size];
				software_tlb_block_info = pom->lookup(address, time, count, lock, eip, modeled, count, NULL);

				mmu_pomtlb_log->debug("[MMU] Software TLB Hit ? %d at TLB: %s", (software_tlb_block_info != NULL), pom->getName().c_str());

				// Simulate the memory access that the software TLB structure does
				translationPacket packet;
				packet.eip          = eip;
				packet.instruction  = false;
				packet.lock_signal  = lock;
				packet.modeled      = modeled;
				packet.count        = count;
				// Software TLB metadata access
				packet.type         = CacheBlockInfo::block_type_t::PAGE_TABLE_DATA;

				IntPtr tag;
				UInt32 set_index;
				pom->getCache().splitAddressTLB(address, tag, set_index, page_size_list[page_size]);

				mmu_pomtlb_log->debug("[MMU] Software TLB Lookup: %lx at page size: %d with tag: %lx and set index: %u and base address: %lx",
					address, page_size_list[page_size], tag, set_index, software_tlb_base_register[page_size]*4096);
				packet.address = software_tlb_base_register[page_size]*4096
				                 + pom->getAssoc()* pom->getEntrySize() * set_index;

				mmu_pomtlb_log->debug("[MMU] Software TLB Address: %lx", packet.address);

                    HitWhere::where_t sw_hit_where = HitWhere::UNKNOWN;
                    software_tlb_latency[page_size] = accessCache(packet, time_before_software_tlb, false, sw_hit_where);

				mmu_pomtlb_log->debug("[MMU] Software TLB Latency: %lu at page size: %d", software_tlb_latency[page_size].getNS(), page_size_list[page_size]);

				if (software_tlb_block_info != NULL)
				{
					// We have a software TLB hit
					final_software_tlb_latency = software_tlb_latency[page_size];
					software_tlb_hit = true;
					ppn_result = software_tlb_block_info->getPPN();
					page_size_result = software_tlb_block_info->getPageSize();
					mmu_pomtlb_log->debug("[MMU] Software TLB Hit at page size: %d", page_size_result);
					mmu_pomtlb_log->debug("[MMU] Software TLB Hit PPN: %lx", ppn_result);
					mmu_pomtlb_log->debug("[MMU] Software TLB Hit VPN: %lx", (address >> page_size_result));
					mmu_pomtlb_log->debug("[MMU] Software TLB Hit Tag: %lx", tag);
					mmu_pomtlb_log->debug("[MMU] Software TLB Hit Latency: %lu", final_software_tlb_latency.getNS());
				}
			}

			if (!software_tlb_hit)
			{
				// If all software TLBs missed, we take the max of the latencies
				mmu_pomtlb_log->debug("[MMU] Software TLB Miss");
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

			mmu_pomtlb_log->debug("[MMU] Final Software TLB Latency: %lu", final_software_tlb_latency.getNS());

			// Advance the clock by the final software TLB latency
			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD,
				time_before_software_tlb + final_software_tlb_latency);
			if (count)
				translation_stats.total_translation_latency += final_software_tlb_latency;

			mmu_pomtlb_log->debug("[MMU] New time after charging Software TLB latency: %lu", shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD).getNS());
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

			mmu_pomtlb_log->debug("[MMU] New time after charging the PT walker allocation delay: %lu ns", shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD).getNS());

			// Perform the PT walk
			int app_id = core->getThread()->getAppId();
			PageTable* page_table = Sim()->getMimicOS()->getPageTable(app_id);
			bool userspace_mimicos_enabled = Sim()->getMimicOS()->isUserspaceMimicosEnabled();

			// @kanellok: restart_walk_after_fault is now false by default
			// The MMU handles page faults and restarts the walk itself
			const bool restart_walk_upon_page_fault = false;
			bool caused_page_fault = false;
			bool had_page_fault = false;  // Persists across loop iterations for fault latency tracking
			
			// Declare ptw_result outside the loop so it's accessible after
			PTWOutcome ptw_result;

			// Loop to handle page faults: perform PTW, handle fault if needed, retry
			do {
				ptw_result = performPTW(address, modeled, count, false, eip, lock, page_table, restart_walk_upon_page_fault, instruction);

mmu_pomtlb_log->debug("[MMU] PTW Result: %lu %d %lx %d", ptw_result.latency.getNS(), ptw_result.page_fault, ptw_result.ppn, ptw_result.page_size);

			total_walk_latency = ptw_result.latency;
			if (count)
			{
				translation_stats.total_walk_latency += total_walk_latency;
				translation_stats.page_table_walks++;
			}

			caused_page_fault = ptw_result.page_fault;
			if (caused_page_fault)
			{
				had_page_fault = true;  // Track that a fault occurred
				mmu_pomtlb_log->debug("[MMU] Page Fault occured");
				translation_stats.page_faults++;
				
				// Handle page fault at MMU level (sniper-space mode)
				if (!userspace_mimicos_enabled)
				{
					mmu_pomtlb_log->debug("[MMU] Handling page fault in sniper-space mode, calling exception handler");
					ExceptionHandlerBase *handler = Sim()->getCoreManager()->getCoreFromID(core->getId())->getExceptionHandler();

					ExceptionHandlerBase::FaultCtx fault_ctx{};
					fault_ctx.vpn = address >> 12;
					fault_ctx.page_table = page_table;
					fault_ctx.alloc_in.metadata_frames = ptw_result.requested_frames;
					handler->handle_page_fault(fault_ctx);
					
					mmu_pomtlb_log->debug("[MMU] Page fault handled, restarting PTW for address: %lx", address);
					// Loop will retry PTW after fault is handled
				}
			}
			} while (caused_page_fault && !userspace_mimicos_enabled);
			
			// Calculate fault latency for timing (after loop, fault should be resolved)
			if (had_page_fault && !userspace_mimicos_enabled)
			{
				SubsecondTime m_page_fault_latency = Sim()->getMimicOS()->getPageFaultLatency();
				if (count)
				{
					translation_stats.total_fault_latency += m_page_fault_latency;
				}
				total_fault_latency = m_page_fault_latency;
			}

			pt_walker_entry.completion_time = time_for_pt + delay 
			                                  + total_walk_latency
			                                  + total_fault_latency;

			pt_walkers->allocate(pt_walker_entry);

			// Move the time to the end of the PTW + possible page fault
			if (had_page_fault && !userspace_mimicos_enabled)
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

			ppn_result       = ptw_result.ppn;
			page_size_result = ptw_result.page_size;

mmu_pomtlb_log->debug("[MMU] New time after charging the PT walker completion time: %lu", shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD).getNS());
		}

mmu_pomtlb_log->debug("[MMU] Total Walk Latency: %lu", total_walk_latency.getNS());
		mmu_pomtlb_log->debug("[MMU] Total Fault Latency: %lu", total_fault_latency.getNS());

		// 5) Allocate the translation in intermediate TLB levels (if needed).
		if (instruction)
			tlbs = tlb_subsystem->getInstructionPath();
		else
			tlbs = tlb_subsystem->getDataPath();

		std::map<int, vector<EvictedTranslation>> evicted_translations;
		int tlb_levels = tlbs.size();

		// Some TLB hierarchies might have a "prefetch" TLB, skipping the last level for 
		// "allocate on miss." We handle that with isPrefetchEnabled checks if relevant.
		if (tlb_subsystem->isPrefetchEnabled())
		{
			tlb_levels = tlbs.size() - 1;
			mmu_pomtlb_log->debug("[MMU] Prefetching is enabled");
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
					TLBAllocResult result;

					mmu_pomtlb_log->debug("[MMU] There are evicted translations from level: %d", (i - 1));
					for (const EvictedTranslation &evicted : evicted_translations[i - 1])
					{
						mmu_pomtlb_log->debug("[MMU] Evicted Translation: %lx", evicted.address);
						IntPtr evicted_address = evicted.address;
						int evicted_page_size = evicted.page_size;
						IntPtr evicted_ppn = evicted.ppn;
						if (tlbs[i][j]->supportsPageSize(evicted_page_size))
						{
							mmu_pomtlb_log->debug("[MMU] Allocating evicted entry in TLB: Level = %d Index =  %d", i, j);
							result = tlbs[i][j]->allocate(evicted_address, time, count, lock,
							                              evicted_page_size, evicted_ppn);

							if (result.evicted)
							{
								evicted_translations[i].push_back(
									EvictedTranslation(result.address, result.page_size, result.ppn));
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
					mmu_pomtlb_log->debug("[MMU] %s supports page size: %d", tlbs[i][j]->getName().c_str(), page_size_result);
					mmu_pomtlb_log->debug("[MMU] Allocating in TLB: Level = %d Index = %d with page size: %d and VPN: %lx", i, j, page_size_result, (address >> page_size_result));
					TLBAllocResult result = tlbs[i][j]->allocate(address, time, count, lock, page_size_result, ppn_result);
					if (result.evicted)
					{
						evicted_translations[i].push_back(
							EvictedTranslation(result.address, result.page_size, result.ppn));
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
					mmu_pomtlb_log->debug("[MMU] Allocating in Software TLB: Page Size = %d with VPN: %lx with PPN: %lx", page_size_list[page_size], (address >> page_size_result), ppn_result);
					pom->allocate(address, time, count, lock, page_size_result, ppn_result);
				}
			}
		}

		// ====================================================================
		// PHASE 6: Calculate Physical Address
		// ====================================================================

		// Physical address = PPN * base_page_size + offset
		// PPN is always at 4KB granularity
		// Offset is extracted based on actual page size (4KB or 2MB)

		const int page_size_in_bytes = 1 << page_size_result;      // 2^page_size (optimized from pow())
		constexpr int base_page_size_in_bytes = 1 << 12;           // 4KB base page

		IntPtr final_physical_address = (ppn_result * base_page_size_in_bytes)
		                                + (address % page_size_in_bytes);

mmu_pomtlb_log->debug("[MMU] Offset: %lx", (address % page_size_in_bytes));
		mmu_pomtlb_log->debug("[MMU] Physical Address: %lx PPN: %lx Page Size: %d", final_physical_address, ppn_result*base_page_size_in_bytes, page_size_result);
		mmu_pomtlb_log->debug("[MMU] Total translation latency: %lu", (charged_tlb_latency + final_software_tlb_latency + total_walk_latency).getNS());
		mmu_pomtlb_log->debug("[MMU] Total fault latency: %lu", total_fault_latency.getNS());
		mmu_pomtlb_log->debug("[MMU] ---- Ending address translation for virtual address: %lx ----", address);

		return final_physical_address;
	}

// ============================================================================
// Page Table Walk Support
// ============================================================================

    /**
     * @brief Filter PTW result through Page Walk Cache (PWC).
     *
     * Removes intermediate page table entries that are already cached in the
     * PWC, reducing the effective walk latency.
     *
     * @param address     Virtual address being translated
     * @param ptw_result  Raw PTW result from page table
     * @param page_table  Page table being walked
     * @param count       Whether to count statistics
     * @return Filtered PTW result with cached entries removed
     */
	PTWResult MemoryManagementUnitPOMTLB::filterPTWResult(IntPtr address, PTWResult ptw_result,
	                                                      PageTable *page_table,
	                                                      bool count)
	{
		return ptw_filter->filterPTWResult(address, ptw_result, page_table, count);
	}

    /**
     * @brief Get the page table for the current thread.
     *
     * @return Pointer to the current thread's page table
     */
	PageTable *MemoryManagementUnitPOMTLB::getPageTable()
	{
		int app_id = core->getThread()->getAppId();
		return Sim()->getMimicOS()->getPageTable(app_id);
	}

    /**
     * @brief Discover Virtual Memory Areas (VMAs).
     *
     * Not implemented for this MMU variant. VMAs are used by range-based
     * MMUs for coalescing translations.
     */
	void MemoryManagementUnitPOMTLB::discoverVMAs()
	{
	}

} // namespace ParametricDramDirectoryMSI
