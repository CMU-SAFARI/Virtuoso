

# TLB Subsystem Overview

This document describes the **TLBHierarchy** class and its role in instantiating and configuring a hierarchy of Translation Lookaside Buffers (TLBs). We also discuss how this subsystem enables TLB prefetching and page-size prediction.

Below, you will find the implementation of the `TLBHierarchy` constructor split into logical sections with explanatory comments in between. This should help you understand how the TLB hierarchy is created, configured, and managed within the memory subsystem.

---

## Table of Contents

1. [Introduction](#introduction)
2. [TLBHierarchy Class](#tlbhierarchy-class)
   1. [Constructor](#constructor)
   2. [predictPagesize()](#predictpagesize)
   3. [predictionResult()](#predictionresult)
   4. [Destructor](#destructor)
3. [Conclusion](#conclusion)

---

## Introduction

The code below shows how a TLB hierarchy is built from a configuration (using `Sim()->getCfg()` to read parameters). Each "level" in the TLB hierarchy can have multiple TLB structures (data, instruction, or unified), and there may be an optional prefetcher queue (PQ). The code also integrates with a **page size predictor** to dynamically guess the most likely page size for each instruction pointer (EIP).

---

## TLBHierarchy Class

### Constructor

#### Includes and Namespaces

```cpp
#include "tlb_subsystem.h"
#include "tlb.h"
#include <boost/algorithm/string.hpp>
#include "config.hpp"
#include "stride_prefetcher.h"
#include "tlb_prefetcher_factory.h"
#include "pagesize_predictor_factory.h"
#include "dvfs_manager.h"

using namespace boost::algorithm;
Explanation

Brings in required headers for TLB structures, configuration parsing, prefetcher factories, DVFS management, etc.
We use boost::algorithm primarily for string manipulation like to_lower_copy, trim, etc.
Constructor Signature and Initialization
cpp
Copy
Edit
namespace ParametricDramDirectoryMSI
{

	TLBHierarchy::TLBHierarchy(String mmu_name, Core *core, MemoryManager *memory_manager, ShmemPerfModel *shmem_perf_model)
	{
		std::cout << "[MMU] Instantiating TLB Hierarchy" << std::endl;
```markdown
Explanation

The constructor logs a message indicating that the TLB hierarchy is being instantiated.
Receives references to the core, memory manager, and shared memory performance model, which are essential for the TLB’s operation within the simulator.

### Reading Basic Configuration

```cpp
        numLevels = Sim()->getCfg()->getInt("perf_model/"+mmu_name+"/tlb_subsystem/number_of_levels");
        prefetch_enabled = Sim()->getCfg()->getBool("perf_model/"+mmu_name+"/tlb_subsystem/prefetch_enabled");
        int add_extra_level = 0;
        if (prefetch_enabled)
            add_extra_level = 1;
```

Explanation

- `numLevels`: The number of TLB levels from the config (for example, L1, L2, L3 TLB).
- `prefetch_enabled`: Whether an extra TLB "queue" (PQ) will be added for prefetching.
- If `prefetch_enabled` is true, we bump the total number of TLB levels by 1.

### Resizing Internal Data Structures

```cpp
        tlbLevels.resize(numLevels + add_extra_level);
        data_path.resize(numLevels + add_extra_level);
        instruction_path.resize(numLevels + add_extra_level);
        tlb_latencies.resize(numLevels + add_extra_level);
```

Explanation

We maintain several parallel vectors to handle:
- `tlbLevels`: The complete set of TLB objects per level.
- `data_path`: The TLBs servicing data accesses.
- `instruction_path`: The TLBs servicing instruction fetches.
- `tlb_latencies`: Latency information per TLB.

### Creating TLB Objects for Each Level

```cpp
        for (int level = 1; level <= numLevels; ++level)
        {
            std::string level_str = std::to_string(level);
            String levelString = String(level_str.begin(), level_str.end());

            int numTLBs = (Sim()->getCfg()->getInt("perf_model/"+mmu_name+"/tlb_level_" + levelString + "/number_of_tlbs"));

            tlbLevels[level - 1].reserve(numTLBs);
            data_path[level - 1].reserve(numTLBs);
            instruction_path[level - 1].reserve(numTLBs);
            tlb_latencies[level - 1].reserve(numTLBs);
```

Explanation

- For each TLB level (e.g., L1, L2, ... Ln), read how many TLBs exist at that level.
- Reserve space in the relevant vectors to store these TLBs.

### Loop Over Each TLB at a Given Level

```cpp
            for (int tlbIndex = 1; tlbIndex <= numTLBs; ++tlbIndex)
            {
                std::string tlbIndex_str = std::to_string(tlbIndex);

                String tlbIndexString = String(tlbIndex_str.begin(), tlbIndex_str.end());
                String tlbconfigstring = "perf_model/"+mmu_name+"/tlb_level_" + levelString + "/tlb" + tlbIndexString;
                String type = Sim()->getCfg()->getString("perf_model/"+mmu_name+"/tlb_level_" + levelString + "/tlb" + tlbIndexString + "/type");
                int size = Sim()->getCfg()->getInt("perf_model/"+mmu_name+"/tlb_level_" + levelString + "/tlb" + tlbIndexString + "/size");
                int assoc = Sim()->getCfg()->getInt("perf_model/"+mmu_name+"/tlb_level_" + levelString + "/tlb" + tlbIndexString + "/assoc");
                page_sizes = Sim()->getCfg()->getInt("perf_model/"+mmu_name+"/tlb_level_" + levelString + "/tlb" + tlbIndexString + "/page_size");
                int *page_size_list = (int *)malloc(sizeof(int) * (page_sizes));
                bool allocate_on_miss = Sim()->getCfg()->getBool("perf_model/"+mmu_name+"/tlb_level_" + levelString + "/tlb" + tlbIndexString + "/allocate_on_miss");
```

Explanation

For each TLB at a given level, read:
- `type`: Data, Instruction, or a unified TLB.
- `size`: Number of entries.
- `assoc`: Associativity.
- `page_sizes`: How many page-size variants are supported by this TLB.
- `page_size_list`: A list of actual page sizes.
- `allocate_on_miss`: Flag controlling whether new translations are inserted on a miss.

### Latency, Page Size List, and Logging

```cpp
                ComponentLatency latency = ComponentLatency(core ? core->getDvfsDomain() : Sim()->getDvfsManager()->getGlobalDomain(DvfsManager::DvfsGlobalDomain::DOMAIN_GLOBAL_DEFAULT),
                                                            Sim()->getCfg()->getInt("perf_model/"+mmu_name+"/tlb_level_" + levelString + "/tlb" + tlbIndexString + "/access_latency"));

                for (int i = 0; i < page_sizes; i++)
                    page_size_list[i] = Sim()->getCfg()->getIntArray("perf_model/"+mmu_name+"/tlb_level_" + levelString + "/tlb" + tlbIndexString + "/page_size_list", i);

                std::string tlbName = "TLB_L" + std::to_string(level) + "_" + std::to_string(tlbIndex);
                String tlbname = String(tlbName.begin(), tlbName.end());
                String full_name = mmu_name+"_"+tlbname;

                std::cout << "TLB: " << tlbName << ", type: " << type << ", size: " << size << ", assoc: " << assoc << ", page sizes: ";

                for (int i = 0; i < page_sizes; i++)
                    std::cout << page_size_list[i] << " ";

                std::cout << std::endl;
```

Explanation

- Compute the TLB’s access latency, either based on the core’s DVFS domain or a global default.
- Retrieve each supported page size from the config file’s array.
- Construct a name like "TLB_L1_1", "TLB_L1_2", etc., for logging.
- Print a summary of the TLB’s configuration to stdout.

### Creating the TLB and Storing in Vectors

```cpp
                TLB *tlb = new TLB(full_name, tlbconfigstring, core ? core->getId() : 0, latency, size, assoc, page_size_list, page_sizes, type, allocate_on_miss);

                tlbLevels[level - 1].push_back(tlb);
                if (type == "Data")
                    data_path[level - 1].push_back(tlb);
                else if (type == "Instruction")
                    instruction_path[level - 1].push_back(tlb);
                else
                {
                    data_path[level - 1].push_back(tlb);
                    instruction_path[level - 1].push_back(tlb);
                }
            }
        }
```

Explanation

- Allocates a new TLB object.
- Adds that TLB to `tlbLevels`.
- Also adds it to either the data path, the instruction path, or both, based on type.

### Handling Prefetch-Enabled Case

```cpp
        if (prefetch_enabled)
        {
            std::cout << "Prefetch Enabled" << std::endl;
            int numpqs = (Sim()->getCfg()->getInt("perf_model/tlb_prefetch/number_of_pqs"));
            tlbLevels[numLevels].reserve(numpqs);
            data_path[numLevels].reserve(numpqs);
            instruction_path[numLevels].reserve(numpqs);
            tlb_latencies[numLevels].reserve(numpqs);
```

Explanation

If TLB prefetching is enabled, an extra "level" (the prefetch queue, or PQ) is created.
The code below will read how many prefetch queues exist (`numpqs`) and configure them.

```cpp
            for (int pqIndex = 1; pqIndex <= numpqs; pqIndex++)
            {
                std::string pqIndex_str = std::to_string(pqIndex);
                String pqIndexString = String(pqIndex_str.begin(), pqIndex_str.end());
                String pqconfigstring = "perf_model/"+mmu_name+"/tlb_prefetch/pq" + pqIndexString;
                String type = Sim()->getCfg()->getString("perf_model/tlb_prefetch/pq" + pqIndexString + "/type");

                int* page_size_list = (int *)malloc(sizeof(int) * 2);
                page_size_list[0] = 12;
                page_size_list[1] = 21;
                
                int size = Sim()->getCfg()->getInt("perf_model/"+mmu_name+"/tlb_prefetch/pq" + pqIndexString + "/size");
                std::string pqName = "PQ_" + std::to_string(pqIndex);
                String pqname = String(pqName.begin(), pqName.end());
                int number_of_prefetchers = Sim()->getCfg()->getInt("perf_model/"+mmu_name+"/tlb_prefetch/pq" + pqIndexString + "/number_of_prefetchers");
                TLBPrefetcherBase **prefetchers = (TLBPrefetcherBase **)malloc(sizeof(TLBPrefetcherBase *) * number_of_prefetchers);
                std::cout << "TLB Prefetchers: " << number_of_prefetchers << std::endl;
```

Explanation

- `pqIndex`: Index into the prefetch queues.
- Hard-coded `page_size_list` with two entries (just for demonstration or specialized usage).
- Each PQ can also have multiple prefetcher objects. We allocate them dynamically.

```cpp
                for (int i = 0; i < number_of_prefetchers; i++)
                {
                    String name = Sim()->getCfg()->getStringArray("perf_model/"+mmu_name+"/tlb_prefetch/pq" + pqIndexString + "/prefetcher_list", i);
                    prefetchers[i] = TLBprefetcherFactory::createTLBPrefetcher(name, pqIndexString, core, memory_manager, shmem_perf_model);
                }

                ComponentLatency latency = ComponentLatency(core->getDvfsDomain(), Sim()->getCfg()->getInt("perf_model/"+mmu_name+"/tlb_prefetch/pq" + pqIndexString + "/access_latency"));

                TLB *pq = new TLB(pqname, pqconfigstring, core->getId(), latency, size, 1, page_size_list, 2, type, false, true, prefetchers, number_of_prefetchers);
```

Explanation

- `TLBprefetcherFactory::createTLBPrefetcher(...)` dynamically instantiates each prefetcher by name.
- Create a new TLB (treated as a PQ) with `prefetch = true`.
- This queue can be inserted into the TLB hierarchy just like another level.

```cpp
                tlbLevels[numLevels].push_back(pq);
                if (type == "Data")
                    data_path[numLevels].push_back(pq);
                else if (type == "Instruction")
                    instruction_path[numLevels].push_back(pq);
                else
                {
                    data_path[numLevels].push_back(pq);
                    instruction_path[numLevels].push_back(pq);
                }
            }
            numLevels++;
        }
```

Explanation

- We place the PQ into `tlbLevels` at the new prefetch "level" index.
- This extends data/instruction path coverage as well.

### Page-Size Predictor Creation

```cpp
        String pagesize_type = Sim()->getCfg()->getString("perf_model/"+mmu_name+"/tlb_subsystem/pagesize_predictor_name");
        page_size_predictor = NULL;
        page_size_predictor = PagesizePredictorFactory::createPagesizePredictor(pagesize_type);
        std::cout << "Tlb levels: " << tlbLevels.size() << std::endl;
    }
```

Explanation

- The constructor reads the type of page-size predictor to instantiate.
- Uses a `PagesizePredictorFactory` to create the appropriate predictor based on the name from config.
```
