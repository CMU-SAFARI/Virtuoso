#include "simple_thp_allocator.h"
#include <utility>
#include <cmath>
#include <iostream>
#include <fstream>
#include <list>
#include <vector>
#include "config.hpp"
#include "simulator.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <tuple>
#include "stats.h"
#include "core_manager.h"
#include "memory_manager.h"
// #define DEBUG_SIMPLE_THP

using namespace std;

SimpleTHPAllocator::SimpleTHPAllocator(int max_order) : m_max_order(max_order)
{
	for (int i = 0; i < m_max_order + 1; i++)
	{
		std::vector<std::tuple<UInt64, UInt64, bool, UInt64>> vec; // Block start, Block end
		free_list.push_back(vec);
	}
	init();
	UInt64 dummy = 0;
	registerStatsMetric("simple_thp", 0, "fragmentation", &dummy, true);
	registerStatsMetric("simple_thp", 0, "two_mb_util_ratio", &dummy, true);
	registerStatsMetric("simple_thp", 0, "two_mb_reserved", &stats.two_mb_reserved);
	registerStatsMetric("simple_thp", 0, "two_mb_promoted", &stats.two_mb_promoted);
	registerStatsMetric("simple_thp", 0, "two_mb_demoted", &stats.two_mb_demoted);
}

void SimpleTHPAllocator::init()
{
	std::cout << "------ [VirtuOS:SimpleTHP] Initializing SimpleTHP allocator ------" << std::endl;
	std::cout << std::endl;
	std::cout << "[VirtuOS:SimpleTHP] Memory Size: " << m_memory_size << std::endl;

	UInt64 pages_in_block = pow(2, m_max_order); // start with max
	UInt64 current_order = m_max_order;
	UInt64 total_mem_in_pages = m_memory_size * 1024 / 4;
	m_total_pages = total_mem_in_pages;
	m_free_pages = m_total_pages;
	UInt64 available_mem_in_pages = total_mem_in_pages;
	UInt64 current_free = 0;

	std::cout << "[VirtuOS:SimpleTHP] 4KB pages in memory: " << total_mem_in_pages << std::endl;
	std::cout << "[VirtuOS:SimpleTHP] 2MB pages in memory: " << total_mem_in_pages / 512 << std::endl;

	while (current_free < total_mem_in_pages)
	{
		while (available_mem_in_pages >= pages_in_block)
		{
			free_list[current_order].push_back(std::make_tuple(current_free, current_free + pages_in_block - 1, false, -1));
			current_free += pages_in_block;
			available_mem_in_pages -= pages_in_block;
		}
#ifdef DEBUG_SIMPLE_THP
		std::cout << "[VirtuOS:SimpleTHP] Order " << current_order << " has " << free_list[current_order].size() << " blocks" << std::endl;
		current_order--;
#endif
		pages_in_block = pow(2, current_order);
	}
}
void demote(UInt64 region)
{
}

UInt64 SimpleTHPAllocator::allocate(UInt64 bytes, UInt64 address, UInt64 core_id)
{

#ifdef DEBUG_SIMPLE_THP
	std::cout << "[VirtuOS:SimpleTHP] Allocating " << bytes << " bytes for address: " << address << std::endl;
#endif
	UInt64 region_begin; // return address
	UInt64 region_2MB;

	if (address != 0) // This means that the application is asking for an allocation because of a page fault
	{
		region_2MB = address >> 21; // Find the 2MB region that the address belongs to
		int offset_in_2MB = (address >> 12) & 0x1FF;
		if (virtual_map.find(region_2MB) != virtual_map.end()) // Check how many pages have been allocated in the reserved 2MB region
		{

			get<1>(virtual_map[region_2MB])[offset_in_2MB] = 1; // Mark the page as allocated inside the 512 sets
#ifdef DEBUG_SIMPLE_THP
			std::cout << "[VirtuOS:SimpleTHP] Found a 2MB region that has been allocated: " << get<0>(virtual_map[region_2MB]) << std::endl;
			std::cout << "[VirtuOS:SimpleTHP] Marked the page as allocated inside the 512 sets" << std::endl;
#endif

			double utilization = static_cast<double>(get<1>(virtual_map[region_2MB]).count()) / 512; // Assuming 512 pages in a 2MB region find the utilization

			if (utilization > 0.7 && !(get<2>(virtual_map[region_2MB])))
			{
				stats.two_mb_promoted++;
				get<2>(virtual_map[region_2MB]) = true; // Mark the 2MB region as promoted
														// mmu->promote(address); // Promote the 2MB region in the mmu so that the page table gets updated
				// std::cout << "Promote\n";																																																			  // Mark the 2MB region as promoted
				((ParametricDramDirectoryMSI::MemoryManager *)Sim()->getCoreManager()->getCoreFromID(core_id)->getMemoryManager())->getMMU()->getPageTable()->promotePage(address, get<1>(virtual_map[region_2MB]), get<0>(virtual_map[region_2MB])); // Promote the 2MB region in the page table so

				// ParametricDramDirectoryMSI::MemoryManager *mm = (ParametricDramDirectoryMSI::MemoryManager *)(Sim()->getCoreManager()->getCoreFromID(core_id)->getMemoryManager());
			}
			else
			{
				return get<0>(virtual_map[region_2MB]) + offset_in_2MB; // Return the address of the page
			}
		}
		else
		{
#ifdef DEBUG_SIMPLE_THP
			std::cout << "[VirtuOS:SimpleTHP] No 2MB region is allocated: proceed by reserving a 2MB region" << std::endl;
#endif
			// if we havent allocated any pages in the 2MB region, we need to reserve a 2MB region
			if (free_list[9].size() > 0) // Is there any free 2MB region?
			{
				stats.two_mb_reserved++;
				std::tuple<UInt64, UInt64, bool, UInt64> temp = free_list[9][0];

#ifdef DEBUG_SIMPLE_THP
				std::cout << "[VirtuOS:SimpleTHP] Reserved at order 9 at page: " << get<0>(temp) << std::endl;
#endif
				free_list[9].erase(free_list[9].begin());
				m_free_pages -= pow(2, 9);

				allocated_map[get<0>(temp)] = get<1>(temp) - get<0>(temp) + 1;						// Add the 2MB region to the allocated map
				region_begin = get<0>(temp);														// We will return this address
				virtual_map[region_2MB] = std::make_tuple(region_begin, std::bitset<512>(), false); // virtual map stores the starting address of the 2MB region
				get<1>(virtual_map[region_2MB])[offset_in_2MB] = 1;									// Mark the page as allocated inside the 512 sets
				return region_begin + offset_in_2MB;												// Return the address of the page
			}
			else // try to demote larger blocks
			{

				int i;
				bool allocate_4KB = false;
				for (i = 9; i <= m_max_order; i++)
				{
					if (free_list[i].size() != 0)
						break;
				}
				if (i == m_max_order + 1)
				{
#ifdef DEBUG_SIMPLE_THP

					std::cout << "[VirtuOS:SimpleTHP] No free 2MB region and no free blocks to demote - we need to go for 4KB page" << std::endl;
					allocate_4KB = true;
#endif
				}
				else
				{

					std::tuple<UInt64, UInt64, bool, UInt64> temp;

					temp = free_list[i][0];
					free_list[i].erase(free_list[i].begin());
					i--;

					while (i >= 9)
					{
#ifdef DEBUG_SIMPLE_THP
						std::cout << "Searching for new order: " << i << std::endl;
#endif

						std::tuple<UInt64, UInt64, bool, UInt64> pair1, pair2;

						pair1 = std::make_tuple(get<0>(temp), get<0>(temp) + (get<1>(temp) - get<0>(temp)) / 2, false, -1);
						pair2 = std::make_tuple(get<0>(temp) + (get<1>(temp) - get<0>(temp) + 1) / 2, get<1>(temp), false, -1);

						free_list[i].push_back(pair1);
						free_list[i].push_back(pair2);
						temp = free_list[i][0];
						free_list[i].erase(free_list[i].begin());

						i--;
					}
					allocated_map[get<0>(temp)] = get<1>(temp) - get<0>(temp) + 1;
#ifdef DEBUG_SIMPLE_THP
					std::cout << "[SimpleTHP Allocator] Reserved at order " << 9 << " with size " << get<1>(temp) - get<0>(temp) + 1 << " at address " << get<0>(temp) << std::endl;
					std::cout << "[SimpleTHP Allocator] I will return the 4KB region until the 2MB region is allocated" << std::endl;
#endif

					region_begin = get<0>(temp);
					m_free_pages -= pow(2, 9);
					return region_begin;
				}

				if (allocate_4KB)
				{
#ifdef DEBUG_SIMPLE_THP
					std::cout << "[VirtuOS:SimpleTHP] No free 2MB region and no free blocks to demote - we need to go for smaller page size" << std::endl;
#endif

					int i;
					int ind = 0;
					for (i = ind; i <= m_max_order; i++)
					{
						if (free_list[i].size() != 0)
							break;
					}
					if (i == m_max_order + 1)
					{
						std::cout << "[VirtuOS:SimpleTHP] No free page inside memory" << std::endl;
						allocate_4KB = true;
					}
					else
					{

						std::tuple<UInt64, UInt64, bool, UInt64> temp;

						temp = free_list[i][0];
						free_list[i].erase(free_list[i].begin());
						i--;

						while (i >= ind)
						{
#ifdef DEBUG_SIMPLE_THP
							std::cout << "Searching for new order: " << i << std::endl;
#endif

							std::tuple<UInt64, UInt64, bool, UInt64> pair1, pair2;

							pair1 = std::make_tuple(get<0>(temp), get<0>(temp) + (get<1>(temp) - get<0>(temp)) / 2, false, -1);
							pair2 = std::make_tuple(get<0>(temp) + (get<1>(temp) - get<0>(temp) + 1) / 2, get<1>(temp), false, -1);

							free_list[i].push_back(pair1);
							free_list[i].push_back(pair2);
							temp = free_list[i][0];
							free_list[i].erase(free_list[i].begin());

							i--;
						}
						allocated_map[get<0>(temp)] = get<1>(temp) - get<0>(temp) + 1;
#ifdef DEBUG_SIMPLE_THP
						std::cout << "[SimpleTHP Allocator] Allocated at order " << ind << "with size " << get<1>(temp) - get<0>(temp) + 1 << " at address " << get<0>(temp) << std::endl;
#endif
						region_begin = get<0>(temp);
						m_free_pages -= pow(2, ind);
						return region_begin;
					}
				}
			}
		}
	}
	else
	{
		int i;

		// find the order of the allocation

		int ind = ceil(log2(bytes / 4096));

#ifdef DEBUG_SIMPLE_THP
		std::cout << "[VirtuOS:SimpleTHP] Order of allocation: " << ind << " for " << bytes << " bytes" << std::endl;
#endif

		for (i = ind; i <= m_max_order; i++)
		{
			if (free_list[i].size() != 0)
				break;
		}
		if (i == m_max_order + 1)
		{
			std::cout << "[VirtuOS:SimpleTHP] No free page inside memory" << std::endl;
		}
		else
		{

			std::tuple<UInt64, UInt64, bool, UInt64> temp;

			temp = free_list[i][0];
			free_list[i].erase(free_list[i].begin());
			i--;

			while (i >= ind)
			{
#ifdef DEBUG_SIMPLE_THP
				std::cout << "Searching for new order: " << i << std::endl;
#endif

				std::tuple<UInt64, UInt64, bool, UInt64> pair1, pair2;

				pair1 = std::make_tuple(get<0>(temp), get<0>(temp) + (get<1>(temp) - get<0>(temp)) / 2, false, -1);
				pair2 = std::make_tuple(get<0>(temp) + (get<1>(temp) - get<0>(temp) + 1) / 2, get<1>(temp), false, -1);

				free_list[i].push_back(pair1);
				free_list[i].push_back(pair2);
				temp = free_list[i][0];
				free_list[i].erase(free_list[i].begin());

				i--;
			}
			allocated_map[get<0>(temp)] = get<1>(temp) - get<0>(temp) + 1;
#ifdef DEBUG_SIMPLE_THP
			std::cout << "[SimpleTHP Allocator] Allocated at order " << ind << "with size " << get<1>(temp) - get<0>(temp) + 1 << " at address " << get<0>(temp) << std::endl;
#endif
			region_begin = get<0>(temp);
			m_free_pages -= pow(2, ind);
			return region_begin;
		}
	}
}
std::vector<Range> SimpleTHPAllocator::allocate_eager_paging(UInt64 bytes)
{
	UInt64 region_begin;
	std::pair<UInt64, UInt64> temp;

	int pages = 0;
	if (bytes < 1024 * 4)
		pages = 1;
	else
		pages = ceil(bytes / (1024 * 4));

	int ind = ceil(log(pages) / log(2));

	bool above_max_order = ind < m_max_order;
	bool free_list_check = (above_max_order) ? false : (free_list[ind].size() > 0);

	std::vector<Range> ranges;

	while (pages > 0)
	{ // We allocate the biggest possible contiguous segments

		int order = ceil(log(pages) / log(2));

		int first_seg_flag = 1;
		for (int i = m_max_order; i >= 0; i--) // Start from max_order
		{

			if (free_list[i].size() > 0 && pages >= pow(2, i))
			{ // if the segment is free and the pages are more or same, allocate it

				std::tuple<UInt64, UInt64, bool, UInt64> temp;
				temp = free_list[i][0];
				free_list[i].erase(free_list[i].begin());
				allocated_map[get<0>(temp)] = get<1>(temp) - get<0>(temp) + 1;

				Range range;
				range.vpn = get<0>(temp);
				range.bounds = get<1>(temp) - get<0>(temp);
				ranges.push_back(range);

				pages -= pow(2, i);
				m_free_pages -= pow(2, i);

				// std::cout << "[Eager Paging] Allocated at order" << order << std::endl;
				break;
			}

			if (free_list[i].size() > 0 && pages < pow(2, i) && first_seg_flag)
			{ // if the segment is free and the pages are less than it,

				std::tuple<UInt64, UInt64, bool, UInt64> temp;
				temp = free_list[i][0];
				// std::cout<<"Taking out the SimpleTHP "<<temp.first <<" "<<temp.second<<std::endl;

				free_list[i].erase(free_list[i].begin());
				i--;

				while (i >= ind)
				{
					// std::cout << "Searching for new order: " << i << std::endl;

					std::tuple<UInt64, UInt64, bool, UInt64> pair1, pair2;
					pair1 = std::make_tuple(get<0>(temp), get<0>(temp) + (get<1>(temp) - get<0>(temp)) / 2, false, -1);
					pair2 = std::make_tuple(get<0>(temp) + (get<1>(temp) - get<0>(temp) + 1) / 2, get<1>(temp), false, -1);

					free_list[i].push_back(pair1);
					free_list[i].push_back(pair2);
					temp = free_list[i][0];
					free_list[i].erase(free_list[i].begin());

					i--;
				}
				// std::cout<<"Memory from "<<temp.	first <<" to "<<temp.second<<" allocated."<<std::endl;
				// std::cout << "[Eager Paging]" << temp.first << " to " << temp.second << " allocated with order: " << i << std::endl;
				allocated_map[get<0>(temp)] = get<1>(temp) - get<0>(temp) + 1;
				Range range;
				range.vpn = get<0>(temp);
				range.bounds = get<1>(temp) - get<0>(temp);
				ranges.push_back(range);
				pages -= pow(2, ind);
				m_free_pages -= pow(2, ind);
				break;
			}
		}
	}
	// std::cout << "[Eager Paging] Finished eager allocation" << std::endl;
	return ranges;
}

void SimpleTHPAllocator::deallocate(UInt64 region_begin)
{
	if (allocated_map.find(region_begin) == allocated_map.end())
	{
		return;
	}

	int ind = ceil(log2(allocated_map[region_begin]));

#ifdef DEBUG_SIMPLE_THP
	std::cout << "[VirtuOS:SimpleTHP] Deallocating " << allocated_map[region_begin] << " pages at address: " << region_begin << std::endl;
#endif

	int i, SimpleTHPNumber, SimpleTHPAddress;

	free_list[ind].push_back(std::make_tuple(region_begin, region_begin + pow(2, ind) - 1, false, -1));
	// std::cout<<"Returned memory block from "<<region_begin<<" to "<<region_begin + pow(2, ind)-1<<std::endl;
	m_free_pages += pow(2, ind);

	// If we are at the max order, no need to try mergning
	if (ind == m_max_order)
	{
		allocated_map.erase(region_begin);
		return;
	}

	SimpleTHPNumber = region_begin / allocated_map[region_begin];
	SimpleTHPAddress = (SimpleTHPNumber % 2 != 0) ? (region_begin - pow(2, ind)) : (region_begin + pow(2, ind));

	// Begin trying to merge the buddies
	while (true)
	{
		bool merged = false;
		UInt64 new_region_begin;
		for (i = 0; i < free_list[ind].size(); i++)
		{
			// If the SimpleTHP is found and free
			if (get<0>(free_list[ind][i]) == SimpleTHPAddress)
			{
				if (SimpleTHPNumber % 2 == 0)
				{
					free_list[ind + 1].push_back(std::make_tuple(region_begin, region_begin + 2 * pow(2, ind) - 1, false, -1));
					// std::cout<<"Mergning blocks starting at "<<region_begin<<" and "<<SimpleTHPAddress<<std::endl;
					new_region_begin = region_begin;
				}
				else
				{
					free_list[ind + 1].push_back(std::make_tuple(SimpleTHPAddress, SimpleTHPAddress + 2 * pow(2, ind), false, -1));
					// std::cout<<"Mergning blocks starting at "<<SimpleTHPAddress<<" and "<<region_begin<<std::endl;
					new_region_begin = SimpleTHPAddress;
				}

				free_list[ind].erase(free_list[ind].begin() + i);
				free_list[ind].erase(free_list[ind].begin() + free_list[ind].size() - 1);
				merged = true;
				break;
			}
		}
		// If we did not merge buddies, no point in continuing to try with next level
		if (!merged)
			break;

		ind++;
		// If we have the largest SimpleTHP, we cannot merge it further
		if (ind == m_max_order)
			break;

		allocated_map.erase(region_begin);
		region_begin = new_region_begin;
		SimpleTHPNumber = 2;
		SimpleTHPAddress = region_begin + pow(2, ind);
	}

	allocated_map.erase(region_begin);
}

void SimpleTHPAllocator::print_allocator()
{
}

UInt64 SimpleTHPAllocator::getFreePages() const
{
	return m_free_pages;
}

UInt64 SimpleTHPAllocator::getTotalPages() const
{
	return m_total_pages;
}

double SimpleTHPAllocator::getAverageSizeRatio() const
{
	std::vector<UInt64> blockSizes;

	// Collect sizes of all free blocks
	for (const auto &list : free_list)
	{
		for (const auto &block : list)
		{
			blockSizes.push_back(get<1>(block) - get<0>(block) + 1);
		}
	}

	// Sort block sizes in descending order
	std::sort(blockSizes.rbegin(), blockSizes.rend());

	// Calculate average of top 50 blocks
	UInt64 totalSize = 0;
	int count = 0;
	for (auto size : blockSizes)
	{
		totalSize += size;
		count++;
	}
	double averageSize = (count > 0) ? (double)totalSize / count : 0.0;
	// std::cout << "[Memory Allocator] Average size: " << averageSize << std::endl;
	return averageSize / (m_total_pages / 1000);
}

double SimpleTHPAllocator::getLargePageRatio() const
{
	int numberOfLargePages = 0;

	// Collect number of 2MB pages based on the free list
	for (const auto &list : free_list)
	{
		for (const auto &block : list)
		{
			if ((get<1>(block) - get<0>(block) + 1) >= 512)
			{
				numberOfLargePages += (get<1>(block) - get<0>(block) + 1) / 512;
			}
		}
	}
	std::cout << "Remaining 2MB pages: " << numberOfLargePages << std::endl;

	// calculate the ratio of available large pages to the total number of 2MB pages
	double largePageRatio = (double)numberOfLargePages / (m_total_pages / 512);
	return largePageRatio;
}

void SimpleTHPAllocator::perform_init_random(double target_fragmentation, double target_memory_percent, bool store_in_file)
{

	UInt64 pages = (UInt64)(m_free_pages * target_memory_percent);
	std::cout << "[Artificial Fragmentation Generator] Allocating: " << pages << " out of: " << m_total_pages << std::endl;
	std::vector<UInt64> used_pages;
	UInt64 chunk = 1;
	std::mt19937 gen(std::random_device{}()); // For generating random numbers
	std::uniform_int_distribution<UInt64> dist(0, m_max_order - 4);

	int counter = 0;
	while (getFreePages() > m_total_pages * (1.0 - target_memory_percent))
	{
		UInt64 randomSize = std::pow(2, dist(gen)) * 4096;
		used_pages.push_back(allocate(randomSize));
#ifdef DEBUG_SIMPLE_THP
		std::cout << "[Artificial Fragmentation Generator] Allocated: " << randomSize << " pages with new fragmentation: " << getLargePageRatio() << std::endl;
#endif
		counter++;
	}

	std::cout << "[Artificial Fragmentation Generator] This amount of memory is left: " << (double)(getFreePages() * 1.0 / m_total_pages * 1.0) << std::endl;

	int declining_frag = 0;
	bool prev_declining = false;

	double current_fragmentation = getLargePageRatio();
	double prev_fragmentation = 1;
#ifdef DEBUG_SIMPLE_THP
	std::cout << "[Artificial Fragmentation Generator] Current fragmentation: " << current_fragmentation << std::endl;
#endif
	bool randomized_fragmentation = false;
	bool cherry_pick_two_mb = false;
	while (current_fragmentation > target_fragmentation)
	{
		if (randomized_fragmentation)
		{
			// Randomly decide whether to allocate or deallocate
			bool isAllocate = (gen() % 2 == 0);

			if (isAllocate)
			{
				UInt64 randomSize = std::pow(2, dist(gen)) * 4096;
				allocate(randomSize);
				// std::cout << "[Artificial Fragmentation Generator] Allocated: " << randomSize << " pages" << std::endl;
			}
			else
			{
				if (!allocated_map.empty())
				{
					UInt64 randomAddress = allocated_map.begin()->first;
					deallocate(randomAddress);
					// std::cout << "[Artificial Fragmentation Generator] Deallocated: " << randomAddress << std::endl;
				}
			}
		}
		else
		{

			// Check if there is any large page to demote
			bool found = false;
			for (int i = 9; i <= m_max_order; i++)
			{
				if (free_list[i].size() > 0)
				{
					found = true;
					std::tuple<UInt64, UInt64, bool, UInt64> temp = free_list[i][0];
					free_list[i].erase(free_list[i].begin());

					UInt64 start = get<0>(temp);
					UInt64 end = get<1>(temp);
					UInt64 size = end - start + 1;
					UInt64 chunk = size / 256;
					for (int j = 0; j < chunk; i++)
					{
						free_list[8].push_back(std::make_tuple(start + i * 256, start + (i + 1) * 256 - 1, false, -1));
						assert(get<0>(free_list[8].back()) == start + i * 256);
					}
				}
			}
			current_fragmentation = getLargePageRatio();

#ifdef DEBUG_SIMPLE_THP
			std::cout << "[Artificial Fragmentation Generator] Current fragmentation: " << current_fragmentation << std::endl;
#endif
		}
	}
	std::cout << "Initialized SimpleTHP with final fragmentation: " << current_fragmentation << "and this ratio of memory: " << (double)(getFreePages() * 1.0 / m_total_pages * 1.0) << std::endl;
	// std::cout << " [Artificial Fragmentation Generator] Finished fragmenting with: " << current_fragmentation << ", " << current_fragmentation * m_total_pages / 1000 << " and " << m_free_pages << " / " << m_total_pages << " free pages " << std::endl;
	// std::cout << "Saving to file...." << std::endl;
	// if (store_in_file)
	// {
	// 	std::ofstream file;
	// 	std::string filename;
	// 	filename = "memory_snapshot_sizeGB_" + std::to_string(m_memory_size / 1024) + "_frag_" + std::to_string((int)(target_fragmentation * 100)) + "_size_" + std::to_string((int)(target_memory_percent * 100)) + ".memdump";
	// 	file.open(filename);
	// 	// Save Max Order, Total Pages, Free Pages
	// 	file << m_max_order << std::endl;
	// 	file << m_total_pages << std::endl;
	// 	file << m_free_pages << std::endl;

	// 	// first save free_list
	// 	for (int i = 0; i < free_list.size(); i++)
	// 	{
	// 		file << i << std::endl;
	// 		file << free_list[i].size() << std::endl;
	// 		for (int j = 0; j < free_list[i].size(); j++)
	// 		{
	// 			file << get<0>(free_list[i][j]) << std::endl;
	// 			file << get<1>(free_list[i][j]) << std::endl;
	// 		}
	// 	}
	// 	// now save allocated map
	// 	file << allocated_map.size() << std::endl;
	// 	for (auto it = allocated_map.begin(); it != allocated_map.end(); it++)
	// 	{
	// 		file << it->first << std::endl;
	// 		file << it->second << std::endl;
	// 	}

	// 	std::cout << "Done writing to the file." << std::endl;
	// }
}
