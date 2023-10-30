#include "buddy_allocator.h"
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
// #define DEBUG_BUDDY

BuddyAllocator::BuddyAllocator(int max_order) : m_max_order(max_order)
{
	for (int i = 0; i < m_max_order + 1; i++)
	{
		std::vector<std::pair<UInt64, UInt64>> vec;
		free_list.push_back(vec);
	}
	init();
}

void BuddyAllocator::init()
{
	std::cout << "------ [VirtuOS:Buddy] Initializing buddy allocator ------" << std::endl;
	std::cout << std::endl;
	std::cout << "[VirtuOS:Buddy] Memory Size: " << m_memory_size << std::endl;

	UInt64 pages_in_block = pow(2, m_max_order); // start with max
	UInt64 current_order = m_max_order;
	UInt64 total_mem_in_pages = m_memory_size * 1024 / 4;
	m_total_pages = total_mem_in_pages;
	m_free_pages = m_total_pages;
	UInt64 available_mem_in_pages = total_mem_in_pages;
	UInt64 current_free = 0;

	std::cout << "[VirtuOS:Buddy] 4KB pages in memory: " << total_mem_in_pages << std::endl;

	while (current_free < total_mem_in_pages)
	{
		while (available_mem_in_pages >= pages_in_block)
		{
			free_list[current_order].push_back(std::make_pair(current_free, current_free + pages_in_block - 1));
			current_free += pages_in_block;
			available_mem_in_pages -= pages_in_block;
		}
		std::cout << "[VirtuOS:Buddy] Order " << current_order << " has " << free_list[current_order].size() << " blocks" << std::endl;
		current_order--;
		pages_in_block = pow(2, current_order);
	}
}

UInt64 BuddyAllocator::allocate(UInt64 bytes, UInt64 address, UInt64 core_id)
{

	UInt64 region_begin;

	int pages = 0;
	if (bytes < 1024 * 4)
		pages = 1;
	else
		pages = ceil(bytes / (1024 * 4));

	int ind = ceil(log(pages) / log(2));
	if (free_list[ind].size() > 0)
	{
		std::pair<UInt64, UInt64> temp = free_list[ind][0];

		free_list[ind].erase(free_list[ind].begin());
		m_free_pages -= pow(2, ind);

		allocated_map[temp.first] = temp.second - temp.first + 1;
		region_begin = temp.first;
	}
	else
	{
		int i;
		for (i = ind; i <= m_max_order; i++)
		{
			if (free_list[i].size() != 0)
				break;
		}

		if (i == m_max_order + 1)
			std::cout << "[ERROR] Failed to allocate in buddy" << std::endl;
		else
		{
			std::pair<UInt64, UInt64> temp;
			temp = free_list[i][0];
			free_list[i].erase(free_list[i].begin());
			i--;

			while (i >= ind)
			{
				std::pair<UInt64, UInt64> pair1, pair2;
				pair1 = std::make_pair(temp.first, temp.first + (temp.second - temp.first) / 2);
				pair2 = std::make_pair(temp.first + (temp.second - temp.first + 1) / 2, temp.second);

				free_list[i].push_back(pair1);
				free_list[i].push_back(pair2);
				temp = free_list[i][0];
				free_list[i].erase(free_list[i].begin());

				i--;
			}
			allocated_map[temp.first] = temp.second - temp.first + 1;
#ifdef DEBUG_BUDDY
			std::cout << "[Buddy Allocator] Allocated at order " << ind << " with size " << temp.second - temp.first + 1 << " at address " << temp.first << std::endl;
#endif
			region_begin = temp.first;
			m_free_pages -= pow(2, ind);
		}
	}

	return region_begin;
}
std::vector<Range> BuddyAllocator::allocate_eager_paging(UInt64 bytes)
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

				std::pair<UInt64, UInt64> temp;
				temp = free_list[i][0];
				free_list[i].erase(free_list[i].begin());
				allocated_map[temp.first] = temp.second - temp.first + 1;

				Range range;
				range.vpn = temp.first;
				range.bounds = temp.second - temp.first;
				ranges.push_back(range);

				pages -= pow(2, i);
				m_free_pages -= pow(2, i);

				// std::cout << "[Eager Paging] Allocated at order" << order << std::endl;
				break;
			}

			if (free_list[i].size() > 0 && pages < pow(2, i) && first_seg_flag)
			{ // if the segment is free and the pages are less than it,

				std::pair<UInt64, UInt64> temp;
				temp = free_list[i][0];
				// std::cout<<"Taking out the buddy "<<temp.first <<" "<<temp.second<<std::endl;

				free_list[i].erase(free_list[i].begin());
				i--;

				while (i >= ind)
				{
					// std::cout << "Searching for new order: " << i << std::endl;
					std::pair<UInt64, UInt64> pair1, pair2;
					pair1 = std::make_pair(temp.first, temp.first + (temp.second - temp.first) / 2);
					pair2 = std::make_pair(temp.first + (temp.second - temp.first + 1) / 2, temp.second);

					free_list[i].push_back(pair1);
					free_list[i].push_back(pair2);
					temp = free_list[i][0];
					free_list[i].erase(free_list[i].begin());

					i--;
				}
				// std::cout<<"Memory from "<<temp.	first <<" to "<<temp.second<<" allocated."<<std::endl;
				// std::cout << "[Eager Paging]" << temp.first << " to " << temp.second << " allocated with order: " << i << std::endl;
				allocated_map[temp.first] = temp.second - temp.first + 1;
				Range range;
				range.vpn = temp.first;
				range.bounds = temp.second - temp.first;
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

void BuddyAllocator::deallocate(UInt64 region_begin)
{
	if (allocated_map.find(region_begin) == allocated_map.end())
	{
		// std::cout<<"[ERROR] FAILED TO FIND A BUDDY TO DEALLOCATE AT RANGE "<<region_begin<<std::endl;
		return;
	}

	int ind = ceil(log(allocated_map[region_begin]) / log(2));
	int i, buddyNumber, buddyAddress;

	free_list[ind].push_back(std::make_pair(region_begin, region_begin + pow(2, ind) - 1));
	// std::cout<<"Returned memory block from "<<region_begin<<" to "<<region_begin + pow(2, ind)-1<<std::endl;
	m_free_pages += pow(2, ind);

	// If we are at the max order, no need to try mergning
	if (ind == m_max_order)
	{
		allocated_map.erase(region_begin);
		return;
	}

	buddyNumber = region_begin / allocated_map[region_begin];
	buddyAddress = (buddyNumber % 2 != 0) ? (region_begin - pow(2, ind)) : (region_begin + pow(2, ind));

	// Begin trying to merge the buddies
	while (true)
	{
		bool merged = false;
		UInt64 new_region_begin;
		for (i = 0; i < free_list[ind].size(); i++)
		{
			// If the buddy is found and free
			if (free_list[ind][i].first == buddyAddress)
			{
				if (buddyNumber % 2 == 0)
				{
					free_list[ind + 1].push_back(std::make_pair(region_begin, region_begin + 2 * pow(2, ind) - 1));
					// std::cout<<"Mergning blocks starting at "<<region_begin<<" and "<<buddyAddress<<std::endl;
					new_region_begin = region_begin;
				}
				else
				{
					free_list[ind + 1].push_back(std::make_pair(buddyAddress, buddyAddress + 2 * pow(2, ind)));
					// std::cout<<"Mergning blocks starting at "<<buddyAddress<<" and "<<region_begin<<std::endl;
					new_region_begin = buddyAddress;
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
		// If we have the largest buddy, we cannot merge it further
		if (ind == m_max_order)
			break;

		allocated_map.erase(region_begin);
		region_begin = new_region_begin;
		buddyNumber = 2;
		buddyAddress = region_begin + pow(2, ind);
	}

	allocated_map.erase(region_begin);
}

void BuddyAllocator::print_allocator()
{
	// std::cout << "Printing buddy data structures" << std::endl;
	// std::cout << "Max order: " << m_max_order << std::endl;
	// std::cout << "Free List:" << std::endl;
	// for (int i = 0; i < m_max_order + 1; i++)
	// {
	// 	std::cout << "Order[" << i << "]" << std::endl;
	// 	for (int j = 0; j < free_list[i].size(); j++)
	// 		std::cout << free_list[i][j].first << " - " << free_list[i][j].second << std::endl;
	// }

	// std::cout << "Fragmentation?" << std::endl;
	// std::cout << getFragmentationPercentage() << std::endl;
}

UInt64 BuddyAllocator::getFreePages() const
{
	return m_free_pages;
}

UInt64 BuddyAllocator::getTotalPages() const
{
	return m_total_pages;
}

double BuddyAllocator::getAverageSizeRatio() const
{
	std::vector<UInt64> blockSizes;

	// Collect sizes of all free blocks
	for (const auto &list : free_list)
	{
		for (const auto &block : list)
		{
			blockSizes.push_back(block.second - block.first + 1);
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

void BuddyAllocator::perform_init_file(String input_file_name)
{
	std::ifstream file(input_file_name.c_str());
	std::string line;
	double frag = 0;

	// Read Max Order, Total Pages, Free Pages
	std::getline(file, line);
	m_max_order = std::stoi(line);

	std::getline(file, line);
	m_total_pages = std::stoi(line);

	std::getline(file, line);
	m_free_pages = std::stoi(line);

	// TODO add check if order is the same in the file and in the allocator
	for (int p = 0; p < free_list.size(); p++)
	{
		std::getline(file, line);
		// std::cout<<"Reading "<<line<<std::endl;
		UInt64 i = std::stoi(line);
		free_list[i].clear();

		std::getline(file, line);
		// std::cout<<"Reading "<<line<<std::endl;
		UInt64 size = std::stoi(line);
		for (int j = 0; j < size; j++)
		{
			std::getline(file, line);
			UInt64 first = std::stoi(line);
			// std::cout<<"Reading "<<line<<std::endl;

			std::getline(file, line);
			UInt64 second = std::stoi(line);
			// std::cout<<"Reading "<<line<<std::endl;

			auto pair = std::make_pair(first, second);
			free_list[i].push_back(pair);
		}
	}

	std::getline(file, line);
	UInt64 size = std::stoi(line);
	// std::cout<<"Read size for allocated list "<<size<<std::endl;
	allocated_map.clear();
	for (int i = 0; i < size; i++)
	{
		std::getline(file, line);
		UInt64 first = std::stoi(line);

		std::getline(file, line);
		UInt64 second = std::stoi(line);

		allocated_map[first] = second;
		// std::cout<<"Added "<<first<<" and "<<second<<std::endl;
	}
}

void BuddyAllocator::perform_init_random(double target_fragmentation, double target_memory_percent, bool store_in_file)
{

	UInt64 pages = (UInt64)(m_free_pages * target_memory_percent);
	std::cout << "[Artificial Fragmentation Generator] Allocating: " << pages << " out of: " << m_total_pages << std::endl;
	std::vector<UInt64> used_pages;
	UInt64 chunk = 1;
	std::mt19937 gen(std::random_device{}()); // For generating random numbers
	std::uniform_int_distribution<UInt64> dist(1, m_max_order);

	int counter = 0;
	while (getFreePages() > m_total_pages * (1.0 - target_memory_percent))
	{
		UInt64 randomSize = std::pow(2, dist(gen));
		used_pages.push_back(allocate(randomSize));
		// std::cout << "[Artificial Fragmentation Generator] Allocated: " << randomSize << " pages at " << used_pages[counter] << std::endl;
		counter++;
	}

	int declining_frag = 0;
	bool prev_declining = false;

	double current_fragmentation = getAverageSizeRatio();
	double prev_fragmentation = 1;

	while (getAverageSizeRatio() > target_fragmentation)
	{
		// Randomly decide whether to allocate or deallocate
		bool isAllocate = (gen() % 2 == 0);

		if (isAllocate)
		{
			UInt64 randomSize = std::pow(2, dist(gen));
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
		// std::cout << "[Artificial Fragmentation Generator] Current fragmentation: " << getAverageSizeRatio() << std::endl;
		current_fragmentation = getAverageSizeRatio();
	}

	std::cout << " [Artificial Fragmentation Generator] Finished fragmenting with: " << current_fragmentation << ", " << current_fragmentation * m_total_pages / 1000 << " and " << m_free_pages << " / " << m_total_pages << " free pages " << std::endl;
	std::cout << "Saving to file...." << std::endl;
	if (store_in_file)
	{
		std::ofstream file;
		std::string filename;
		filename = "memory_snapshot_sizeGB_" + std::to_string(m_memory_size / 1024) + "_frag_" + std::to_string((int)(target_fragmentation * 100)) + "_size_" + std::to_string((int)(target_memory_percent * 100)) + ".memdump";
		file.open(filename);
		// Save Max Order, Total Pages, Free Pages
		file << m_max_order << std::endl;
		file << m_total_pages << std::endl;
		file << m_free_pages << std::endl;

		// first save free_list
		for (int i = 0; i < free_list.size(); i++)
		{
			file << i << std::endl;
			file << free_list[i].size() << std::endl;
			for (int j = 0; j < free_list[i].size(); j++)
			{
				file << free_list[i][j].first << std::endl;
				file << free_list[i][j].second << std::endl;
			}
		}
		// now save allocated map
		file << allocated_map.size() << std::endl;
		for (auto it = allocated_map.begin(); it != allocated_map.end(); it++)
		{
			file << it->first << std::endl;
			file << it->second << std::endl;
		}

		std::cout << "Done writing to the file." << std::endl;
	}
}
