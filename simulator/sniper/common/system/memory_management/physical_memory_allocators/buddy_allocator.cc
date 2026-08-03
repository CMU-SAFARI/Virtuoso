#include "buddy_allocator.h"
#include "fixed_types.h"
#include <vector>
#include <tuple>
#include <string>
#include <iostream>
#include <cmath>
#include <random>
#include <cassert>
#include <algorithm>
#include "debug_config.h"


using namespace std;

Buddy::Buddy(int memory_size, int max_order, int kernel_size, String frag_type) :
m_memory_size(memory_size),  m_max_order(max_order), m_kernel_size(kernel_size),  m_frag_type(frag_type)

{
	// Initialize SimLog for Buddy allocator (uses DEBUG_BUDDY flag)
	buddy_log = new SimLog("BUDDY", 0, DEBUG_BUDDY);

	buddy_log->debug("------ Initializing free lists ------");

	for (int i = 0; i < m_max_order + 1; i++)
	{
		std::vector<std::tuple<UInt64, UInt64, bool, UInt64>> vec; // Block start, Block end
		free_list.push_back(vec);
	}
	// Set the fragmentation function based on the frag_type
	if (frag_type == "contiguity")
	{
		frag_fun = &Buddy::getAverageSizeRatio;
	}
	else if (frag_type == "largepage")
	{
		frag_fun = &Buddy::getLargePageRatio;
	}
	else
	{ // default is getLargePageRatio
		frag_fun = &Buddy::getLargePageRatio;
	}


	std::cout << "[Buddy] Memory Size: " << m_memory_size << std::endl;


	UInt64 pages_in_block = static_cast<UInt64>(pow(2, m_max_order)); // start with max

	UInt64 current_order = m_max_order;

	// Initialize the free list with all the pages in memory
	// We need to subtract the kernel size from the total memory size
	UInt64 total_mem_in_pages = m_memory_size * 1024 / 4 - m_kernel_size * 1024 / 4;

	m_total_pages = total_mem_in_pages;

	m_free_pages = m_total_pages;

	UInt64 available_mem_in_pages = total_mem_in_pages;

	UInt64 current_free = m_kernel_size * 1024 / 4;


	std::cout << "[Buddy] 4KB pages in memory: " << total_mem_in_pages << std::endl;
	std::cout << "[Buddy] 2MB pages in memory: " << total_mem_in_pages / 512 << std::endl;
	std::cout << "[Buddy] 1GB pages in memory: " << total_mem_in_pages / 512 / 512 << std::endl;


	// Initialize the free list with all the pages in memory
	while (current_free < static_cast<UInt64>(m_memory_size * 1024 / 4))
	{
		while (available_mem_in_pages >= pages_in_block)
		{
			buddy_log->trace("Adding block of size ", pages_in_block, " at address ", current_free);
			free_list[current_order].push_back(std::make_tuple(current_free, current_free + pages_in_block - 1, false, -1));
			current_free += pages_in_block;
			available_mem_in_pages -= pages_in_block;
		}
		buddy_log->trace("Order ", current_order, " has ", free_list[current_order].size(), " blocks");
		current_order--;
		pages_in_block = static_cast<UInt64>(pow(2, current_order));
	}
	buddy_log->debug("Initialization done");

}

bool Buddy::checkIfFree(UInt64 physical_page, bool allocate)
{
    buddy_log->debug("checkIfFree: Checking page ", physical_page, ", allocate: ", (allocate ? "true" : "false"));

    int found_order = -1;
    int found_block_index = -1;
    bool found = false;

	int iterations = 0;
    // Search for the physical page in the free lists, starting from order 0.
    for (int i = 0; i <= m_max_order; i++)
    {
        for (size_t j = 0; j < free_list[i].size(); ++j)
        {
            if (get<0>(free_list[i][j]) <= physical_page && get<1>(free_list[i][j]) >= physical_page)
            {
                buddy_log->trace("checkIfFree: Found page ", physical_page, " in block at order ", i, ", index ", j);
                buddy_log->trace("checkIfFree: Block details: start=", get<0>(free_list[i][j]), ", end=", get<1>(free_list[i][j]));

                found = true;
                found_order = i;
                found_block_index = j;

                goto found_block; // Exit nested loops once the block is found
            }
			iterations++;
        }
    }



found_block:
std::cout << "[Buddy::checkIfFree] Iterations: " << iterations << std::endl;
    if (!found)
    {
        buddy_log->debug("checkIfFree: Page ", physical_page, " is not in any free block.");
        return false; // The page is not in any free block.
    }

    if (allocate)
    {
        buddy_log->debug("checkIfFree: Allocation requested. Starting split process.");

        // The page is free and we need to allocate it.
        // This involves removing the block and splitting it until we isolate the single page.
        
        // 1. Get the block that contains the page and remove it from its free list.
        std::tuple<UInt64, UInt64, bool, UInt64> block_to_split = free_list[found_order][found_block_index];
        free_list[found_order].erase(free_list[found_order].begin() + found_block_index);

        buddy_log->debug("checkIfFree: Removed block from order ", found_order, " to start splitting.");

        // 2. Recursively split the block until we reach order 0 (a single page).
        for (int current_order = found_order; current_order > 0; --current_order)
        {
            buddy_log->trace("checkIfFree: Splitting block of order ", current_order, ". Start: ", get<0>(block_to_split), ", End: ", get<1>(block_to_split));

            UInt64 start_addr = get<0>(block_to_split);
            UInt64 end_addr = get<1>(block_to_split);
            UInt64 mid_point = start_addr + (end_addr - start_addr) / 2;
            
            std::tuple<UInt64, UInt64, bool, UInt64> buddy1, buddy2;
            
            buddy1 = std::make_tuple(start_addr, mid_point, false, -1);
            buddy2 = std::make_tuple(mid_point + 1, end_addr, false, -1);
            
            // Determine which buddy contains the physical page.
            // The other buddy is returned to the free list.
            if (physical_page <= mid_point)
            {
                buddy_log->trace("checkIfFree: Page in first buddy. Returning second buddy (start=", get<0>(buddy2), ", end=", get<1>(buddy2), ") to order ", current_order - 1);

                // The page is in the first buddy. Add the second buddy to the free list.
                free_list[current_order - 1].push_back(buddy2);
                block_to_split = buddy1; // Continue splitting the first buddy.
            }
            else
            {
                buddy_log->trace("checkIfFree: Page in second buddy. Returning first buddy (start=", get<0>(buddy1), ", end=", get<1>(buddy1), ") to order ", current_order - 1);

                // The page is in the second buddy. Add the first buddy to the free list.
                free_list[current_order - 1].push_back(buddy1);
                block_to_split = buddy2; // Continue splitting the second buddy.
            }
        }
        
        // 3. Decrement the free page count. The 'block_to_split' is now the single page being allocated.
        m_free_pages--;
        buddy_log->debug("checkIfFree: Allocation complete. Final allocated page: ", get<0>(block_to_split), ". Free pages left: ", m_free_pages);
    }

    // If we found the block, the page is considered free, regardless of allocation.
    return true;
}

bool Buddy::checkIfFreeDiffSize(UInt64 base_addr, UInt64 bytes, bool allocate)
{
    buddy_log->debug("checkIfFreeDiffSize: Checking page at base addr: ", base_addr, ", allocate: ", (allocate ? "true" : "false"));

    int found_order = -1;
    int found_block_index = -1;
    bool found = false;

    // Search for the physical page in the free lists, starting from order 0.
    for (int i = 0; i <= m_max_order; i++)
    {
        for (size_t j = 0; j < free_list[i].size(); ++j)
        {
            if (get<0>(free_list[i][j]) <= base_addr && get<1>(free_list[i][j]) >= base_addr + bytes)
            {
                buddy_log->trace("checkIfFreeDiffSize: Found page ", base_addr, " in block at order ", i, ", index ", j);
                buddy_log->trace("checkIfFreeDiffSize: Block details: start=", get<0>(free_list[i][j]), ", end=", get<1>(free_list[i][j]));

                found = true;
                found_order = i;
                found_block_index = j;
                goto found_block; // Exit nested loops once the block is found
            }
        }
    }

found_block:
    if (!found)
    {
        buddy_log->debug("checkIfFreeDiffSize: Page ", base_addr, " allocation of ", bytes, " bytes is not in any free block.");
        return false; // The page is not in any free block.
    }

    if (allocate)
    {
        buddy_log->debug("checkIfFreeDiffSize: Allocation requested. Starting split process.");

        // The page is free and we need to allocate it.
        // This involves removing the block and splitting it until we isolate the single page.
        
        // 1. Get the block that contains the page and remove it from its free list.
        std::tuple<UInt64, UInt64, bool, UInt64> block_to_split = free_list[found_order][found_block_index];
        free_list[found_order].erase(free_list[found_order].begin() + found_block_index);

        buddy_log->debug("checkIfFreeDiffSize: Removed block from order ", found_order, " to start splitting.");

        // 2. Recursively split the block until we reach order 0 (a single page).
        for (int current_order = found_order; current_order > 0; --current_order)
        {
            buddy_log->trace("checkIfFreeDiffSize: Splitting block of order ", current_order, ". Start: ", get<0>(block_to_split), ", End: ", get<1>(block_to_split));

            UInt64 start_addr = get<0>(block_to_split);
            UInt64 end_addr = get<1>(block_to_split);
            UInt64 mid_point = start_addr + (end_addr - start_addr) / 2;
            
            std::tuple<UInt64, UInt64, bool, UInt64> buddy1, buddy2;
            
            buddy1 = std::make_tuple(start_addr, mid_point, false, -1);
            buddy2 = std::make_tuple(mid_point + 1, end_addr, false, -1);
            
            // Determine which buddy contains the physical page.
            // The other buddy is returned to the free list.
            if (base_addr <= mid_point)
            {
                buddy_log->trace("checkIfFreeDiffSize: Page in first buddy. Returning second buddy (start=", get<0>(buddy2), ", end=", get<1>(buddy2), ") to order ", current_order - 1);

                // The page is in the first buddy. Add the second buddy to the free list.
                free_list[current_order - 1].push_back(buddy2);
                block_to_split = buddy1; // Continue splitting the first buddy.
            }
            else
            {
                buddy_log->trace("checkIfFreeDiffSize: Page in second buddy. Returning first buddy (start=", get<0>(buddy1), ", end=", get<1>(buddy1), ") to order ", current_order - 1);

                // The page is in the second buddy. Add the first buddy to the free list.
                free_list[current_order - 1].push_back(buddy1);
                block_to_split = buddy2; // Continue splitting the second buddy.
            }
        }
        
        // 3. Decrement the free page count. The 'block_to_split' is now the single page being allocated.
        m_free_pages--;
        buddy_log->debug("checkIfFreeDiffSize: Allocation complete. Final allocated page: ", get<0>(block_to_split), ". Free pages left: ", m_free_pages);
    }

    // If we found the block, the page is considered free, regardless of allocation.
    return true;
}

/**
 * @brief Fragment the memory to achieve a target fragmentation level.
 *
 * @param target_fragmentation The desired level of memory fragmentation.
 *
 * The function uses a random number generator with a fixed seed to ensure
 * reproducibility. It iterates through the free list of pages, starting
 * from the largest pages, and demotes them to smaller pages by splitting
 * them into smaller chunks. The process continues until the current
 * fragmentation level is less than or equal to the target fragmentation level.
 *
 * Debug information is logged if DEBUG_BUDDY is defined.
 */

void Buddy::fragmentMemory(double target_fragmentation)
{

	std::cout << "[BUDDY] Fragmenting memory to achieve target fragmentation: " << target_fragmentation << std::endl;

	std::vector<UInt64> used_pages;
	unsigned seed = 12345;
	std::mt19937 gen(seed);

	std::uniform_int_distribution<UInt64> dist(0, m_max_order - 4);


	double current_fragmentation = (this->*frag_fun)();

	buddy_log->debug("Artificial Fragmentation Generator: Current fragmentation: ", current_fragmentation);

	while (current_fragmentation > target_fragmentation)
	{
		// Check if there is any large page to demote

		for (int i = m_max_order; i >= 9; i--)
		{
			if (free_list[i].size() > 0)
			{
				std::tuple<UInt64, UInt64, bool, UInt64> temp = free_list[i][0];
				free_list[i].erase(free_list[i].begin());

				UInt64 start = get<0>(temp);
				UInt64 end = get<1>(temp);
				UInt64 size = end - start + 1;
				// generate a random order between 8 and m_max_order-3, add seed
				std::uniform_int_distribution<UInt64> dist(8, i - 1);

				UInt64 random_order = dist(gen);
				UInt64 chunk = size / std::pow(2, random_order);
				UInt64 pages_in_block = pow(2, random_order);

				// Essentially, we are splitting the large page into smaller pages
				// and adding them to the free list
				// This way we are increasing the fragmentation without actually allocating memory which is very useful for testing and evaluation
				for (UInt64 j = 0; j < chunk; j++)
				{
					free_list[random_order].push_back(std::make_tuple(start + j * pages_in_block, start + (j + 1) * pages_in_block - 1, false, -1));
					assert(get<0>(free_list[random_order].back()) == (start + j * pages_in_block));
				}
				break;
			}
		}
		current_fragmentation = (this->*frag_fun)();
		buddy_log->debug("Artificial Fragmentation Generator: Current fragmentation: ", current_fragmentation);
	}
	std::cout << "[BUDDY] Initialized memory with final fragmentation: " << current_fragmentation << std::endl;
	std::cout << "[BUDDY] Free pages: " << m_free_pages << std::endl;
	std::cout << "[BUDDY] Free pages in (MB): " << m_free_pages * 4 / 1024 << std::endl;
	return;
}


UInt64 Buddy::allocate(UInt64 bytes, UInt64 address, UInt64 core_id)
{

	int ind = ceil(log2(bytes / 4096));
	int i;

	for (i = ind; i <= m_max_order; i++)
	{
		if (free_list[i].size() != 0)
			break;
	}
	if (i == m_max_order + 1)
	{
		buddy_log->debug("allocate: No free page inside memory");
		return static_cast<UInt64>(-1);
	}
	else
	{
		std::tuple<UInt64, UInt64, bool, UInt64> temp;

		buddy_log->debug("allocate: Found free page in order ", i);
		temp = free_list[i].back();
		free_list[i].pop_back();
		i--;


		// Split the block into smaller blocks until we reach the desired size

		while (i >= ind)
		{

			std::tuple<UInt64, UInt64, bool, UInt64> pair1, pair2;

			pair1 = std::make_tuple(get<0>(temp), get<0>(temp) + (get<1>(temp) - get<0>(temp)) / 2, false, -1);
			pair2 = std::make_tuple(get<0>(temp) + (get<1>(temp) - get<0>(temp)) / 2 + 1, get<1>(temp), false, -1);

			assert((get<1>(pair2) - get<0>(pair2) + 1) == pow(2, i));

			free_list[i].push_back(pair1);
			free_list[i].push_back(pair2);
			temp = free_list[i].back();
			free_list[i].pop_back();
			i--;
		}
		buddy_log->debug("allocate: Allocated ", bytes, " bytes at address ", get<0>(temp));
		m_free_pages -= pow(2, ind);
		return get<0>(temp);
	}
	assert(false);
	return static_cast<UInt64>(-1);

}


std::pair<IntPtr,int> Buddy::allocate_contiguous(UInt64 size, UInt64 app_id)
{

	int ind = ceil(log2(size / 4096));
	int i;

	int largest_order = -1;

	buddy_log->debug("allocate_contiguous: Allocating ", size, " bytes for app ", app_id);
	buddy_log->debug("allocate_contiguous: Requested order: ", ind);

	// Search for high order blocks first
	for (i = ind; i <= m_max_order; i++)
	{
		if (free_list[i].size() != 0)
			break;

	}

	if (i == m_max_order + 1)
	{
		buddy_log->debug("allocate_contiguous: I can't provide such a large contiguous memory");

		// Provide the largest contiguous memory available

		for (i=0; i <= m_max_order; i++)
		{
			if (free_list[i].size() >0)
			{
				largest_order = i;
			}
		}

		if (largest_order == -1)
		{
			buddy_log->debug("allocate_contiguous: No free page inside memory");
			return std::make_pair((UInt64)-1, -1);
		}
		else
		{
			buddy_log->debug("allocate_contiguous: Found free block in order ", largest_order);
			std::tuple<UInt64, UInt64, bool, UInt64> temp = free_list[largest_order].back();
			free_list[largest_order].pop_back();
			m_free_pages -= pow(2, largest_order);
			return std::make_pair(get<0>(temp), get<1>(temp) - get<0>(temp) + 1);

		}

	}
	else
	{
		std::tuple<UInt64, UInt64, bool, UInt64> temp;
		std::vector<std::tuple<UInt64, UInt64, bool, UInt64>> temp_list;

		buddy_log->debug("allocate_contiguous: Found free block in high order ", i, " and requested order is: ", ind);
		temp = free_list[i].back();
		free_list[i].pop_back();

		i--;

		// Split the block into smaller blocks until we reach the desired size
		buddy_log->debug("allocate_contiguous: Splitting block into smaller blocks");

		while (i >= ind)
		{

			std::tuple<UInt64, UInt64, bool, UInt64> pair1, pair2;

			buddy_log->trace("allocate_contiguous: Splitting block into smaller blocks of order ", i);
			buddy_log->trace("allocate_contiguous: Block start: ", get<0>(temp), " Block end: ", get<1>(temp));

			pair1 = std::make_tuple(get<0>(temp), get<0>(temp) + (get<1>(temp) - get<0>(temp)) / 2, false, -1);
			pair2 = std::make_tuple(get<0>(temp) + (get<1>(temp) - get<0>(temp)) / 2 + 1, get<1>(temp), false, -1);

			assert((get<1>(pair2) - get<0>(pair2) + 1) == pow(2, i));

			free_list[i].push_back(pair1);
			free_list[i].push_back(pair2);
			temp = free_list[i].back();
			free_list[i].pop_back();
			i--;
		}

		buddy_log->debug("allocate_contiguous: Allocated ", size, " bytes at address ", get<0>(temp));

		m_free_pages -= pow(2, ind);

		buddy_log->debug("allocate_contiguous: Updated m_free_pages = ", m_free_pages);
		buddy_log->debug("allocate_contiguous: Returning address: ", get<0>(temp), " and size: ", get<1>(temp) - get<0>(temp) + 1);

		return std::make_pair(get<0>(temp), get<1>(temp) - get<0>(temp) + 1);
		 
	}

}

std::tuple<UInt64, UInt64, bool, UInt64> Buddy::reserve_2mb_page(UInt64 address, UInt64 core_id)
{
	
	// check if there is a 2MB region available
	buddy_log->debug("reserve_2mb_page: Checking for 2MB region in free_list[9]");

	if (free_list[9].size() > 0)
	{

		buddy_log->debug("reserve_2mb_page: 2MB region available in free_list[9]");

		// get the 2MB region
		std::tuple<UInt64, UInt64, bool, UInt64> temp = free_list[9].back();
		free_list[9].pop_back();

		buddy_log->debug("reserve_2mb_page: Retrieved and removed 2MB region from free_list[9]");

		return temp;
	}
	else {

		buddy_log->debug("reserve_2mb_page: No 2MB region available in free_list[9], checking higher orders");

		for (int i = 10; i <= m_max_order; i++)
		{
			if (free_list[i].size() > 0)
			{

				buddy_log->debug("reserve_2mb_page: Region available in free_list[", i, "]");

				std::tuple<UInt64, UInt64, bool, UInt64> temp = free_list[i].front();
				free_list[i].erase(free_list[i].begin());
				UInt64 start = get<0>(temp);
				UInt64 end = get<1>(temp);
				UInt64 size = end - start + 1;
				UInt64 chunk = size / 512;
				UInt64 pages_in_block = pow(2, 9);

				buddy_log->trace("reserve_2mb_page: Splitting region from free_list[", i, "] into 2MB chunks");

				for (UInt64 j = 0; j < chunk; j++)
				{
					free_list[9].push_back(std::make_tuple(start + j * pages_in_block, start + (j + 1) * pages_in_block - 1, false, -1));
					assert(get<0>(free_list[9].back()) == (start + j * pages_in_block));
					buddy_log->trace("reserve_2mb_page: Added 2MB chunk to free_list[9], start = ", (start + j * pages_in_block));
				}
				temp = free_list[9].back();
				free_list[9].pop_back();
				buddy_log->debug("reserve_2mb_page: Retrieved and removed 2MB region from free_list[9] after splitting");
				buddy_log->debug("reserve_2mb_page: Returning 2MB region from 4KB start page: ", get<0>(temp), " and end page: ", get<1>(temp));

				return temp;
			}
		}
	}

	buddy_log->debug("reserve_2mb_page: No region available, returning nullptr");

	return std::make_tuple(-1, 0, false, 0);

}


void Buddy::free(UInt64 start, UInt64 end)
{
	int order = ceil(log2((end - start + 1)));
	int i = order;

	buddy_log->trace("free: Calculated order = ", order);

	std::tuple<UInt64, UInt64, bool, UInt64> temp = std::make_tuple(start, end, false, -1);

	buddy_log->trace("free: Created tuple with start = ", start, " and end = ", end);

	free_list[i].push_back(temp);

	buddy_log->trace("free: Pushed tuple to free_list[", i, "]");

	m_free_pages += pow(2, i);

	buddy_log->trace("free: Updated m_free_pages = ", m_free_pages);
}


double Buddy::getAverageSizeRatio()
{
	std::vector<UInt64> blockSizes;

	// Collect sizes of all free blocks
	for (const auto &list : free_list)
	{
		for (const auto &block : list)
		{
			// check if the block is contiguous
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
		if (count == 50)
			break;
	}

	double averageSize = (count > 0) ? (double)totalSize / count : 0.0;
	return averageSize / pow(2, m_max_order - 3);
}

double Buddy::getLargePageRatio()
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

	// calculate the ratio of available large pages to the total number of 2MB pages
	double largePageRatio = (double)numberOfLargePages / (m_total_pages / 512);
	m_frag_factor = largePageRatio;
	return largePageRatio;
}