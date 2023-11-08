#include "pagetable_radix.h"
#include "pagetable.h"
#include "simulator.h"
#include "physical_memory_allocator.h"
#include "virtuos.h"
// #define DEBUG

namespace ParametricDramDirectoryMSI
{

	PageTableRadix::PageTableRadix(int core_id, String name, int page_sizes, int *page_size_list, int levels, int frame_size, PWC *_pwc)
		: PageTable(core_id, name, page_sizes, page_size_list),
		  levels(levels),
		  m_frame_size(frame_size),
		  pwc(_pwc)
	{

		std::cout << "Creating radix page table" << std::endl;
		bzero(&stats, sizeof(stats));

		registerStatsMetric(name, core_id, "page_faults", &stats.page_faults);
		registerStatsMetric(name, core_id, "page_table_walks", &stats.page_table_walks);
		registerStatsMetric(name, core_id, "ptw_num_cache_accesses", &stats.ptw_num_cache_accesses);
		registerStatsMetric(name, core_id, "pf_num_cache_accesses", &stats.pf_num_cache_accesses);
		registerStatsMetric(name, core_id, "allocated_frames", &stats.allocated_frames);

		stats.page_size_discovery = new UInt64[m_page_sizes];

		for (int i = 0; i < m_page_sizes; i++)
		{
			stats.page_size_discovery[i] = 0;
			registerStatsMetric(name, core_id, "page_size_discovery_" + itostr(i), &stats.page_size_discovery[i]);
		}

		root = (PTFrame *)malloc(sizeof(PTFrame));
		root->entries = (PTEntry *)malloc(sizeof(PTEntry) * m_frame_size);
		root->emulated_ppn = Sim()->getVirtuOS()->getMemoryAllocator()->allocate(4096);
#ifdef DEBUG
		std::cout << "Root frame: " << root << std::endl;
#endif

		for (int i = 0; i < m_frame_size; i++)
		{
			root->entries[i].is_pte = false;
			root->entries[i].data.next_level = NULL;
		}
	}

	PTWResult PageTableRadix::initializeWalk(IntPtr address, bool count)
	{
		bool is_pagefault = false;
		if (count)
			stats.page_table_walks++;
		// SubsecondTime latency = SubsecondTime::Zero();
		AllocatedPage page_fault_result;
	restart_walk: // Get the 9 MSB of the address
		IntPtr offset = (address >> 39) & 0x1FF;
		// print the number in binary format

		PTFrame *current_frame = root;

		// #ifdef DEBUG
		// 		std::cout << "Accessing address: " << current_frame << " at level: " << levels << " with offset: " << offset << std::endl;
		// #endif

		accessedAddresses visited_pts;
		IntPtr ppn_result;
		IntPtr page_size_result;

		int counter = 0; // Stores the depth of the pointer-chasing
		int i = 0;		 // Stores the page table id

		int level = levels;

		while (level > 0)
		{
			offset = (address >> (48 - 9 * (levels - level + 1))) & 0x1FF;
			bool pwc_hit = false;
			IntPtr pwc_address = (IntPtr)(current_frame->emulated_ppn * 4096 + offset);

			if (pwc != NULL && level != 1)
			{ //@kanellok access page walk caches

				pwc_hit = pwc->lookup(pwc_address, SubsecondTime::Zero(), true, levels - (level), count);
			}

			if (!pwc_hit)
			{
				visited_pts.push_back(make_tuple(i, counter, (IntPtr)(current_frame->emulated_ppn * 4096 + offset), current_frame->entries[offset].is_pte && !is_pagefault));
				if (count)
					stats.ptw_num_cache_accesses++;
			}
#ifdef DEBUG
			std::cout << "Accessing address: " << current_frame << " at level: " << level << " with offset: " << offset << std::endl;
#endif
			if (current_frame->entries[offset].is_pte)
			{

				if (current_frame->entries[offset].data.translation.valid == false)
				{
					page_fault_result = handlePageFault(address, count);
					is_pagefault = true;
					goto restart_walk;
				}
				if (count)
					stats.page_size_discovery[level - 1]++;

				ppn_result = current_frame->entries[offset].data.translation.ppn;
				page_size_result = m_page_size_list[level - 1];
				break;
			}
			else
			{
				if (current_frame->entries[offset].data.next_level == NULL)
				{

					page_fault_result = handlePageFault(address, count);
					is_pagefault = true;
					goto restart_walk;
				}
				else
				{
					current_frame = current_frame->entries[offset].data.next_level;
				}
			}

			level--;
			counter++;
		}

		if (is_pagefault)
		{
			visited_pts.insert(visited_pts.end(), get<1>(page_fault_result).begin(), get<1>(page_fault_result).end());
			return PTWResult(get<2>(page_fault_result), visited_pts, get<0>(page_fault_result));
		}
		else
		{
			return PTWResult(page_size_result, visited_pts, ppn_result);
		}
	}

	AllocatedPage PageTableRadix::handlePageFault(IntPtr address, bool count, IntPtr ppn)
	{
		if (count)
			stats.page_faults++;

		PTFrame *current_frame = root;
		IntPtr offset = (address >> 39) & 0x1FF;

		int level = levels;
		int counter = 0;
		int i = 0;

		accessedAddresses pagefault_addresses;
		int page_size = 12;
		int returned_address;

#ifdef DEBUG
		std::cout << "Handling page fault at address: " << address << std::endl;
#endif

		while (level > 0)
		{
			offset = (address >> (48 - 9 * (levels - level + 1))) & 0x1FF;
			pagefault_addresses.push_back(make_tuple(i, counter, (IntPtr)(current_frame->emulated_ppn * 4096 + offset), false));
			if (count)
				stats.pf_num_cache_accesses++;

			if (current_frame->entries[offset].is_pte)
			{
				int page_size = m_page_size_list[level - 1];

				IntPtr physical_address;

				if (ppn != -1)
				{
					physical_address = ppn;
				}
				else
				{
					physical_address = Sim()->getVirtuOS()->getMemoryAllocator()->allocate(4096, address, core_id);
				}
#ifdef DEBUG
				std::cout << "Allocating physical page at entry: " << &current_frame->entries[offset] << " physical address:" << physical_address << std::endl;
#endif
				current_frame->entries[offset].data.translation.valid = true;
				current_frame->entries[offset].data.translation.ppn = physical_address;
				returned_address = physical_address;
				break;
			}

			if (current_frame->entries[offset].data.next_level == NULL)
			{
				// Allocate a new page table
				PTFrame *new_pt_frame = new PTFrame;

				stats.allocated_frames++;

				IntPtr physical_address = Sim()->getVirtuOS()->getMemoryAllocator()->allocate(4096);

#ifdef DEBUG
				std::cout << "Allocating new page table frame at level: " << (level - 1) << " address: " << new_pt_frame << " and emulated address:" << physical_address << "in offset: " << offset << std::endl;
#endif
				new_pt_frame->emulated_ppn = physical_address;
				new_pt_frame->entries = new PTEntry[m_frame_size];
				for (int i = 0; i < m_frame_size; i++)
				{
					if (level == 2)
					{
						new_pt_frame->entries[i].is_pte = true;
						new_pt_frame->entries[i].data.translation.valid = false;
					}
					else
					{
						new_pt_frame->entries[i].is_pte = false;
						new_pt_frame->entries[i].data.next_level = NULL;
					}
				}
				current_frame->entries[offset].data.next_level = new_pt_frame;
				current_frame = new_pt_frame;
			}
			else
			{
				current_frame = current_frame->entries[offset].data.next_level;
			}
			level--;
			counter++;
		}

		return AllocatedPage(returned_address, pagefault_addresses, page_size);
	}
	void PageTableRadix::insertLargePage(IntPtr address, IntPtr ppn)
	{
		PTFrame *current_frame = root;
		IntPtr offset = (address >> 39) & 0x1FF;

		int level = levels;
		int counter = 0;
		int i = 0;

		accessedAddresses pagefault_addresses;
		int page_size = 12;
		int returned_address;

#ifdef DEBUG
		std::cout << "Handling page fault at address: " << address << std::endl;
#endif

		while (level > 0)
		{
			offset = (address >> (48 - 9 * (levels - level + 1))) & 0x1FF;
			if (level == 2)
			{
				int page_size = m_page_size_list[level - 1];
				IntPtr physical_address;
#ifdef DEBUG
				std::cout << "Allocating physical page at entry: " << &current_frame->entries[offset] << "physical address:" << physical_address << std::endl;
#endif
				current_frame->entries[offset].is_pte = true;
				current_frame->entries[offset].data.translation.valid = true;
				current_frame->entries[offset].data.translation.ppn = ppn;
				break;
			}

			if (current_frame->entries[offset].data.next_level == NULL)
			{
				// Allocate a new page table
				PTFrame *new_pt_frame = new PTFrame;
				stats.allocated_frames++;

				IntPtr physical_address = Sim()->getVirtuOS()->getMemoryAllocator()->allocate(4096);

#ifdef DEBUG
				std::cout << "Allocating new page table frame at level: " << (level - 1) << " address: " << new_pt_frame << " and emulated address:" << physical_address << "in offset: " << offset << std::endl;
#endif

				new_pt_frame->entries = new PTEntry[m_frame_size];
				for (int i = 0; i < m_frame_size; i++)
				{
					if (level == 2)
					{
						new_pt_frame->entries[i].is_pte = true;
						new_pt_frame->entries[i].data.translation.valid = false;
					}
					else
					{
						new_pt_frame->entries[i].is_pte = false;
						new_pt_frame->entries[i].data.next_level = NULL;
					}
				}
				current_frame->entries[offset].data.next_level = new_pt_frame;
				current_frame = new_pt_frame;
			}
			else
			{
				current_frame = current_frame->entries[offset].data.next_level;
			}
			level--;
			counter++;
		}
	}
}