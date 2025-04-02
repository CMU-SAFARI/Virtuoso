---
sidebar_position: 2
---

# Baseline MMU



This document provides an in-depth walkthrough of the Memory Management Unit (MMU) implementation. 

We will explore:

- The different components of the MMU
- How the MMU handles address translation


### Constructor and Destructor

The MMU needs backward pointers to the core, the memory manager and the time model. 

```cpp
MemoryManagementUnit::MemoryManagementUnit(Core *_core, MemoryManager *_memory_manager, ShmemPerfModel *_shmem_perf_model, String _name, MemoryManagementUnitBase *_nested_mmu)
```


- **instantiatePageTableWalker()**
  
The MMU uses two components to accelerate page table walks:

(i) The number of page table walkers, which can be configured to be more than one to handle multiple page table walks in parallel.
(ii) The page walk caches to store the results of page table walks to avoid redundant walks.

```cpp
void MemoryManagementUnit::instantiatePageTableWalker()
```

- **instantiateTLBSubsystem()**
  Sets up the TLB (Translation Lookaside Buffer) hierarchy which is crucial for speeding up address translation by storing recent translations. The TLB hierarchy can be configured to have multiple levels of TLBs and different number of TLBs per level.

```cpp
void MemoryManagementUnit::instantiateTLBSubsystem()
```

- **registerMMUStats()**
  Registers various statistics like page faults and translation latencies, which are useful for performance monitoring and debugging.

```cpp
void MemoryManagementUnit::registerMMUStats()
```

### Address Translation

- **performAddressTranslation()**

  The function is called by the memory manager and offloads the address translation to the MMU.

  Conducts the translation of a virtual address to a physical address, considering whether the address is for data or instructions, and updates performance metrics accordingly. The function returns the physical address and the time taken for translation. 

```cpp
IntPtr MemoryManagementUnit::performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count)
```
- **Accessing the TLB Subsystem**

  The TLB subsystem is accessed through the MMU. The TLB subsystem is responsible for storing recent translations to speed up address translation. We perform a lookup in the TLB hierarchy to find the translation for the virtual address.

  1) We iterate through the TLB hierarchy across all levels and all TLBs at each level.
```cpp
// We iterate through the TLB hierarchy to find if there is a TLB hit
for (UInt32 i = 0; i < tlbs.size(); i++){
  for (UInt32 j = 0; j < tlbs[i].size(); j++){
    bool tlb_stores_instructions = (tlbs[i][j]->getType() == TLBtype::Instruction) || (tlbs[i][j]->getType() == TLBtype::Unified);

    // If the TLB stores instructions, we need to check if the address is an instruction address
    if (tlb_stores_instructions && instruction){
      // @kanellok: Passing the page table to the TLB lookup function is a legacy from the old TLB implementation. 
      // It is not used in the current implementation.

      tlb_block_info = tlbs[i][j]->lookup(address, time, count, lock, eip, modeled, count, NULL);

      if (tlb_block_info != NULL){
        hit_tlb = tlbs[i][j]; // Keep track of the TLB that hit
        hit_level = i; // Keep track of the level of the TLB that hit
        hit = true; // We have a hit
        goto HIT; // @kanellok: This is ultra bad practice, but it works
      }
    }
  }
}
```

  3) If there is a TLB hit, we keep track of the TLB that hit and the block info.
```cpp
CacheBlockInfo *tlb_block_info // This variable will store the translation information if there is a TLB hit
```




- **Accessing the Page Table after a miss in the TLB subsystem**

  The page table is accessed through the MMU. The page table is a crucial data structure that maps virtual addresses to physical addresses. The MMU requires a pointer to the page table to perform address translation. The page table is looked up after every L2 TLB miss.


1) We need to keep track of the total walk latency and the total fault latency (if there was a fault). 
The physical page number that we will get from the PTW is stored in `ppn_result`.

```cpp
// We need to keep track of the total walk latency and the total fault latency (if there was a fault)
SubsecondTime total_walk_latency = SubsecondTime::Zero();
SubsecondTime total_fault_latency = SubsecondTime::Zero();
    // This is the physical page number that we will get from the PTW
IntPtr ppn_result;
```

2) We only trigger the PTW if there was a TLB miss. We keep track of the time before the PTW starts so 
that we search for a free slot in the MSHRs for the PT walker. 
* First, we find if there is any delay because of all the walkers being busy.
* We switch the time to the time when the PT walker is allocated so that we start the PTW at that time.

```cpp
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
  #ifdef DEBUG_MMU
    log_file << "[MMU] New time after charging the PT walker allocation delay: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
  #endif
```
3) We retrieve the page table for the application that is currently running on the core. We then perform the PTW and get the PTW latency, PF latency, Physical Address, and Page Size as a tuple. The `restart_ptw` variable is used to indicate whether the PTW should be automatically restarted in cases of a page fault.

```cpp
// returns PTW latency, PF latency, Physical Address, Page Size as a tuple
int app_id = core->getThread()->getAppId();
PageTable* page_table = Sim()->getMimicOS()->getPageTable(app_id);

bool restart_ptw = true;
auto ptw_result = performPTW(address, modeled, count, false, eip, lock, page_table, restart_ptw);
```

4) We need to calculate the total walk latency and the total fault latency.
* If the walk caused a page fault, we need to charge the page fault latency.
* In the baseline Virtuoso+Sniper, we charge a static page fault latency for all page faults (e.g., 1000 cycles).
* We update the translation statistics with the total walk latency and the total fault latency.

```cpp
total_walk_latency = get<0>(ptw_result); // Total walk latency is only the time it takes to walk the page table (excluding page faults)	
if (count)
{
  translation_stats.total_walk_latency += total_walk_latency;
  translation_stats.page_table_walks++;
}

// If the walk caused a page fault, we need to charge the page fault latency
bool caused_page_fault = get<1>(ptw_result);


if (caused_page_fault)
{
  SubsecondTime m_page_fault_latency = Sim()->getMimicOS()->getPageFaultLatency();	
  if (count)
  {
    translation_stats.page_faults++;
    translation_stats.total_fault_latency += m_page_fault_latency;
  }
  total_fault_latency = m_page_fault_latency;
}
```
5) We need to calculate when the PTW will be completed to update the completion time of the PT walker entry.
* We set the completion time to the time before the PTW starts + delay + total walk latency + total fault latency. We then allocate the PT walker entry. The completion time of each PT walker is used to track the time when the PTW is completed so that 
we charge the corresponding latencies. 
* If the PTW caused a page fault, we need to set the time to the time after the PTW is completed. 
We treat the fault as a pseudo-instruction and queue it in the performance model. The pseudo-instruction serializes the page fault routine and charges the page fault latency as if the ROB was stalled for that time (which would cause a full stall in the pipeline).
In this case, we also update the time so that the memory manager sends the request to the cache hierarchy after the Page Fault Routine is completed.
* If there was no page fault, we set the time to the time after the PTW is completed. Again, we update the time so that the memory manager sends the request to the cache hierarchy after the PTW is completed.

```cpp
/*
We need to set the completion time:
1) Time before PTW starts
2) Delay because of all the walkers being busy
3) Total walk latency
4) Total fault latency
*/

pt_walker_entry.completion_time = time_for_pt + delay + total_walk_latency + total_fault_latency;
pt_walkers->allocate(pt_walker_entry);


ppn_result = get<2>(ptw_result);
page_size = get<3>(ptw_result);

/* 
We need to set the time to the time after the PTW is completed. 
This is done so that the memory manager sends the request to the cache hierarchy after the PTW is completed
*/
if (caused_page_fault){
  PseudoInstruction *i = new PageFaultRoutineInstruction(total_fault_latency);
  getCore()->getPerformanceModel()->queuePseudoInstruction(i);
  shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, pt_walker_entry.completion_time);
}
else{
  shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, pt_walker_entry.completion_time);
}

#ifdef DEBUG_MMU
  log_file << "[MMU] New time after charging the PT walker completion time: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD) << std::endl;
#endif

}
```


**Allocating a new entry in the TLB subsystem**

After the page table walk or a TLB hit at a higher level (e.g., L2 TLB), we need to allocate the translation in the TLB that missed. We iterate through the TLB hierarchy to find the TLB that missed and allocate the translation in that TLB.

* We only allocate the translation if the TLB supports the page size of the translation and the TLB is an "allocate on miss" TLB.
* We also check if there are any evicted translations from the previous level and allocate them in the current TLB.
For example, if the L1 TLB misses, gets filled up and evicts some translations, we need to allocate these evicted translations in the L2 TLB which acts as a victim. 

```cpp

for (int i = 0; i < tlb_levels; i++)
{
  // We will check where we need to allocate the page

  for (UInt32 j = 0; j < tlbs[i].size(); j++)
  {
    // We need to check if there are any evicted translations from the previous level and allocate them
    if ((i > 0) && (evicted_translations[i - 1].size() != 0))
    {
      tuple<bool, IntPtr, int> result;

#ifdef DEBUG_MMU
      log_file << "[MMU] There are evicted translations from level: " << i - 1 << std::endl;
#endif
      // iterate through the evicted translations and allocate them in the current TLB
      for (UInt32 k = 0; k < evicted_translations[i - 1].size(); k++)
      {
#ifdef DEBUG_MMU
        log_file << "[MMU] Evicted Translation: " << get<0>(evicted_translations[i - 1][k]) << std::endl;
#endif
        // We need to check if the TLB supports the page size of the evicted translation
        if (tlbs[i][j]->supportsPageSize(page_size))
        {
#ifdef DEBUG_MMU
          log_file << "[MMU] Allocating evicted entry in TLB: Level = " << i << " Index =  " << j << std::endl;
#endif

          result = tlbs[i][j]->allocate(get<0>(evicted_translations[i - 1][k]), time, count, lock, get<1>(evicted_translations[i - 1][k]), ppn_result);

          // If the allocation was successful and we have an evicted translation, 
          // we need to add it to the evicted translations vector for

          if (get<0>(result) == true)
          {
            evicted_translations[i].push_back(make_tuple(get<1>(result), get<2>(result)));
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
#ifdef DEBUG_MMU
      log_file << "[MMU] Allocating in TLB: Level = " << i << " Index = " << j << " with page size: " << page_size << " and VPN: " << (address >> page_size) << std::endl;
#endif
      tuple<bool, IntPtr, int> result;

      result = tlbs[i][j]->allocate(address, time, count, lock, page_size, ppn_result);
      if (get<0>(result) == true)
      {
        evicted_translations[i].push_back(make_tuple(get<1>(result), get<2>(result)));
      }
    }
  }
}
```

## Debugging

The `DEBUG_MMU` macro can be enabled to log detailed debug messages at various points in the address translation process to help understand and trace the steps involved. DO NOT ENABLE THIS MACRO WHEN YOU ARE RUNNING A SIMULATION. IT WILL SLOW DOWN THE SIMULATION AND GENERATE TONS OF DATA. 

```cpp


