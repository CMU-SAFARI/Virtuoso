---
sidebar_position: 2
---

# Part-of-Memory TLB (POM-TLB) MMU Design

In this section, we will discuss the implementation of the Part-of-Memory TLB (POM-TLB) MMU design. The POM-TLB design is a software-managed TLB that uses a software-managed L3 TLB to reduce the overhead of address translation. The POM-TLB design is based on the work by [Papadopoulou et al.](https://ieeexplore.ieee.org/document/8192494).

We will describe only the differences between the POM-TLB and the baseline MMU design. For a detailed description of the baseline MMU design, please refer to the [Baseline MMU Design](./mmu_baseline.md) section.



This code implements the handling of TLB (Translation Lookaside Buffer) misses in a memory management unit (MMU). It differentiates between hardware TLB and software TLB, and performs a lookup in the software TLB when a hardware TLB miss occurs. Below are the key differences and functionalities explained:

1. **Hardware TLB Miss Handling**:
    - When a TLB miss occurs (`!tlb_hit`), the code initiates a search in the software TLB structures for the requested address.
    - Debug logs are generated (if `DEBUG_MMU` is enabled) to trace the TLB miss and subsequent software TLB lookup process.

2. **Software TLB Lookup**:
    - The software TLB is organized by page sizes, and the code iterates through all possible page sizes (`number_of_page_sizes`) to perform the lookup.
    - For each page size:
      - The corresponding software TLB (`m_pom_tlb[page_size]`) is queried using the `lookup` method.
      - Debug logs provide detailed information about the lookup process, including the page size, tag, set index, and base address used for the lookup.
      - The memory access latency for the software TLB is simulated and recorded.

```cpp
#ifdef DEBUG_MMU
			log_file << "[MMU] TLB Miss, checking software TLB" << std::endl;
#endif
            // We need to check multiple software TLBs (one for each page size)
			for (int page_size = 0; page_size < number_of_page_sizes; page_size++)
			{
#ifdef DEBUG_MMU
				log_file << "[MMU] Searching software TLB for page size: "
				         << page_size_list[page_size] << std::endl;
#endif
				TLB* pom = m_pom_tlb[page_size];
				software_tlb_block_info = pom->lookup(address, time, count, lock, eip, modeled, count, NULL);

#ifdef DEBUG_MMU
				log_file << "[MMU] Software TLB Hit ? "
				         << (software_tlb_block_info != NULL)
				         << " at TLB: " << pom->getName() << std::endl;
#endif

				// Simulate the memory access that the software TLB structure does
				translationPacket packet;
				packet.eip          = eip;
				packet.instruction  = false;
				packet.lock_signal  = lock;
				packet.modeled      = modeled;
				packet.count        = count;
				packet.type         = CacheBlockInfo::block_type_t::TLB_ENTRY;

				IntPtr tag;
				UInt32 set_index;
				pom->getCache().splitAddressTLB(address, tag, set_index, page_size_list[page_size]);

#ifdef DEBUG_MMU
				log_file << "[MMU] Software TLB Lookup: " << address
				         << " at page size: " << page_size_list[page_size]
				         << " with tag: " << tag
				         << " and set index: " << set_index
				         << " and base address: "
				         << software_tlb_base_register[page_size]*4096 << std::endl;
#endif
				packet.address = software_tlb_base_register[page_size]*4096
				                 + pom->getAssoc()* pom->getEntrySize() * set_index;

#ifdef DEBUG_MMU
				log_file << "[MMU] Software TLB Address: " << packet.address << std::endl;
#endif

				software_tlb_latency[page_size] = accessCache(packet, time_before_software_tlb);

#ifdef DEBUG_MMU
				log_file << "[MMU] Software TLB Latency: " 
				         << software_tlb_latency[page_size]
				         << " at page size: " << page_size_list[page_size] 
				         << std::endl;
#endif

}
```

3. **Software TLB Hit**:
    - If a software TLB hit occurs (`software_tlb_block_info != NULL`):
      - The physical page number (PPN) and page size of the hit are retrieved.
      - The latency of the software TLB hit is recorded as `final_software_tlb_latency`.
      - Debug logs provide detailed information about the hit, including the PPN, VPN, tag, and latency.


```cpp
if (software_tlb_block_info != NULL)
{
    // We have a software TLB hit
    final_software_tlb_latency = software_tlb_latency[page_size];
    software_tlb_hit = true;
    ppn_result = software_tlb_block_info->getPPN();
    page_size_result = software_tlb_block_info->getPageSize();
#ifdef DEBUG_MMU
    log_file << "[MMU] Software TLB Hit at page size: "
                << page_size_result << std::endl;
    log_file << "[MMU] Software TLB Hit PPN: " << ppn_result << std::endl;
    log_file << "[MMU] Software TLB Hit VPN: "
                << (address >> page_size_result) << std::endl;
    log_file << "[MMU] Software TLB Hit Tag: " << tag << std::endl;
    log_file << "[MMU] Software TLB Hit Latency: "
                << final_software_tlb_latency << std::endl;
#endif
}
```

4. **Software TLB Miss**:

- If all software TLBs miss (`!software_tlb_hit`):
- The maximum latency among all software TLB lookups is calculated and used as the `final_software_tlb_latency`.
- Debug logs indicate the software TLB miss and the calculated latency.

```cpp
			if (!software_tlb_hit)
			{
				// If all software TLBs missed, we take the max of the latencies
#ifdef DEBUG_MMU
				log_file << "[MMU] Software TLB Miss" << std::endl;
#endif
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

#ifdef DEBUG_MMU
			log_file << "[MMU] Final Software TLB Latency: "
			         << final_software_tlb_latency << std::endl;
#endif
```

5. **Latency Accumulation**:
    - Whether a software TLB hit or miss occurs, the final latency is accumulated into the `translation_stats.software_tlb_latency` if the `count` flag is set.

6. **Debugging Support**:
    - The code includes extensive debugging support (enabled via `DEBUG_MMU`) to log detailed information about the TLB lookup process, hits, misses, and latencies for both hardware and software TLBs. DO NOT ENABLE DEBUGGING WHEN RUNNING EXPERIMENTS.








