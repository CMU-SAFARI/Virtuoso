#pragma once

#include "debug_config.h"
#include "physical_memory_allocator.h"
#include <vector>

#include "templates_traits_config.h"

#include "memory_management/physical_memory_allocators/buddy_policy_traits.h"
#include "memory_management/physical_memory_allocators/buddy.h"

#include "debug_config.h"

// forward declare policy concept struct later
template <typename Policy>
class SpotAllocator : public PhysicalMemoryAllocator, private Policy
{
    using BuddyPolicy = typename BuddyPolicyFor<Policy>::type;
    using BuddyType =   Buddy<BuddyPolicy>;

public:
    std::ofstream log_stream;

    SpotAllocator(String name,
                    UInt64 memory_size,
                    int max_order,
                    UInt64 kernel_size,
                    String frag_type) 
        : PhysicalMemoryAllocator(name, memory_size, kernel_size),
        m_max_order(max_order)
    {
        // @hsongara: Add on_init
        Policy::on_init(name, memory_size, kernel_size, max_order, frag_type, this);

        // PROBLEM: include‑order contract problem: Right now, reserve_thp.h assumes that BuddyPolicyFor<Policy> is fully specialized. If someone forgets to include the side‑specific buddy_policy.h first, compilation fails with a cryptic "incomplete type" error.
        // SOLUTION (self‑documenting and enforceable at compile‑time): define type trait "is_complete" to validate type is complete
        static_assert(is_complete<BuddyPolicyFor<Policy>>::value,
                        "BuddyPolicyFor<Policy> is incomplete. Did you include the correct buddy_policy.h before spot.h?");

        buddy_allocator = new BuddyType(memory_size, max_order, kernel_size, frag_type);
        
        PhysicalRange initial_range = {};
        initial_range.ppn = kernel_size * 1024 / 4;
        initial_range.bounds = memory_size * 1024 / 4;

        // Initialize contiguity map with an initial range
        std::cout << "[SpotAllocator] Initializing contiguity map with range: " << initial_range.ppn << " - " << initial_range.bounds << std::endl;
        contiguity_map.push_back(initial_range);
    }

    ~SpotAllocator()
    {
        delete buddy_allocator;
    }

    /**
     * @brief Allocates a block of physical memory.
     *
     * This function allocates a block of physical memory of the specified size.
     *
     * @param size The number of bytes to allocate.
     * @param address The address hint for the allocation.
     * @param app_id The ID of the core requesting the allocation.
     * @param is_pagetable_allocation A flag indicating if the allocation is for a page table.
     * @param is_instruction_allocation A flag indicating if the allocation is for instruction pages.
     * @return The physical address of the allocated memory block.
     */
    std::pair<UInt64, UInt64> allocate(UInt64 size,
                                       IntPtr address,
                                       UInt64 app_id,
                                       bool is_pagetable_allocation,
                                       bool is_instruction_allocation = false) override
    {
#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
        log_stream << "SpotAllocator::allocate(" << size << ", " << address << ", " << app_id << ", " << is_pagetable_allocation << ", " << is_instruction_allocation << ")" << endl;
#endif
        // If the allocation is for a page table, handle it separately
        if (is_pagetable_allocation)
        {
            UInt64 final_ppn = handle_page_table_allocations(size);
#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
            // Ensure that the PPN is valid; otherwise: handle_page_table_allocations is broken
            assert(final_ppn != static_cast<UInt64>(-1));
#endif
            return make_pair(final_ppn, 12);
        }

        // Instruction allocations go to the reserved instruction area
        if (is_instruction_allocation)
        {
            return this->allocateInstruction(size);
        }

        // Find the VMA that contains the address
        IntPtr final_physical_page = static_cast<IntPtr>(-1);
        IntPtr final_ppn = static_cast<IntPtr>(-1);

        auto [vma_index, current_offset, vma_size] = Policy::find_VMA_specs(address, app_id, this);

        // No VMA found for this address — fall back to simple buddy allocation
        if (vma_index == -1)
        {
#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
            log_stream << "[SpOTAllocator] No VMA found for address 0x" << std::hex << address
                       << std::dec << ", falling back to buddy allocation" << std::endl;
#endif
            final_ppn = buddy_allocator->allocate(4096);
            return make_pair(final_ppn, 12);
        }

        // If we found a VMA that was not allocated
        if (current_offset == static_cast<int64_t>(-1))
        {
            bool found = false;
            int64_t largestest_range = -1;
            int largestest_range_index = -1;
            size_t i = static_cast<size_t>(contiguity_map_pointer); // Start from the current pointer in the contiguity map
            size_t visited_ranges = 0; // To avoid infinite loops in case of a bug

            while (visited_ranges < contiguity_map.size() && !found)
            {
#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
                    log_stream << "[SpOTAllocator] Checking contiguity map range: " << contiguity_map[i].ppn << " - " << contiguity_map[i].bounds << std::endl;
                    log_stream << "[SpOTAllocator] bounds - ppn + 1 == " << contiguity_map[i].bounds  - contiguity_map[i].ppn + 1 << std::endl;
                    log_stream << "[SpOTAllocator] vma_size / 4096 == " << vma_size / 4096 << std::endl;
#endif
                if ((contiguity_map[i].bounds - contiguity_map[i].ppn+1) >= static_cast<IntPtr>(vma_size)/4096)
                {
#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
                    log_stream << "[SpOTAllocator] Found suitable range: " << contiguity_map[i].ppn << " - " << contiguity_map[i].bounds << std::endl;
#endif
                    // Find the first ppn in the contiguity map
                    IntPtr first_ppn = contiguity_map[i].ppn;

                    // Set the offset to the first ppn of the range
                    current_offset = static_cast<int64_t>(address >> 12) - static_cast<int64_t>(first_ppn);
                    Policy::setPhysicalOffset(app_id, vma_index, current_offset, this);

                    contiguity_map_pointer = (i + 1) % contiguity_map.size(); // Update the pointer to the next free range
                    found = true; // We found a suitable range, so we can break the loop
                    
#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
                    log_stream << "[SpOTAllocator] Allocating at PPN: " << first_ppn << " for vpn: " << address/4096 << " and updating contiguity map pointer to: " << first_ppn + 1 << std::endl;
                    log_stream << "[SpOTAllocator] Current offset: " << std::hex << current_offset << " -- set offset: " << Policy::getPhysicalOffset(app_id, address, this) << std::endl << std::dec;
#endif
                    break;
                }

                // Update the largestest range so that we resort to it if we cannot find a suitable range that can accommodate the whole VMA
                if (static_cast<int64_t>(contiguity_map[i].bounds - contiguity_map[i].ppn+1) > static_cast<int64_t>(largestest_range))
                {
#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
                    log_stream << "[SpOTAllocator]  Update the largestest range so that we resort to it if we cannot find a suitable range that can accommodate the whole VMA" << std::endl << '\n';
                    log_stream << "[SpOTAllocator] BEFORE: " << "largest_range = " << largestest_range << " bytes -- largest_range_index = " << largestest_range_index << std::endl;
#endif
                    largestest_range = (contiguity_map[i].bounds - contiguity_map[i].ppn);
                    largestest_range_index = i;
#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
                    log_stream << "[SpOTAllocator] AFTER: " << "largest_range = " << largestest_range << " bytes -- largest_range_index = " << largestest_range_index << std::endl;
#endif
                }

                visited_ranges++;
                i = (i + 1) % contiguity_map.size();
            }

            // If we did not find a suitable range
            if (!found) {
#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
                log_stream << "[SpOTAllocator] Did not find a suitable range, trying to use the largest range: " << largestest_range << " at index: " << largestest_range_index << std::endl;
#endif
                if (largestest_range_index != -1) 
                {
                    // Use the largestest range
                    IntPtr first_ppn = contiguity_map[largestest_range_index].ppn;
#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
                    assert(first_ppn != static_cast<IntPtr>(-1));
#endif 
                    current_offset = static_cast<int64_t>(address >> 12) - static_cast<int64_t>(first_ppn);
                    Policy::setPhysicalOffset(app_id, vma_index, current_offset, this);
                    contiguity_map_pointer = largestest_range_index;
                }
                else 
                {
                    // We did not find any suitable range, so we cannot allocate memory
                    std::cerr << "[SpOTAllocator] [ERROR]: Could not find a suitable range in the contiguity map for address: " << address << " , so we cannot allocate memory" << std::endl;
                    return make_pair((UInt64)-1, 12);
                }
            }
        }
        else 
        {
#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
            log_stream << "[SpotAllocator] We have already allocated this VMA, so we can use the existing offset: " << current_offset << std::endl;
#endif
        }

        // Calculate the requested ppn based on the address and the offset
        IntPtr requested_ppn = address / 4096 - current_offset;

#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
        log_stream << "[SpotAllocator] Requested PPN: " << requested_ppn << std::endl;
        log_stream << "[SpotAllocator] Current Offset: " << current_offset << std::endl;
        Policy::log_VMA_specs(app_id, vma_index, this);
#endif

        auto buddy_request = buddy_allocator->checkIfFree(requested_ppn, false); // Check if a 4KiB page is free, and if so: DON'T ALLOCATE IT
#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
        log_stream << "[SpotAllocator] Is requested_ppn: " << requested_ppn << " Free? - " << (requested_ppn ? "yes" : "no") << std::endl;
#endif

        if (buddy_request)
        {
            final_physical_page = requested_ppn << 12;
            final_ppn = requested_ppn;

            bool succ_incremented_ctr = Sim()->getMimicOS()->incrementSuccessfulOffsetBasedAllocations(app_id, address);
            assert(succ_incremented_ctr);

#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
            log_stream << "[SpotAllocator] Alloc'd 4KiB page at base phys addr: " << final_physical_page << std::endl;
#endif
        }
        else {
            // The page is not free, so we cannot allocate it
#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
            log_stream << "[SpotAllocator] Requested PPN: " << requested_ppn << " is not free, cannot allocate exactly this PPN" << std::endl;
#endif

            final_ppn = buddy_allocator->allocate(size, address, app_id);
            final_physical_page = final_ppn << 12; // Convert the PPN to a base address (PA)

#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
            log_stream << "[SpotAllocator] !buddy_request - Alloc'd PPN: " << final_ppn << std::endl;
#endif

#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
            if (final_physical_page == static_cast<IntPtr>(-1))
            {
                log_stream << "[SpotAllocator] Buddy allocator returned -1, cannot allocate memory" << std::endl;
                exit(1);
            }
            else {
                log_stream << "[SpotAllocator] Buddy allocator returned physical page: " << final_physical_page << std::endl;
            }
#endif
        }

        int index_in_contiguity_map = -1;
        int counter = 0;

        // Update the contiguity map 
        for (auto &range : contiguity_map)
        {

#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
            log_stream << "[SpotAllocator] Checking contiguity map range: " << range.ppn << " - " << range.bounds << std::endl;            
#endif
            if (range.ppn <= final_ppn && range.bounds >= final_ppn)
            {
                index_in_contiguity_map = counter;
                break;
            }
            counter++;
        }

        // Some other process has allocated the page, so we cannot allocate it
        if (index_in_contiguity_map == -1)
        {
#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
            log_stream << "[SpotAllocator] Error: Could not find the range in the contiguity map for physical page: " << final_physical_page <<  " - PPN = " << final_ppn << std::endl;
            log_stream << "[SpotAllocator] Fallback 4KiB default alloc, w/o using the contiguity map" << std::endl;
            log_stream << "[SpotAllocator] PPN returned by givePageFast: " << final_ppn << std::endl; // TODO: Vlad Validate
#endif
            return make_pair(final_ppn, 12);
        }
        else
        {
#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
            log_stream << "[SpotAllocator] Found range in contiguity map at index: " << index_in_contiguity_map << std::endl;
#endif
        }

        if (contiguity_map[index_in_contiguity_map].bounds == final_ppn && contiguity_map[index_in_contiguity_map].ppn == final_ppn)
        {
            // If the range is exactly the allocated page, we can just remove it from the contiguity map
            contiguity_map.erase(contiguity_map.begin() + index_in_contiguity_map);
        }
        else if (contiguity_map[index_in_contiguity_map].ppn == final_ppn)
        {
            // If the range is exactly the allocated page, we can just update the ppn
            contiguity_map[index_in_contiguity_map].ppn = final_ppn + 1;
#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
            log_stream << "[SpotAllocator] Allocated page: " << final_physical_page << "- PPN: " << final_ppn << std::endl;
            log_stream << "[SpotAllocator] New range: " << contiguity_map[index_in_contiguity_map].ppn << " - " << contiguity_map[index_in_contiguity_map].bounds << std::endl;
#endif
        }
        else  if (contiguity_map[index_in_contiguity_map].bounds == final_ppn)
        {
        // If the range is exactly the allocated page, we can just update the bounds
        contiguity_map[index_in_contiguity_map].bounds = final_ppn - 1;
#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
        log_stream << "[SpotAllocator] Allocated page: " << final_physical_page << "- PPN: " << final_ppn << std::endl;
        log_stream << "[SpotAllocator] New range: " << contiguity_map[index_in_contiguity_map].ppn << " - " << contiguity_map[index_in_contiguity_map].bounds << std::endl;
#endif
        }
        else 
        {
        PhysicalRange new_range_before;
        new_range_before.ppn = contiguity_map[index_in_contiguity_map].ppn; // Set the ppn to the first ppn of the range
        new_range_before.bounds = final_ppn - 1; // Set the bounds to the last ppn of the range before the allocated page
        
        PhysicalRange new_range_after;
        new_range_after.ppn = final_ppn + 1; // Set the ppn to the first ppn of the range after the allocated page
        new_range_after.bounds = contiguity_map[index_in_contiguity_map].bounds; // Set the bounds to the last ppn of the range after the allocated page

        // Update the contiguity map with the new ranges
        contiguity_map[index_in_contiguity_map] = new_range_before; // Update the range before the allocated page
        contiguity_map.insert(contiguity_map.begin() + index_in_contiguity_map + 1, new_range_after); // Insert the range after the allocated page

#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
        log_stream << "[SpotAllocator] Allocated page: " << final_physical_page << "- PPN: " << final_ppn << std::endl;
        log_stream << "[SpotAllocator] New range before: " << new_range_before.ppn << " - " << new_range_before.bounds << std::endl;
        log_stream << "[SpotAllocator] New range after: " << new_range_after.ppn << " - " << new_range_after.bounds << std::endl;
#endif
        }

        return make_pair(final_ppn, 12); 
    }


    void deallocate(UInt64 index, UInt64 app_id) override
    {
        // No deallocation for SpotAllocator for now
    }

    std::vector<Range> allocate_ranges(IntPtr, IntPtr, int) override
    {
        // Not implemented - just return an empty vector
        std::vector<Range> ranges;
        return ranges;
    }

    void fragment_memory(double target_fragmentation) override
    {
        // 1024 PPNs of 4KiB = 4MiB
        const int num_ppns = 1024;
        int num_4mb_pages = buddy_allocator->getTotalPages() / 1024;
        std::cerr << "[SpotAllocator] Total number of 4KiB pages: " << buddy_allocator->getTotalPages() << std::endl;

        // Fragment the contiguity map to ensure that we have a good starting point
        int num_poisoned_pages = target_fragmentation * num_4mb_pages; // Randomly poison <target_fragmentation> of pages in the contiguity map

#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
        log_stream << "[SpotAllocator] Poisoning " << target_fragmentation * 100.0 << " \% of pages - num_poisoned_pages = " << num_poisoned_pages << std::endl;
#endif

        std::cerr << "[SpotAllocator] Starting fragmenting memory by poisonign " << num_poisoned_pages << " 4MiB pages, which represents " << target_fragmentation * 100.0 << " percent" <<  std::endl;
        int skip_ctr = 0; // Counter for skipped allocations
        
        for (int i = 0; i < num_poisoned_pages; ++i) {
            if (i <= 5) {
                std::cerr << "[SpotAllocator] Poisoning page " << i + 1 << " / " << num_poisoned_pages << std::endl;
                debug_contiguity_map(contiguity_map); // Debug the contiguity map before poisoning TODO: remove
            }

            // Randomly select a page to poison
            int sz = contiguity_map.size();
            if (sz == 0) {
#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
                log_stream << "[SpotAllocator] Error: Contiguity map is empty, cannot poison pages anymore." << std::endl;
#endif
                break; // If the contiguity map is empty, we cannot poison any pages
            }

            int poisoned_range = largest_region_in_contiguity_map(contiguity_map, sz); // Get the largest region in the contiguity map
            if (i <= 5) { 
                std::cerr << "[SpotAllocator] Largest region in contiguity map at iteration " << i << " is: " << poisoned_range << std::endl;
            }

            // if (i % DEBUG_ITERATION == 0) {
            //     std::cerr << "[SpotAllocator] Poisoning iteration: " << i << " - Poisoning range ID: " << poisoned_range << std::endl;
            // }

            [[maybe_unused]] int poisoned_range_size = (contiguity_map[poisoned_range].bounds - contiguity_map[poisoned_range].ppn) / num_ppns; // Calculate the size of the poisoned range in 4MiB pages

#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
            log_stream << "[SpotAllocator] sz: " << sz << std::endl;
            log_stream << "[SpotAllocator] Iteration: " << i << " Poisoning range ID: " << poisoned_range << std::endl;
#endif

            if (contiguity_map[poisoned_range].ppn == contiguity_map[poisoned_range].bounds) {
                // If the range is a single page, we can just remove it from the contiguity map
                contiguity_map.erase(contiguity_map.begin() + poisoned_range); // Remove the range from the contiguity map
#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
                log_stream << "[SpotAllocator] Erasing single page: Poisoning range ID: " << poisoned_range << std::endl;
#endif
            }
            else if (contiguity_map[poisoned_range].ppn + num_ppns == contiguity_map[poisoned_range].bounds) {
                // If the range is a single page, we can just remove it from the contiguity map
                contiguity_map.erase(contiguity_map.begin() + poisoned_range); // Remove the range from the contiguity map
#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
                log_stream << "[SpotAllocator] Erasing single page: Poisoning range ID: " << poisoned_range << std::endl;
#endif
            }
            else {
                int num_4mb_pages = (contiguity_map[poisoned_range].bounds - contiguity_map[poisoned_range].ppn) / num_ppns;
#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
                // std::cerr <<  "Range Start: " << contiguity_map[poisoned_range].ppn << " - Range End: " << contiguity_map[poisoned_range].bounds   << "[SpotAllocator] num_4mb_pages = " << num_4mb_pages << std::endl;
#endif

                // Skip if too small
                if (num_4mb_pages == 0) {
#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
                skip_ctr ++;
                log_stream << "[SpotAllocator] Skipping too small allocation" << std::endl; // TODO: Vlad Validate
#endif
                    std::cerr << "[SpotAllocator] [ERROR]: num_4mb_pages is 0, cannot poison pages anymore in iteration #" << i << std::endl;
                    break; // Largest range < 4MiB, so jut stop spliiting
                }
                else if (num_4mb_pages <= 2) {
                    // If the range is a single page, we can just remove it from the contiguity map
#if DEBUG_SPOT_ALLOCATOR >= DEBUG_BASIC
                    log_stream << "[SpotAllocator] num_4mb_pages is: " << num_4mb_pages << " - splitting into 1 x 4MiB page and the rest of the region in iteration #" << i << std::endl;
#endif
                    contiguity_map[poisoned_range].bounds = contiguity_map[poisoned_range].ppn + num_ppns; // Reserve 1 x 1 MiB page, the other one gets poisoned
                }
                else {

                    // Random page offset in range [1, num_4mb_pages - 2] to avoid edge poisoning
                    int rand_page_offset = 1 + (rand() % (num_4mb_pages - 2));
                    IntPtr rand_page = contiguity_map[poisoned_range].ppn + static_cast<IntPtr>(rand_page_offset) * num_ppns;

                    // Now build ranges safely
                    PhysicalRange left_range = {
                        .ppn = contiguity_map[poisoned_range].ppn,
                        .bounds = rand_page - 1
                    };

                    PhysicalRange right_range = {
                        .ppn = rand_page + num_ppns + 1,
                        .bounds = contiguity_map[poisoned_range].bounds
                    };

                    // Defensive checks
                    if (left_range.ppn >= left_range.bounds || right_range.ppn >= right_range.bounds) {
                        std::cerr << "SKIPPING bad split: left or right invalid (overlap or reversed)" << std::endl;
                        continue;
                    }

                    // Insert the new ranges
                    contiguity_map[poisoned_range] = left_range;
                    contiguity_map.insert(contiguity_map.begin() + poisoned_range + 1, right_range);
    // #ifdef DEBUG
    //                 log_stream << "[SpotAllocator] num_4mb_pages = " << num_4mb_pages << std::endl;
    //                 log_stream << "[SpotAllocator] After posioning - new left range: PPN: " << left_range.ppn << " Bounds: " << left_range.bounds << std::endl;
    //                 log_stream << "[SpotAllocator] After posioning - new right range: PPN: " << right_range.ppn << " Bounds: " << right_range.bounds << std::endl;
    // #ifdef DEBUG_CONTIGUITY_MAP
    //                 debug_contiguity_map(log_stream, contiguity_map);
    // #endif
    // #endif
                }
            }
        }

        std::cerr << "[SpotAllocator] skip_counter = " << skip_ctr <<  std::endl;
        std::cerr << "[SpotAllocator] Finished fragmenting memory" << std::endl;
    }

    UInt64 givePageFast(UInt64 size, UInt64 address = 0, UInt64 app_id = (UInt64)-1) override
    {
        UInt64 physical_page = buddy_allocator->allocate(size, address, app_id);
	    return physical_page;
    }

protected:
    BuddyType* buddy_allocator;

private:
    friend Policy; // so Policy can access private members if needed

    int m_max_order;

    std::vector<PhysicalRange> contiguity_map;
    int contiguity_map_pointer = 0;

    static void debug_contiguity_map(const std::vector<PhysicalRange>& contiguity_map) {
        std::cerr << "[SpotAllocator] Contiguity Map:" << std::endl;
        int i = 0;
        for (const auto& range : contiguity_map) {
            std::cerr << "4MiB pages in entry # " << i << " " << (range.bounds - range.ppn) / 1024 << std::endl;
            i ++;
        }

        std::cerr << "---------------------\n";
        std::cerr << "---------------------\n";
        std::cerr << "---------------------\n";
    }

    static int largest_region_in_contiguity_map(const std::vector<PhysicalRange>& contiguity_map, int sz) {
        int max_size = -1, max_i = -1;
        for (int i = 0; i < sz; ++i) {
            int region_size = (contiguity_map[i].bounds - contiguity_map[i].ppn) / 1024; // 4MiB pages
            if (max_size < region_size) {
                max_i = i;
                max_size = region_size;
            }
        }

        return max_i;
    }
};

