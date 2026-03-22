// ============================================================================
// MMU Range-Based Translation Implementation
// ============================================================================
//
// This file implements an MMU variant with Range-Based Translation for the
// Sniper multi-core simulator. Range translation coalesces multiple page
// translations into a single range entry, reducing TLB pressure for
// contiguous memory allocations.
//
// Key Features:
// ~~~~~~~~~~~~~
//   1. Standard hardware TLB hierarchy (L1 iTLB, L1 dTLB, L2 TLB)
//   2. Range Lookaside Buffer (RLB) for contiguous memory regions
//   3. Range Table for storing VPN → PPN+offset mappings
//   4. Falls back to page table walk only on RLB and TLB miss
//
// Architecture Overview:
// ~~~~~~~~~~~~~~~~~~~~~~
//   +--------+     +-----------+     +------------+
//   |  Core  | --> | RangeMMU  | --> |   Memory   |
//   +--------+     +-----------+     +------------+
//                       |
//         +-------------+-------------+
//         |             |             |
//    +----v----+   +----v----+   +----v----+
//    |   TLB   |   |   RLB   |   |   PTW   |
//    | Subsys  |   | (Range) |   |         |
//    +---------+   +---------+   +---------+
//
// Translation Flow:
// ~~~~~~~~~~~~~~~~~
//   1. Lookup hardware TLB hierarchy
//   2. On TLB miss: Lookup RLB for contiguous range match
//   3. On RLB miss: Lookup Range Table (may access memory)
//   4. On Range Table miss: Perform full page table walk
//   5. Allocate translation in TLBs and RLB
//
// Range Entry Structure:
// ~~~~~~~~~~~~~~~~~~~~~~
//   - VPN: Starting virtual page number of the range
//   - Bounds: Number of pages in the contiguous range
//   - Offset: Physical page number offset for translation
//   - Translation: PPN = (current_VPN - range_VPN) + offset
//
// Reference:
// ~~~~~~~~~~
//   Range Translation supports direct-segment and subblock coalescing
//   optimizations similar to CoLT and RMM designs.
//
// ============================================================================

#include "mmu_range.h"
#include "memory_manager.h"
#include "cache_cntlr.h"
#include "subsecond_time.h"
#include "fixed_types.h"
#include "core.h"
#include "thread.h"
#include "mmu_base.h"
#include "misc/exception_handler_base.h"
// sniper_space_exception_handler.h no longer needed — using base-class interface
#include <iostream>
#include <fstream>
#include <algorithm>
#include "rangelb.h"
#include "simulator.h"
#include "../../../../../include/memory_management/physical_memory_allocators/physical_memory_allocator.h"
#include "mimicos.h"
#include "../../../../../include/memory_management/misc/vma.h"
#include "filter_factory.h"

// #define DEBUG_MMU  // Uncomment for verbose debug logging

using namespace std;

namespace ParametricDramDirectoryMSI
{

// ============================================================================
// Construction / Destruction
// ============================================================================

    /**
     * @brief Construct the Range-based MMU.
     *
     * Initializes an MMU with Range Translation support that coalesces
     * multiple page translations into range entries for reduced TLB pressure.
     *
     * @param _core           Pointer to the core this MMU belongs to
     * @param _memory_manager Memory manager for cache access
     * @param _shmem_perf_model Performance model for timing
     * @param _name           Configuration name prefix
     * @param _nested_mmu     Optional nested MMU for virtualization
     */
	RangeMMU::RangeMMU(Core *_core, MemoryManagerBase *_memory_manager, ShmemPerfModel *_shmem_perf_model, String _name, MemoryManagementUnitBase *_nested_mmu)
		: MemoryManagementUnitBase(_core, _memory_manager, _shmem_perf_model, _name, _nested_mmu)
	{
		// SimLog for debug output
		mmu_range_log = new SimLog("MMU_RANGE", core->getId(), DEBUG_MMU_RANGE);

		instantiateRangeTableWalker();  // Creates RLB for range lookups
		instantiatePageTableWalker();   // Creates PWC filter and PTW MSHRs
		instantiateTLBSubsystem();      // Creates TLB hierarchy
		registerMMUStats();             // Registers statistics with Sniper
	}

    /**
     * @brief Destroy the Range-based MMU.
     *
     * Cleans up all allocated resources including TLBs and RLB.
     */
	RangeMMU::~RangeMMU()
	{
		delete mmu_range_log;
		delete tlb_subsystem;
		delete range_lb;
	}

// ============================================================================
// Initialization Methods
// ============================================================================

    /**
     * @brief Initialize the Range Lookaside Buffer (RLB).
     *
     * Creates the RLB which caches range entries (VPN → PPN mappings for
     * contiguous memory regions). Configuration includes:
     * - Number of sets (configurable via perf_model/mmu/rlb/num_sets)
     * - Access latency (configurable via perf_model/mmu/rlb/latency)
     */
	void RangeMMU::instantiateRangeTableWalker()
	{
		int num_sets = Sim()->getCfg()->getInt("perf_model/" + name + "/rlb/num_sets");
		ComponentLatency latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/" + name + "/rlb/latency"));
		range_lb = new RLB(core, "rlb", latency, num_sets);
	}

    /**
     * @brief Initialize the Page Table Walker and Page Walk Cache.
     *
     * Creates the PWC filter for caching intermediate page table entries
     * and the MSHR structure for tracking concurrent page table walks.
     */
	void RangeMMU::instantiatePageTableWalker()
	{
		String mimicos_name = Sim()->getMimicOS()->getName();
		String page_table_type = Sim()->getCfg()->getString("perf_model/" + mimicos_name + "/page_table_type");
		String page_table_name = Sim()->getCfg()->getString("perf_model/" + mimicos_name + "/page_table_name");

		// Create Page Walk Cache filter
		String filter_type = Sim()->getCfg()->getString("perf_model/" + name + "/ptw_filter_type");
		ptw_filter = FilterPTWFactory::createFilterPTWBase(filter_type, name, core);

		// Create MSHRs for N concurrent page table walks
		pt_walkers = new MSHR(Sim()->getCfg()->getInt("perf_model/" + name + "/page_table_walkers"));
	}

    /**
     * @brief Initialize the TLB hierarchy.
     *
     * Creates the standard hardware TLB hierarchy (L1 iTLB, L1 dTLB, L2 TLB, etc.)
     */
	void RangeMMU::instantiateTLBSubsystem()
	{
		tlb_subsystem = new TLBHierarchy(name, core, memory_manager, shmem_perf_model);
	}

// ============================================================================
// Statistics Registration
// ============================================================================

    /**
     * @brief Register all MMU statistics with Sniper's statistics framework.
     *
     * Statistics include:
     * - num_translations: Total address translations performed
     * - requests_resolved_by_rlb: Translations satisfied by RLB hit
     * - requests_resolved_by_rlb_latency: Cumulative RLB hit latency
     * - total_range_walk_latency: Cumulative range table walk time
     * - page_table_walks: Number of full page table walks
     * - total_walk_latency: Cumulative PTW time
     * - page_faults: Pages not present in memory
     * - total_fault_latency: Time handling page faults
     * - total_tlb_latency: Cumulative TLB lookup time
     * - requests_resolved_by_tlb_latency: Cumulative TLB hit latency
     * - total_translation_latency: End-to-end translation time
     */
	void RangeMMU::registerMMUStats()
	{
		bzero(&translation_stats, sizeof(translation_stats));

		translation_stats.tlb_latency_per_level = new SubsecondTime[tlb_subsystem->getTLBSubsystem().size()];

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

// ============================================================================
// Address Translation - Core MMU Operation
// ============================================================================

    /**
     * @brief Perform virtual-to-physical address translation with range support.
     *
     * This is the main entry point for address translation. The Range MMU
     * extends the standard translation flow with Range Lookaside Buffer lookups.
     *
     * ┌─────────────────────────────────────────────────────────────────────┐
     * │                    RANGE TRANSLATION FLOW                            │
     * ├─────────────────────────────────────────────────────────────────────┤
     * │                                                                      │
     * │   Virtual Addr ──► TLB Lookup ──┬──► HIT ──► Physical Address        │
     * │                                 │                                    │
     * │                                 └──► MISS                            │
     * │                                       │                              │
     * │                                       ▼                              │
     * │                                  RLB Lookup                          │
     * │                                       │                              │
     * │                              +--------+--------+                     │
     * │                              ▼                 ▼                     │
     * │                            HIT              MISS                     │
     * │                              │                 │                     │
     * │                              │                 ▼                     │
     * │                              │         Range Table Lookup            │
     * │                              │                 │                     │
     * │                              │         +-------+-------+             │
     * │                              │         ▼               ▼             │
     * │                              │       HIT            MISS             │
     * │                              │         │               │             │
     * │                              │         │               ▼             │
     * │                              │         │       Page Table Walk       │
     * │                              │         │               │             │
     * │                              +---------+-------+-------+             │
     * │                                        │                             │
     * │                                        ▼                             │
     * │                               Physical Address                       │
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
	IntPtr RangeMMU::performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count)
	{
		// Track DRAM accesses for power/performance analysis
		dram_accesses_during_last_walk = 0;

		mmu_range_log->debug("");
		mmu_range_log->debug("[MMU] ---- Starting address translation for virtual address: %lx ---- at time %lu", address, shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD).getNS());

		// Find the VMA containing this address for range tracking
		findVMA(address);

		SubsecondTime time = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

		// Cleanup completed PTW entries from MSHR
		pt_walkers->removeCompletedEntries(time);

		if (count)
			translation_stats.num_translations++;

		// Get reference to TLB hierarchy
		TLBSubsystem tlbs = tlb_subsystem->getTLBSubsystem();

		// ====================================================================
		// PHASE 1: TLB Hierarchy Lookup
		// ====================================================================

		bool tlb_hit = false;
		TLB *hit_tlb = NULL;
		CacheBlockInfo *tlb_block_info_hit = NULL;
		CacheBlockInfo *tlb_block_info = NULL;
		int hit_level = -1;
		int page_size = -1;
		IntPtr ppn_result;

		// Search hardware TLB hierarchy
		for (UInt32 i = 0; i < tlbs.size(); i++)
		{
			mmu_range_log->debug("[MMU] Searching TLB at level: %u", i);
			for (UInt32 j = 0; j < tlbs[i].size(); j++)
			{
				bool tlb_stores_instructions = (tlbs[i][j]->getType() == TLBtype::Instruction) || (tlbs[i][j]->getType() == TLBtype::Unified);

				if (tlb_stores_instructions && instruction)
				{

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
				mmu_range_log->debug("[MMU] TLB Hit at level: %d at TLB %s", hit_level, hit_tlb->getName().c_str());
				break;
			}
		}

		SubsecondTime charged_tlb_latency = SubsecondTime::Zero();

		// If we have a TLB hit, we need to charge the TLB hit latency
		if (tlb_hit)
		{
			mmu_range_log->debug("[MMU] TLB Hit ? %d at level: %d at TLB: %s", tlb_hit, hit_level, hit_tlb->getName().c_str());

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
				mmu_range_log->debug("[MMU] Charging TLB Latency: %lu at level: %d", tlb_latency[i].getNS(), i);
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

					mmu_range_log->debug("[MMU] Charging TLB Hit Latency: %lu at level: %d", hit_tlb->getLatency().getNS(), hit_level);
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
			mmu_range_log->debug("[MMU] New time after charging TLB latency: %lu", shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD).getNS());
		}

		// If we have a TLB miss, we need to charge the TLB latency based on the slowest component
		// at each level of the hierarchy

		SubsecondTime tlb_latency[tlbs.size()];

		// If we have a TLB miss, we need to charge the TLB latency based on the slowest component
		// at each level of the hierarchy, across all levels of the hierarchy

		if (!tlb_hit)
		{
			mmu_range_log->debug("[MMU] TLB Miss");
			for (UInt32 i = 0; i < tlbs.size(); i++)
			{
				for (UInt32 j = 0; j < tlbs[i].size(); j++)
				{
					tlb_latency[i] = max(tlbs[i][j]->getLatency(), tlb_latency[i]);
				}
				mmu_range_log->debug("[MMU] Charging TLB Latency: %lu at level: %u", tlb_latency[i].getNS(), i);
				translation_stats.total_tlb_latency += tlb_latency[i];
				charged_tlb_latency += tlb_latency[i];
			}

			if(count) translation_stats.total_translation_latency += charged_tlb_latency;

			// We progress the time by the charged TLB latency so that the PTW starts after the TLB latency
			shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time + charged_tlb_latency);
			mmu_range_log->debug("[MMU] New time after charging TLB latency: %lu", shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD).getNS());
		}

		// ====================================================================
		// PHASE 2: Range Lookaside Buffer (RLB) Lookup
		// ====================================================================
		// On TLB miss, check if address falls within a cached range entry.
		// Range entries coalesce multiple page translations into one.

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
				mmu_range_log->debug("[MMU] Range Hit: %d at time: %lu", range_hit, shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD).getNS());
				mmu_range_log->debug("[MMU] VPN Start: %lx", vpn_start);
				mmu_range_log->debug("[MMU] PPN Offset: %lx", ppn_offset);
				mmu_range_log->debug("[MMU] Current VPN: %lx", current_vpn);
				mmu_range_log->debug("[MMU] Final PPN: %lx", (current_vpn - vpn_start) + ppn_offset);
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
				mmu_range_log->debug("[MMU] New time after charging TLB and Range latency: %lu", shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD).getNS());
			}
			else{
				if (count)
				{
					translation_stats.total_translation_latency += range_lb->get_latency().getLatency();
				}
				mmu_range_log->debug("[MMU] Range Miss: %d at time: %lu", range_hit, shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD).getNS());
				// We progress the time by the max of the TLB latency and the range latency
				shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time + max(range_latency, charged_tlb_latency));
				mmu_range_log->debug("[MMU] New time after charging TLB and Range latency: %lu", shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD).getNS());
			}
		}

		SubsecondTime total_walk_latency = SubsecondTime::Zero();
		SubsecondTime total_fault_latency = SubsecondTime::Zero();

		// ====================================================================
		// PHASE 3: Page Table Walk (PTW) - Fallback on TLB and RLB Miss
		// ====================================================================
		// If both TLB and RLB miss, perform full page table walk.
		// Includes page fault handling with retry loop.

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
			mmu_range_log->debug("[MMU] New time after charging the PT walker allocation delay: %lu", shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD).getNS());

			// returns PTW latency, PF latency, Physical Address, Page Size as a tuple
			int app_id = core->getThread()->getAppId();
			PageTable *page_table = Sim()->getMimicOS()->getPageTable(app_id);
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
				total_walk_latency = ptw_result.latency; // Total walk latency is only the time it takes to walk the page table (excluding page faults)

				shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, time_for_pt + delay + total_walk_latency);

				if (count)
				{
					translation_stats.total_walk_latency += total_walk_latency;
					translation_stats.page_table_walks++;
				}

				// If the page fault latency is greater than zero, we need to charge the page fault latency
				caused_page_fault = ptw_result.page_fault;

				// If the PTW caused a page fault, we need to handle it
				if (caused_page_fault)
				{
					had_page_fault = true;  // Track that a fault occurred
					mmu_range_log->debug("[RangeMMU] Page Fault has occured");
					translation_stats.page_faults++;
					
					// Handle page fault at MMU level (sniper-space mode)
					if (!userspace_mimicos_enabled)
					{
						mmu_range_log->debug("[RangeMMU] Handling page fault in sniper-space mode, calling exception handler");
						ExceptionHandlerBase *handler = Sim()->getCoreManager()->getCoreFromID(core->getId())->getExceptionHandler();
						ExceptionHandlerBase::FaultCtx fault_ctx{};
						fault_ctx.vpn = address >> 12;
						fault_ctx.page_table = page_table;
						fault_ctx.alloc_in.metadata_frames = ptw_result.requested_frames;
						handler->handle_page_fault(fault_ctx);
						
						mmu_range_log->debug("[RangeMMU] Page fault handled, restarting PTW for address: %lx", address);
						// Loop will retry PTW after fault is handled
					}
				}
			} while (caused_page_fault && !userspace_mimicos_enabled);
			
			// Calculate fault latency for timing (use had_page_fault to survive loop retry)
			if (had_page_fault && !userspace_mimicos_enabled)
			{
				SubsecondTime m_page_fault_latency = Sim()->getMimicOS()->getPageFaultLatency();
				mmu_range_log->debug("[RangeMMU] Charging Page Fault Latency: %lu", m_page_fault_latency.getNS());
				total_fault_latency = m_page_fault_latency;
				if (count)
				{
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
			mmu_range_log->debug("[MMU] Charging PT Walker Completion Time: %lu", (time_for_pt + delay + total_walk_latency + total_fault_latency).getNS());
			pt_walker_entry.completion_time = time_for_pt + delay + total_walk_latency + total_fault_latency;
			pt_walkers->allocate(pt_walker_entry);

			/*
			We need to set the time to the time after the PTW is completed.
			This is done so that the memory manager sends the request to the cache hierarchy after the PTW is completed
			*/

			if (had_page_fault && !userspace_mimicos_enabled)
			{
				PseudoInstruction *i = new PageFaultRoutineInstruction(total_fault_latency);
				getCore()->getPerformanceModel()->queuePseudoInstruction(i);
				shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, pt_walker_entry.completion_time);
			}
			else
			{
				shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, pt_walker_entry.completion_time);
			}

			ppn_result = ptw_result.ppn;
			page_size = ptw_result.page_size;
			mmu_range_log->debug("[RangeMMU] Final PPN: %lx", ppn_result);
			mmu_range_log->debug("[RangeMMU] Final Page Size: %d", page_size);

			mmu_range_log->debug("[MMU] New time after charging the PT walker completion time: %lu ns", shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD).getNS());
		}

		// ====================================================================
		// PHASE 4: TLB Allocation (on TLB miss)
		// ====================================================================
		// After PTW or range lookup, insert the translation into TLB hierarchy.
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
			mmu_range_log->debug("[MMU] Prefetching is enabled");
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

					mmu_range_log->debug("[MMU] There are evicted translations from level: %d", i - 1);
					// iterate through the evicted translations and allocate them in the current TLB
					for (UInt32 k = 0; k < evicted_translations[i - 1].size(); k++)
					{
						const EvictedTranslation& evicted = evicted_translations[i - 1][k];
						mmu_range_log->debug("[MMU] Evicted Translation: %lx", evicted.address);
						// We need to check if the TLB supports the page size of the evicted translation
						IntPtr evicted_address = evicted.address;
						int evicted_page_size = evicted.page_size;
						IntPtr evicted_ppn = evicted.ppn;
						
						if (tlbs[i][j]->supportsPageSize(evicted_page_size))
						{
							mmu_range_log->debug("[MMU] Allocating evicted entry in TLB: Level = %d Index =  %d", i, j);

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

				if (tlbs[i][j]->supportsPageSize(page_size) && tlbs[i][j]->getAllocateOnMiss() && (!tlb_hit || (tlb_hit && hit_level > i)))
				{
					mmu_range_log->debug("[MMU] %s supports page size: %d", tlbs[i][j]->getName().c_str(), page_size);
					mmu_range_log->debug("[MMU] Allocating in TLB: Level = %d Index = %d with page size: %d and VPN: %lx", i, j, page_size, (address >> page_size));
					TLBAllocResult result;

					result = tlbs[i][j]->allocate(address, time, count, lock, page_size, ppn_result);
					if (result.evicted)
					{
						evicted_translations[i].push_back(EvictedTranslation(result.address, result.page_size, result.ppn));
					}
				}
			}
		}

		// ====================================================================
		// PHASE 5: Calculate Physical Address
		// ====================================================================

		constexpr IntPtr base_page_size = 4096;  // 4KB base page
		IntPtr physical_address = ppn_result * base_page_size + (address % page_size);

		return physical_address;
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
	PTWResult RangeMMU::filterPTWResult(IntPtr address, PTWResult ptw_result,
										PageTable *page_table,
										bool count)
	{
		return ptw_filter->filterPTWResult(address, ptw_result, page_table, count);
	}

// ============================================================================
// Range Walk Implementation
// ============================================================================

    /**
     * @brief Perform Range Lookaside Buffer and Range Table lookup.
     *
     * Attempts to translate a virtual address using range entries. First checks
     * the RLB (hardware cache), then falls back to the Range Table in memory.
     *
     * Range Translation Math:
     *   Given range entry: (vpn_start, bounds, offset)
     *   For address with VPN:
     *     If vpn_start <= VPN < vpn_start + bounds:
     *       PPN = (VPN - vpn_start) + offset
     *
     * @param address  Virtual address to translate
     * @param eip      Instruction pointer
     * @param lock     Cache coherence lock signal
     * @param modeled  Whether to model timing
     * @param count    Whether to update statistics
     * @return Tuple of (latency, vpn_start, offset) or (-1, -1) on miss
     */
	std::tuple<SubsecondTime, IntPtr, int> RangeMMU::performRangeWalk(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count)
	{
		SubsecondTime charged_range_walk_latency = SubsecondTime::Zero();
		
		// Check RLB first (hardware cache of range entries)
		auto hit_rlb = range_lb->access(Core::mem_op_t::READ, address, count);

		RangeTable *range_table = Sim()->getMimicOS()->getRangeTable(core->getThread()->getAppId());

		if (!hit_rlb.first)  // RLB miss - check Range Table
		{
			mmu_range_log->debug("Miss in RLB for address: %lx", address);
			// Lookup in software Range Table
			auto result = range_table->lookup(address);
			
			if (get<0>(result) != NULL)  // Found in Range Table
			{
				mmu_range_log->debug("Key found for address: %lx in the range table", address);
				// Build range entry from table lookup
				Range range;
				range.vpn = get<0>(result)->keys[get<1>(result)].first;
				range.bounds = get<0>(result)->keys[get<1>(result)].second;
				range.offset = get<0>(result)->values[get<1>(result)].offset;

				mmu_range_log->debug("VPN: %lx Bounds: %lx Offset: %lx", range.vpn, range.bounds, range.offset);
				// Charge memory access latency for range table nodes visited
				for (auto &addr : get<2>(result))
				{
					HitWhere::where_t hit_where_temp = HitWhere::UNKNOWN;
					translationPacket packet;
					packet.address = addr;
					packet.eip = eip;
					packet.instruction = false;
					packet.lock_signal = lock;
					packet.modeled = modeled;
					packet.count = count;
					// Range table metadata accesses are treated as page table data
					packet.type = CacheBlockInfo::block_type_t::PAGE_TABLE_DATA;

					charged_range_walk_latency += accessCache(packet, charged_range_walk_latency, false, hit_where_temp);
				}
				
				// Insert entry into RLB for future lookups
				range_lb->insert_entry(range);
			}
			else  // Not found in Range Table - need full PTW
			{
				mmu_range_log->debug("No key found for address: %lx in the range table", address);
				return std::make_tuple(charged_range_walk_latency, -1, -1);
			}
			return std::make_tuple(charged_range_walk_latency, hit_rlb.second.vpn, hit_rlb.second.offset);
		}
		else  // RLB hit
		{
			mmu_range_log->debug("Hit in RLB for address: %lx", address);
			mmu_range_log->debug("VPN: %lx Bounds: %lx Offset: %lx", hit_rlb.second.vpn, hit_rlb.second.bounds, hit_rlb.second.offset);
			return std::make_tuple(charged_range_walk_latency, hit_rlb.second.vpn, hit_rlb.second.offset);
		}
	}

// ============================================================================
// VMA Management
// ============================================================================

    /**
     * @brief Discover Virtual Memory Areas (VMAs).
     *
     * Hook for OS-level VMA discovery. VMAs define contiguous virtual memory
     * regions that can be candidates for range translation.
     */
	void RangeMMU::discoverVMAs()
	{
		// Placeholder for VMA discovery logic
	}

    /**
     * @brief Find the VMA containing a virtual address.
     *
     * Searches through the application's VMA list to find which region
     * contains the given virtual address. Used for range tracking.
     *
     * @param address Virtual address to look up
     * @return VMA containing the address (asserts if not found)
     */
	VMA RangeMMU::findVMA(IntPtr address)
	{
		int app_id = core->getThread()->getAppId();
		std::vector<VMA> vma_list = Sim()->getMimicOS()->getVMA(app_id);

		for (UInt32 i = 0; i < vma_list.size(); i++)
		{
			if (address >= vma_list[i].getBase() && address < vma_list[i].getEnd())
			{
				mmu_range_log->debug("VMA found for address: %lx in VMA: %lx - %lx", address, vma_list[i].getBase(), vma_list[i].getEnd());
				return vma_list[i];
			}
		}
		assert(false);  // Address must belong to some VMA
		return VMA(-1, -1);
	}

} // namespace ParametricDramDirectoryMSI
