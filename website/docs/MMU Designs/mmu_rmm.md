
# Range Mappings MMU Design 

In this section, we will discuss the implementation of the Range Mappings MMU design. The Range Mappings MMU design is based on the work by 

We will describe only the differences between the Range Mappings MMU design and the baseline MMU design. For a detailed description of the baseline MMU design, please refer to the [Baseline MMU Design](./mmu_baseline.md) section.



## New Components

- **Range Lookaside Buffer (RLB):** A range-based cache that stores base-bound pairs for fast address translation.
- **Range Table Walker:** A component that walks the range table to find the base-bound pairs for address translation and inserts them into the RLB.


## Address Translation Flow

1. **TLB Lookup:** The MMU first checks the TLB hierarchy for a translation. If a TLB hit occurs, the physical address is returned.
2. **Range Walk:** If a TLB miss occurs, the MMU performs a range walk to check for a range mapping. 

```cpp
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
#ifdef DEBUG_MMU
				log_file << "[MMU] Range Hit: " << range_hit << " at time: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
				log_file << "[MMU] VPN Start: " << vpn_start << std::endl;
				log_file << "[MMU] PPN Offset: " << ppn_offset << std::endl;
				log_file << "[MMU] Current VPN: " << current_vpn << std::endl;
				log_file << "[MMU] Final PPN: " << (current_vpn - vpn_start) + ppn_offset << std::endl;
#endif
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
#ifdef DEBUG_MMU
				log_file << "[MMU] New time after charging TLB and Range latency: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
#endif
			}
```

### Range Walk

The range walk is performed by the `performRangeWalk` function and  returns a tuple containing the charged range walk latency, the virtual page number (VPN), and the offset.

* The function first checks if the address is present in the Range Lookaside Buffer (RLB). If it is a hit, it returns the VPN and offset.
* If it is a miss, it checks the Range Table for the address. If the address is found in the Range Table, it inserts the entry into the RLB and returns the VPN and offset.
* If the address is not found in the Range Table, it returns -1 for both VPN and offset.
* The function also charges the range walk latency based on the number of memory accesses required to traverse the range table.
* The accesses are serialized since the walk to the range table (e.g., the B+ tree) is serialized. 




```cpp
	std::tuple<SubsecondTime, IntPtr, int> RangeMMU::performRangeWalk(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count)
	{

		SubsecondTime time = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

		SubsecondTime charged_range_walk_latency = SubsecondTime::Zero();
		auto hit_rlb = range_lb->access(Core::mem_op_t::READ, address, count);

		RangeTable *range_table = Sim()->getMimicOS()->getRangeTable(core->getThread()->getAppId());

		if (!hit_rlb.first) // Miss in RLB
		{
#ifdef DEBUG_MMU
			log_file << "Miss in RLB for address: " << address << std::endl;
#endif
			// Check if the address is in the range table
			auto result = range_table->lookup(address);
			if (get<0>(result) != NULL) // TreeNode* is not NULL
			{
				// We found the key inside the range table
#ifdef DEBUG_MMU
				log_file << "Key found for address: " << address << " in the range table" << std::endl;
#endif
				Range range;
				range.vpn = get<0>(result)->keys[get<1>(result)].first;
				range.bounds = get<0>(result)->keys[get<1>(result)].second;
				range.offset = get<0>(result)->values[get<1>(result)].offset;

#ifdef DEBUG_MMU
				log_file << "VPN: " << range.vpn << " Bounds: " << range.bounds << " Offset: " << range.offset << std::endl;
#endif
				// Insert the entry in the RLB
				for (auto &address : get<2>(result))
				{

					translationPacket packet;
					packet.address = address;
					packet.eip = eip;
					packet.instruction = false;
					packet.lock_signal = lock;
					packet.modeled = modeled;
					packet.count = count;
					packet.type = CacheBlockInfo::block_type_t::RANGE_TABLE;

					charged_range_walk_latency += accessCache(packet, charged_range_walk_latency);
				}
				range_lb->insert_entry(range);
			}
			else // We did not find the key inside the range table
			{
		
#ifdef DEBUG_MMU
				log_file << "No key found for address: " << address << " in the range table" << std::endl;
#endif
				return std::make_tuple(charged_range_walk_latency, -1, -1);
			}
			return std::make_tuple(charged_range_walk_latency, hit_rlb.second.vpn, hit_rlb.second.offset);
		}
		else
		{
#ifdef DEBUG_MMU
			log_file << "Hit in RLB for address: " << address << std::endl;
			log_file << "VPN: " << hit_rlb.second.vpn << " Bounds: " << hit_rlb.second.bounds << " Offset: " << hit_rlb.second.offset << std::endl;
#endif
			return std::make_tuple(charged_range_walk_latency, hit_rlb.second.vpn, hit_rlb.second.offset);
		}
	}

```
### Finding my corresponding VMA

```cpp
	void RangeMMU::discoverVMAs()
	{
		// We need to discover the VMAs in the application
	}

	VMA RangeMMU::findVMA(IntPtr address)
	{
		int app_id = core->getThread()->getAppId();
		std::vector<VMA> vma_list = Sim()->getMimicOS()->getVMA(app_id);

		for (UInt32 i = 0; i < vma_list.size(); i++)
		{
			if (address >= vma_list[i].getBase() && address < vma_list[i].getEnd())
			{
#ifdef DEBUG_MMU
				log_file << "VMA found for address: " << address << " in VMA: " << vma_list[i].getBase() << " - " << vma_list[i].getEnd() << std::endl;
#endif
				return vma_list[i];
			}
		}
		assert(false);
		return VMA(-1, -1);
	}

