/**
 * @file mmu_dmt.cc
 * @brief Direct Memory Translation MMU - Single-Level Page Table Access
 *
 * This MMU implements Direct Memory Translation (DMT) which accesses only
 * the last level of the page table, significantly reducing translation
 * overhead compared to a full radix page table walk.
 *
 * ============================================================================
 * DIRECT MEMORY TRANSLATION OVERVIEW
 * ============================================================================
 *
 * Traditional Radix PTW (4-level x86-64):
 *   PML4 → PDPT → PD → PT → Physical Address
 *   (4 memory accesses per translation on TLB miss)
 *
 * Direct Memory Translation:
 *   Direct Table → Physical Address
 *   (1 memory access per translation on TLB miss)
 *
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │                     DMT TRANSLATION FLOW                                 │
 * ├─────────────────────────────────────────────────────────────────────────┤
 * │                                                                          │
 * │   Virtual Addr ──► TLB Lookup ──┬──► HIT ──► Physical Address            │
 * │                                 │                                        │
 * │                                 └──► MISS                                │
 * │                                       │                                  │
 * │                                       ▼                                  │
 * │                         ┌──────────────────────┐                         │
 * │                         │  Direct Translation  │                         │
 * │                         │  (Single PT Access)  │                         │
 * │                         └──────────┬───────────┘                         │
 * │                                    │                                     │
 * │                                    ▼                                     │
 * │                           Physical Address                               │
 * └─────────────────────────────────────────────────────────────────────────┘
 *
 * KEY DIFFERENCE FROM STANDARD MMU:
 * ----------------------------------
 * Instead of calling performPTW() which walks all page table levels,
 * DMT calls page_table->initializeWalk() and only accesses the final
 * translation entry, reducing memory accesses from 4 to 1.
 *
 * USE CASES:
 * - Single-level page tables
 * - Flat memory models
 * - Performance analysis of translation overhead
 * - Research into reduced-depth page tables
 *
 * LIMITATIONS:
 * - Requires page table format that supports direct access
 * - May not support all page sizes (primarily 4KB)
 *
 * @author Konstantinos Kanellopoulos
 * @author SAFARI Research Group
 */

#include "mmu.h"
#include "mmu_base.h"
#include "mmu_dmt.h"
#include "memory_manager.h"
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "pagetable_factory.h"
#include "pagetable_radix.h"
#include "metadata_factory.h"
#include "mimicos.h"
#include "instruction.h"
#include "core.h"
#include "thread.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include "filter_factory.h"
#include "misc/exception_handler_base.h"
// sniper_space_exception_handler.h no longer needed — using base-class interface

/* If you enable this flag, you will see debug messages that will help you understand the address translation process
 * This is useful for debugging purposes 
*/
//#define DEBUG_MMU 

using namespace std;

namespace ParametricDramDirectoryMSI
{

// ============================================================================
// Construction / Destruction
// ============================================================================

    /**
     * @brief Construct the Direct Memory Translation MMU.
     *
     * Initializes an MMU that performs single-level page table access
     * for faster translations at the cost of flexibility.
     *
     * @param _core           Pointer to the core this MMU belongs to
     * @param _memory_manager Memory manager for cache access
     * @param _shmem_perf_model Performance model for timing
     * @param _name           Configuration name prefix
     * @param _nested_mmu     Optional nested MMU for virtualization
     */
	MemoryManagementUnitDMT::MemoryManagementUnitDMT(Core *_core, MemoryManagerBase *_memory_manager, ShmemPerfModel *_shmem_perf_model, String _name, MemoryManagementUnitBase *_nested_mmu)
	: MemoryManagementUnitBase(_core, _memory_manager, _shmem_perf_model, _name, _nested_mmu), memory_manager(_memory_manager)
	{
		std::cout << std::endl;
		std::cout << "[MMU] Initializing MMU for core " << core->getId() << std::endl;
		// SimLog for debug output
		mmu_dmt_log = new SimLog("MMU_DMT", core->getId(), DEBUG_MMU_DMT);

		instantiatePageTableWalker(); // This instantiates the page table walker
		instantiateTLBSubsystem(); // This instantiates the TLB hierarchy
		registerMMUStats(); // This instantiates the MMU stats
		std::cout << std::endl;

	}

    /**
     * @brief Destroy the Direct Memory Translation MMU.
     *
     * Cleans up all allocated resources including TLBs and PTW structures.
     */
	MemoryManagementUnitDMT::~MemoryManagementUnitDMT()
	{
		delete mmu_dmt_log;
		delete tlb_subsystem;
		delete pt_walkers;
		delete[] translation_stats.tlb_latency_per_level;
		delete[] translation_stats.tlb_hit_page_sizes;
	}

// ============================================================================
// Initialization Methods
// ============================================================================

    /**
     * @brief Initialize the optional metadata table.
     *
     * The metadata table stores additional information about memory regions
     * such as protection bits and semantic information.
     */
	void MemoryManagementUnitDMT::instantiateMetadataTable()
	{
		String metadata_table_name = Sim()->getCfg()->getString("perf_model/"+name+"/metadata_table_name");
		metadata_table = MetadataFactory::createMetadataTable(metadata_table_name, core, shmem_perf_model, this, memory_manager);
	}

    /**
     * @brief Initialize the Page Table Walker.
     *
     * Creates the PTW filter and MSHR structure. Note that DMT only
     * accesses the last level, so intermediate caching is less critical.
     */
	void MemoryManagementUnitDMT::instantiatePageTableWalker()
	{
		String mimicos_name = Sim()->getMimicOS()->getName();
		String page_table_type = Sim()->getCfg()->getString("perf_model/"+mimicos_name+"/page_table_type");
		String page_table_name = Sim()->getCfg()->getString("perf_model/"+mimicos_name+"/page_table_name");

		String filter_type = Sim()->getCfg()->getString("perf_model/" + name + "/ptw_filter_type");
		ptw_filter = FilterPTWFactory::createFilterPTWBase(filter_type, name, core);

		pt_walkers = new MSHR(Sim()->getCfg()->getInt("perf_model/"+name+"/page_table_walkers"));
			
	}

    /**
     * @brief Initialize the TLB hierarchy.
     *
     * Creates the standard hardware TLB hierarchy (L1 iTLB, L1 dTLB, L2 TLB, etc.)
     */
	void MemoryManagementUnitDMT::instantiateTLBSubsystem()
	{
		tlb_subsystem = new TLBHierarchy(name, core, memory_manager, shmem_perf_model);
	}

// ============================================================================
// Statistics Registration
// ============================================================================

    /**
     * @brief Register all MMU statistics with Sniper's statistics framework.
     *
     * Note: DMT does not track page_table_walks separately since each
     * translation is a single memory access.
     */
	void MemoryManagementUnitDMT::registerMMUStats()
	{
		bzero(&translation_stats, sizeof(translation_stats));

		// Statistics for the whole MMU
		registerStatsMetric(name, core->getId(), "num_translations", &translation_stats.num_translations);
		registerStatsMetric(name, core->getId(), "page_faults", &translation_stats.page_faults);
		registerStatsMetric(name, core->getId(), "total_table_walk_latency", &translation_stats.total_walk_latency);
		registerStatsMetric(name, core->getId(), "total_tlb_latency", &translation_stats.total_tlb_latency);
		registerStatsMetric(name, core->getId(), "total_translation_latency", &translation_stats.total_translation_latency);
		registerStatsMetric(name, core->getId(), "total_fault_latency", &translation_stats.total_fault_latency);


		// This statistic can be used to compare it against the *.active counter which is exposed through performance counters in a real system
		registerStatsMetric(name, core->getId(), "walker_is_active", &translation_stats.walker_is_active);

		// Statistics for TLB subsystem
		translation_stats.tlb_latency_per_level = new SubsecondTime[tlb_subsystem->getTLBSubsystem().size()];

		// Keep track of the tlb latency for each TLB level
		for (UInt32 i = 0; i < tlb_subsystem->getTLBSubsystem().size(); i++){
			registerStatsMetric(name, core->getId(), "tlb_latency_" + itostr(i), &translation_stats.tlb_latency_per_level[i]);
		}
			
	}

// ============================================================================
// Address Translation - Core MMU Operation
// ============================================================================

	/**
	 * @brief Perform Direct Memory Translation.
	 *
	 * This is the main entry point for address translation. Unlike standard
	 * MMUs that walk all page table levels, DMT directly accesses only the
	 * last-level page table entry for the translation.
	 *
	 * Translation Phases:
	 * 1. TLB Hierarchy Lookup - Check all TLB levels for cached translation
	 * 2. Direct PT Access - On TLB miss, access only last-level PTE
	 * 3. Page Fault Handling - If not present, handle via exception
	 * 4. TLB Allocation - Insert translation into TLB hierarchy
	 *
	 * @param eip          Instruction pointer causing this access
	 * @param address      Virtual address to translate
	 * @param instruction  True if this is an instruction fetch
	 * @param lock         Cache coherence lock signal
	 * @param modeled      True to model timing
	 * @param count        True to update statistics
	 * @return Physical address, or -1 if userspace page fault
	 */

	IntPtr MemoryManagementUnitDMT::performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count)
	{
		// We keep track of the number of accesses to the DRAM during the translation process
		dram_accesses_during_last_walk = 0;

		mmu_dmt_log->debug("");
		mmu_dmt_log->debug("[MMU] ---- Starting address translation for virtual address: %lx ---- at time %lu", address, shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD).getNS());

		SubsecondTime time = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD); 

		// Cleanup so that we do not have any completed entries left inside the MSHRs of the PT walkers
		pt_walkers->removeCompletedEntries(time);

		if (count)
			translation_stats.num_translations++;

		TLBSubsystem tlbs = tlb_subsystem->getTLBSubsystem(); // Get the TLB hierarchy

		// ====================================================================
		// PHASE 1: TLB Hierarchy Lookup
		// ====================================================================

		bool hit = false; // Variables to keep track of TLB hits
		TLB *hit_tlb = NULL; // We need to keep track of the TLB that hit

		CacheBlockInfo *tlb_block_info_hit = NULL; // If there is a TLB hit, we need to keep track of the block info (which eventually contains the translation)
		CacheBlockInfo *tlb_block_info = NULL; // This is the block info that we get from the TLB lookup
		
		int hit_level = -1;
		int page_size = -1; // This variable will reflect if the virtual address is mapped to a 4KB page or a 2MB page

		IntPtr ppn_result = 0;
		// We iterate through the TLB hierarchy to find if there is a TLB hit
		for (UInt32 i = 0; i < tlbs.size(); i++)
		{
			mmu_dmt_log->debug("[MMU] Searching TLB at level: %u", i);
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
				mmu_dmt_log->debug("[MMU] TLB Hit at level: %d at TLB %s", hit_level, hit_tlb->getName().c_str());
				break;
			}
		}


		SubsecondTime charged_tlb_latency = SubsecondTime::Zero();
		
		//If we have a TLB hit, we need to charge the TLB hit latency
		if (hit)
		{
			ppn_result = tlb_block_info_hit->getPPN(); // We get the PPN from the block info
			page_size = tlb_block_info_hit->getPageSize(); // We get the page size from the block info
			mmu_dmt_log->debug("[MMU] TLB Hit ? %d at level: %d at TLB: %s", hit, hit_level, hit_tlb->getName().c_str());
			
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
				mmu_dmt_log->debug("[MMU] Charging TLB Latency: %lu at level: %d", tlb_latency[i].getNS(), i);
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

					mmu_dmt_log->debug("[MMU] Charging TLB Hit Latency: %lu at level: %d", hit_tlb->getLatency().getNS(), hit_level);
				}
			}

			// Progress the clock to the time after the TLB latency
			// This is done so that the PTW starts after the TLB latency
			// @kanellok: Be very careful if you want to play around with the timing model
			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time + charged_tlb_latency); 
			mmu_dmt_log->debug("[MMU] New time after charging TLB latency: %lu", shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD).getNS());

		}

		// If we have a TLB miss, we need to charge the TLB latency based on the slowest component 
		// at each level of the hierarchy
		
		SubsecondTime tlb_latency[tlbs.size()];


		// If we have a TLB miss, we need to charge the TLB latency based on the slowest component	
		// at each level of the hierarchy, across all levels of the hierarchy

		if (!hit)
		{
			
			mmu_dmt_log->debug("[MMU] TLB Miss");
			for (UInt32 i = 0; i < tlbs.size(); i++) 
			{
				for (UInt32 j = 0; j < tlbs[i].size(); j++)
				{
					tlb_latency[i] = max(tlbs[i][j]->getLatency(), tlb_latency[i]);
				}
				mmu_dmt_log->debug("[MMU] Charging TLB Latency: %lu at level: %u", tlb_latency[i].getNS(), i);
				translation_stats.total_tlb_latency += tlb_latency[i];
				charged_tlb_latency += tlb_latency[i];
			}
			
			// We progress the time by the charged TLB latency so that the PTW starts after the TLB latency
			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time + charged_tlb_latency);
			mmu_dmt_log->debug("[MMU] New time after charging TLB latency: %lu", shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD).getNS());
		}

		// We need to keep track of the total walk latency and the total fault latency (if there was a fault)
		SubsecondTime total_walk_latency = SubsecondTime::Zero();
		SubsecondTime total_fault_latency = SubsecondTime::Zero();

		// ====================================================================
		// PHASE 2: Direct Memory Translation (on TLB miss)
		// ====================================================================
		// Unlike standard PTW, DMT only accesses the last-level PTE directly.
		// This reduces memory accesses from 4 (radix) to 1.

		// We only trigger the PTW if there was a TLB miss
		if (!hit)
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
			mmu_dmt_log->debug("[MMU] New time after charging the PT walker allocation delay: %lu", shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD).getNS());

			// returns PTW latency, PF latency, Physical Address, Page Size as a tuple
			int app_id = core->getThread()->getAppId();
			PageTable* page_table = Sim()->getMimicOS()->getPageTable(app_id);
			bool userspace_mimicos_enabled = Sim()->getMimicOS()->isUserspaceMimicosEnabled();
			
			// Declare variables outside the loop so they're accessible after
			decltype(page_table->initializeWalk(address, count, false, false)) ptw_result;
			accessedAddresses visited_pts;
			bool caused_page_fault = false;
			bool had_page_fault = false;  // Persists across loop iterations
			
			// Loop to handle page faults: perform PTW, handle fault if needed, retry
			do {
				ptw_result = page_table->initializeWalk(address, count, false, false);

				visited_pts = ptw_result.accesses;
				std::sort(visited_pts.begin(), visited_pts.end());
				visited_pts.erase(std::unique(visited_pts.begin(), visited_pts.end()), visited_pts.end());
				
				caused_page_fault = ptw_result.fault_happened;

				if (caused_page_fault)
				{
					had_page_fault = true;  // Track that a fault occurred
					mmu_dmt_log->debug("[MMU] Page Table Walk Result: Page Fault occurred!");

					if (count)
					{
						translation_stats.page_faults++;
					}

					// Handle page fault at MMU level (sniper-space mode)
					if (!userspace_mimicos_enabled)
					{
						mmu_dmt_log->debug("[MMU] Handling page fault in sniper-space mode, calling exception handler");
						ExceptionHandlerBase *handler = Sim()->getCoreManager()->getCoreFromID(core->getId())->getExceptionHandler();
						ExceptionHandlerBase::FaultCtx fault_ctx{};
						fault_ctx.vpn = address >> 12;
						fault_ctx.page_table = page_table;
						fault_ctx.alloc_in.metadata_frames = ptw_result.requested_frames;
						handler->handle_page_fault(fault_ctx);

						SubsecondTime m_page_fault_latency = Sim()->getMimicOS()->getPageFaultLatency();
						if (count)
						{
							translation_stats.total_fault_latency += m_page_fault_latency;
						}
						total_fault_latency = m_page_fault_latency;
						
						mmu_dmt_log->debug("[MMU] Page fault handled, restarting PTW for address: %lx", address);
						// Loop will retry PTW after fault is handled
					}
				}
			} while (caused_page_fault && !userspace_mimicos_enabled);
			
			// In case that MimicOS is enabled in userspace mode, we need to handle the page fault at the MimicOS level
			if (caused_page_fault && userspace_mimicos_enabled)
			{
				mmu_dmt_log->debug("[MMU] Page Fault caused in userspace by address: %lx", address);
				return static_cast<IntPtr>(-1); // If there was a page fault in userspace mode, return -1
			}

			mmu_dmt_log->debug("[MMU] Page Table Walk Result:");
			for (const auto& pt : visited_pts)
			{
				mmu_dmt_log->debug("[MMU] PTW Address: %lx, Level: %d, Depth: %d, correct translation?: %d", pt.physical_addr, pt.table_level, pt.depth, pt.is_pte);
			}

			// We need to access the memory hierarchy to the get the final PA
			translationPacket packet;
			packet.eip = eip;
			packet.instruction = instruction;
			packet.lock_signal = lock;
			packet.modeled = modeled;
			packet.count = count;
			// Use PAGE_TABLE_DATA for page table accesses during PTW
			packet.type = instruction ? CacheBlockInfo::block_type_t::PAGE_TABLE_INSTRUCTION : CacheBlockInfo::block_type_t::PAGE_TABLE_DATA;
			packet.address = visited_pts[visited_pts.size()-1].physical_addr;

			mmu_dmt_log->debug("[MMU] Accessing memory hierarchy using address: %lx", packet.address);

			SubsecondTime t_now = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
			HitWhere::where_t hit_where_temp = HitWhere::UNKNOWN;
			SubsecondTime total_ptw_latency = accessCache(packet, t_now, false, hit_where_temp);
			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, t_now + total_ptw_latency);
			
			if (count)
			{
				translation_stats.total_walk_latency += total_ptw_latency;
				translation_stats.page_table_walks++;
			}

			ppn_result = ptw_result.ppn;
			page_size = ptw_result.page_size;

			/*
			We need to set the completion time:
			1) Time before PTW starts
			2) Delay because of all the walkers being busy
			3) Total walk latency
			4) Total fault latency
			*/

			pt_walker_entry.completion_time = time_for_pt + delay + total_ptw_latency + total_fault_latency;
			pt_walkers->allocate(pt_walker_entry);

			mmu_dmt_log->debug("[MMU] PTW Completion Time: %lu", pt_walker_entry.completion_time.getNS());

			// Queue pseudo-instruction to model fault handling overhead in sniper-space mode
			if (had_page_fault && !userspace_mimicos_enabled)
			{
				PseudoInstruction *i = new PageFaultRoutineInstruction(total_fault_latency);
				getCore()->getPerformanceModel()->queuePseudoInstruction(i);
			}

			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, pt_walker_entry.completion_time);

			mmu_dmt_log->debug("[MMU] New time after charging the PT walker completion time: %lu", shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD).getNS());

		}


mmu_dmt_log->debug("[MMU] Total Walk Latency: %lu", total_walk_latency.getNS());
		mmu_dmt_log->debug("[MMU] Total Fault Latency: %lu", total_fault_latency.getNS());

		// ====================================================================
		// PHASE 3: TLB Allocation (on TLB miss)
		// ====================================================================
		// After direct translation, insert result into TLB hierarchy.
		// Evicted entries propagate to higher TLB levels (inclusive policy).

		// instruction path: follows the instruction TLB path
		// data path: follows the data TLB path
		if (instruction)
			tlbs = tlb_subsystem->getInstructionPath();
		else
			tlbs = tlb_subsystem->getDataPath();

		std::map<int, vector<EvictedTranslation>> evicted_translations;

		// We need to allocate the entry in every "allocate on miss" TLB

		int tlb_levels = tlbs.size();

		if (tlb_subsystem->isPrefetchEnabled())
		{
			tlb_levels = tlbs.size() - 1;
			mmu_dmt_log->debug("[MMU] Prefetching is enabled");
		}

		for (int i = 0; i < tlb_levels; i++)
		{
			// We will check where we need to allocate the page

			for (UInt32 j = 0; j < tlbs[i].size(); j++)
			{
				// We need to check if there are any evicted translations from the previous level and allocate them
				if ((i > 0) && (evicted_translations[i - 1].size() != 0))
				{
					TLBAllocResult result;

					mmu_dmt_log->debug("[MMU] There are evicted translations from level: %d", i - 1);
					// iterate through the evicted translations and allocate them in the current TLB
					for (UInt32 k = 0; k < evicted_translations[i - 1].size(); k++)
					{
						const EvictedTranslation& evicted = evicted_translations[i - 1][k];
						mmu_dmt_log->debug("[MMU] Evicted Translation: %lx", evicted.address);
						// We need to check if the TLB supports the page size of the evicted translation
						IntPtr evicted_address = evicted.address;
						int evicted_page_size = evicted.page_size;
						IntPtr evicted_ppn = evicted.ppn;
						if (tlbs[i][j]->supportsPageSize(evicted_page_size))
						{
							
							mmu_dmt_log->debug("[MMU] %s supports page size: %d", tlbs[i][j]->getName().c_str(), evicted_page_size);
							mmu_dmt_log->debug("[MMU] Allocating in TLB: Level = %d Index = %d with page size: %d and VPN: %lx", i, j, evicted_page_size, (evicted.address >> evicted_page_size));

							result = tlbs[i][j]->allocate(evicted_address, time, count, lock, evicted_page_size, evicted_ppn);

							// If the allocation was successful and we have an evicted translation, 
							// we need to add it to the evicted translations vector for	

							if (result.evicted)
							{
								evicted_translations[i].push_back(EvictedTranslation(result.address, result.page_size, result.ppn));
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
					
					mmu_dmt_log->debug("[MMU] %s supports page size: %d", tlbs[i][j]->getName().c_str(), page_size);
					mmu_dmt_log->debug("[MMU] Allocating in TLB: Level = %d Index = %d with page size: %d and VPN: %lx", i, j, page_size, (address >> page_size));
					TLBAllocResult result;

					result = tlbs[i][j]->allocate(address, time, count, lock, page_size, ppn_result);
					if (result.evicted)
					{
						evicted_translations[i].push_back(EvictedTranslation(result.address, result.page_size, result.ppn));
					}
				}

			}
		}

		
		translation_stats.total_translation_latency += charged_tlb_latency + total_walk_latency;

		// ====================================================================
		// PHASE 4: Calculate Physical Address
		// ====================================================================
		// Physical address = PPN * base_page_size + offset

		IntPtr page_size_in_bytes = 1ULL << page_size;      // 2^12 = 4KB or 2^21 = 2MB
		constexpr IntPtr base_page_size_in_bytes = 4096;    // 4KB base

#ifdef DEBUG_MMU
		if(charged_tlb_latency > total_walk_latency && total_walk_latency > SubsecondTime::Zero()){
			std::cout << "[MMU] TLB latency is greater than the page table walk latency" << std::endl;
			
			UInt64  elapsed_time = charged_tlb_latency.getNS();
			UInt64 freq = (UInt64) (core->getDvfsDomain()->getPeriodInFreqMHz());
			//calculate the elapsed cycles - frequency is in MHz
			UInt64 cycles = (UInt64) (elapsed_time * freq / 1000);

			std::cout << "[MMU] TLB latency: " << cycles << " cycles" << std::endl;

			elapsed_time = total_walk_latency.getNS();
			//calculate the elapsed cycles - frequency is in MHz
			cycles = (UInt64) (elapsed_time * freq / 1000);
			std::cout << "[MMU] Page table walk latency: " << cycles << " cycles" << std::endl;
			
		}
#endif
		IntPtr final_physical_address = (ppn_result * base_page_size_in_bytes) + (address % page_size_in_bytes);

		mmu_dmt_log->debug("[MMU] Multiplication factor: %lu", page_size_in_bytes);
		mmu_dmt_log->debug("[MMU] Offset: %lx", (address % page_size_in_bytes));
		mmu_dmt_log->debug("[MMU] Physical Address: %lx PPN: %lx Page Size: %d", final_physical_address, ppn_result*base_page_size_in_bytes, page_size);
		mmu_dmt_log->debug("[MMU] Total translation latency: %lu", (charged_tlb_latency + total_walk_latency).getNS());
		mmu_dmt_log->debug("[MMU] Total fault latency: %lu", total_fault_latency.getNS());
		mmu_dmt_log->debug("[MMU] ---- Ending address translation for virtual address: %lx ----", address);

		// We return the total translation latency and the physical address
		return final_physical_address;
	}

// ============================================================================
// Page Table Walk Support
// ============================================================================

    /**
     * @brief Filter PTW result through Page Walk Cache (PWC).
     *
     * Note: For DMT, this filter is less impactful since only one level
     * is accessed, but it's kept for interface compatibility.
     *
     * @param address      Virtual address being translated
     * @param ptw_result   Raw PTW result from page table
     * @param page_table   Page table being walked
     * @param count        Whether to count statistics
     * @return Filtered PTW result with cached entries removed
     */
	PTWResult MemoryManagementUnitDMT::filterPTWResult(IntPtr address, PTWResult ptw_result, PageTable *page_table, bool count)
	{
		return ptw_filter->filterPTWResult(address, ptw_result, page_table, count);
	}

// ============================================================================
// VMA Management (Unused)
// ============================================================================

    /**
     * @brief Discover Virtual Memory Areas (placeholder).
     *
     * VMA discovery is not used in the current DMT implementation.
     */
	void MemoryManagementUnitDMT::discoverVMAs()
	{
		return ;
	}

} // namespace ParametricDramDirectoryMSI
