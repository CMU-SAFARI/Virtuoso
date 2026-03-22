# Virtuoso: An Open-Source, Comprehensive and Modular Simulation Framework for Virtual Memory Research


This is an alpha version of the simulator. Maintainance and updates will keep coming. Stay tuned !

  
## Software requirements


We prepared container images which are uploaded publicly in Docker hub
under the tags:  

``` cpp
#Contains all the simulator dependencies 
1. kanell21/artifact_evaluation:victima                   
```

Using container images, you can easily use the simulator without installing multiple new packages and handling cyclic dependencies. 

## Getting Started

Virtuoso is built on top of Sniper but can be plugged into multiple simulators.  
We refer the users to Sniper's [website](https://snipersim.org/w/Getting_Started) and manual for more information about the underlying simulator. 


## Code Structure

The code of is organized as follows:

``` cpp
virtuoso/common/core/memory_subsystem/parametric_dram_directory_msi/
├── memory_manager.h/cc // Memory manager that handles the memory requests - coreInitiateMemoryAccess() is the key function
├── mmu.h/cc //Generic and Baseline MMU class that implements TLB hierarchy and Page Table
├── mmu_factory.h/cc //Factory for the MMU 
├── tlb_subsystem.h/cc //Instantiation of the whole TLB hierarchy 
├── tlb.h/cc //Implementation of the generic TLB controller
├── pagesize_predictor_factory.h/cc //Factory for the pagesize predictor
├── superpage_predictor_factory.h/cc //Superpage predictor following [Papadopoulou et al. HPCA 2015]
``` 

``` cpp
virtuoso/common/system/
├── virtuos.h/cc //OS emulator that instantiates the physical memory allocator 
├── allocator_factory.h/cc //Factory for the physical memory allocator
├── physical_memory_allocator.h/cc //Abstract class for the physical memory allocator
├── buddy_allocator.h/cc //Buddy allocator for the physical memory
|── simple_thp_allocator.h/cc //Simple Transparent Huge Page allocator for the physical memory
``` 

## Generic MMU walkthrough 

The Memory Management Unit (MMU) is designed to simulate the functionality of an MMU within a processor. This component handles the translation of virtual addresses to physical addresses using a hierarchical TLB subsystem and a page table. 

### Instantiating the MMU
The MMU gets instantiated in the constructor of parametric_dram_directory_msi/memory_manager.cc. The MMU is instantiated by calling the instantiateMMU() function. The MMU is composed of a page table, a TLB subsystem, and a metadata table.
The TLB subsystem is responsible for caching direct virtual to physical address translations. 
The page table walker is responsible for translating virtual addresses to physical addresses.


This function instantiates the page table and the TLB subsystem. The page table and the TLB subsystem are instantiated by calling the instantiatePageTable() and instantiateTLBSubsystem() functions, respectively. 

``` cpp
   void MemoryManagementUnit::instantiateMMU()
   {
      instantiatePageTable();
      instantiateTLBSubsystem();
   }
```

### Instantiating the Page Table
The page table is instantiated by calling the instantiatePageTable() function. The page table is instantiated by calling the createPageTable() function of the PageTableFactory class. The createPageTable() function returns a pointer to the page table. The createPageTable() function takes two arguments: the page table type and the page table name. The page table type and the page table name are read from the configuration file. The page table type and the page table name are read from the configuration file by calling the getCfg()->getString() function of the Sim() object. The getCfg()->getString() function takes a string argument that specifies the configuration file path. The getCfg()->getString() function returns a string that contains the value of the configuration file entry. The page table type and the page table name are passed as arguments to the createPageTable() function. The createPageTable() function returns a pointer to the page table. The pointer to the page table is assigned to the page_table member variable of the MMU class. 

``` cpp
   void MemoryManagementUnit::instantiatePageTable()
   {
      String page_table_type = Sim()->getCfg()->getString("perf_model/mmu/page_table_type");
      String page_table_name = Sim()->getCfg()->getString("perf_model/mmu/page_table_name");
      page_table = PageTableFactory::createPageTable(page_table_type, page_table_name, core);
   }
```

### Instantiating the TLB Subsystem
The TLB subsystem is instantiated by calling the instantiateTLBSubsystem() function. The TLB subsystem is instantiated by calling the TLBHierarchy constructor. The TLBHierarchy constructor takes four arguments: the name of the TLB subsystem, a pointer to the core, a pointer to the memory manager, and a pointer to the shared memory performance model (this is essential for keeping track of the elapsed cycles).

``` cpp
	void MemoryManagementUnit::instantiateTLBSubsystem()
	{
		tlb_subsystem = new TLBHierarchy("tlb_subsystem", core, memory_manager, shmem_perf_model);
	}
```

The file parametric_dram_directory_msi/tlb_subsystem.cc contains the implementation of the TLBHierarchy class. The TLBHierarchy class is responsible for instantiating the TLB controllers and the TLB hierarchy. The TLBHierarchy class is composed of a vector of TLB controllers. 
* The number of levels specifies the number of levels in the TLB hierarchy - in each level there can be multiple TLBs for different page sizes/data types{data, instruction, unified}.
* The data path specifies the data path of the TLB hierarchy - For example: [[L1D 4KB,L1D 2MB], [L2 Unified]]
* The instruction path specifies the instruction path of the TLB hierarchy - For example: [[L1I 4KB], [L2 Unified]]
* The TLB latencies specify the latencies of the TLB controllers - For example: [[L1D-4KB:1, L1D-2MB:2, L1-I-4KB:5], [L2:10]]

``` cpp
	TLBHierarchy::TLBHierarchy(String name, Core *core, MemoryManager *memory_manager, ShmemPerfModel *shmem_perf_model)
	{

		int numLevels = Sim()->getCfg()->getInt("perf_model/tlb_subsystem/number_of_levels");
		prefetch_enabled = Sim()->getCfg()->getBool("perf_model/tlb_subsystem/prefetch_enabled");

		tlbLevels.resize(numLevels + prefetch_enabled);
		data_path.resize(numLevels + prefetch_enabled);
		instruction_path.resize(numLevels + prefetch_enabled);
		tlb_latencies.resize(numLevels + prefetch_enabled);
      ...
```

For every level of the TLB hierarchy, we instatiate all the TLBs and assign their properties based on the configuration file. For example, 
we read the size and associativity based on the configuration file and instantiate the TLB.
``` cpp
		for (int level = 1; level <= numLevels; ++level)
		{

			std::string level_str = std::to_string(level);
			String levelString = String(level_str.begin(), level_str.end());

			int numTLBs = (Sim()->getCfg()->getInt("perf_model/tlb_level_" + levelString + "/number_of_tlbs"));

			tlbLevels[level - 1].reserve(numTLBs);
			data_path[level - 1].reserve(numTLBs);
			instruction_path[level - 1].reserve(numTLBs);
			tlb_latencies[level - 1].reserve(numTLBs);

			for (int tlbIndex = 1; tlbIndex <= numTLBs; ++tlbIndex)
			{

				std::string tlbIndex_str = std::to_string(tlbIndex);
            int size = Sim()->getCfg()->getInt("perf_model/tlb_level_" + levelString + "/tlb" + tlbIndexString + "/size");
				int assoc = Sim()->getCfg()->getInt("perf_model/tlb_level_" + levelString + "/tlb" + tlbIndexString + "/assoc");
            ..
            TLB *tlb = new TLB(tlbname, tlbconfigstring, core->getId(), latency, size, assoc, page_size_list, page_sizes, type, allocate_on_miss);

			}
		}

If TLB prefetching is enabled, we instantiate a hierarchy of TLB prefetch buffers 
```cpp
		if (prefetch_enabled)
		{
			...
		}
```

We instantiate a page size predictor which can be used to predict the page size and avoid serially probing the TLBs that store entries with N different page sizes.
The page size predictor uses the PC (eip) as a feature to make the prediction.
``` cpp 
		page_size_predictor = PagesizePredictorFactory::createPagesizePredictor(pagesize_type);

	int TLBHierarchy::predictPagesize(IntPtr eip)
	{
		if (page_size_predictor == NULL)
		{
			if (page_sizes == 0)
			{
				return 0;
			}
			return page_size_list[0];
		}
		return page_size_predictor->predictPageSize(eip);
	}
	void TLBHierarchy::predictionResult(IntPtr eip, bool success)
	{
		if (page_size_predictor == NULL)
		{
			return;
		}
		return page_size_predictor->predictionResult(eip, success);
	}
```
### Performing Address Translation 

The function performAddressTranslation() is responsible for performing the address translation. The performAddressTranslation() function takes five arguments: the eip, the address, a boolean that specifies whether the address is an instruction, a lock signal, and a boolean that specifies whether the translation is modeled. The performAddressTranslation() function returns a pair that contains the latency of the address translation and the physical address. The performAddressTranslation() function first checks if the address is in the TLB.  We iterate through all the TLBs and check if the address is in the TLB. 

``` cpp


```cpp 

pair<SubsecondTime, IntPtr> MemoryManagementUnit::performAddressTranslation(IntPtr eip, IntPtr address, bool instruction, Core::lock_signal_t lock, bool modeled, bool count)
	{

		// std::cout << "Metadata table walk latency: " << metadata_table_walk_latency << std::endl;

		// metadata run end
		bool hit = false;
		TLB *hit_tlb = NULL;
		CacheBlockInfo *tlb_block_info = NULL;
		int hit_level = -1;

		int page_size;
		// TLB Access for all TLBs
		for (int i = 0; i < tlbs.size(); i++)
		{
			for (int j = 0; j < tlbs[i].size(); j++)
			{
				bool tlb_stores_instructions = (tlbs[i][j]->getType() == TLBtype::Instruction) || (tlbs[i][j]->getType() == TLBtype::Unified);

				if (tlb_stores_instructions && instruction)
				{
					tlb_block_info = tlbs[i][j]->lookup(address, time, count, lock, eip, modeled, count, page_table);
					if (tlb_block_info != NULL)
					{
						hit_tlb = tlbs[i][j];
						hit_level = i;
						hit = true;
						goto HIT;
					}
				}
				else if (!instruction)
				{
					bool tlb_stores_data = !(tlbs[i][j]->getType() == TLBtype::Instruction);
					if (tlb_stores_data)
					{
						tlb_block_info = tlbs[i][j]->lookup(address, time, count, lock, eip, modeled, count, page_table);
						if (tlb_block_info != NULL)
						{
							hit_tlb = tlbs[i][j];
							hit_level = i;
							hit = true;
							goto HIT;
						}
					}
				}
			}
		}
```
 
If the address is in the TLB, we charge the according TLB hit latency. 

```cpp


#ifdef DEBUG_MMU
		std::cout << "TLB Hit ? " << hit << " at level: " << hit_level << std::endl;
#endif
		SubsecondTime charged_tlb_latency = SubsecondTime::Zero();

		if (hit)
		{
			if (instruction)
				tlbs = tlb_subsystem->getInstructionPath();
			else
				tlbs = tlb_subsystem->getDataPath();

			SubsecondTime tlb_latency[hit_level + 1];

			for (int i = 0; i < hit_level; i++)
			{
				for (int j = 0; j < tlbs[i].size(); j++)
				{
					tlb_latency[i] = max(tlbs[i][j]->getLatency(), tlb_latency[i]);
				}
#ifdef DEBUG_MMU
				std::cout << "Charging TLB Latency: " << tlb_latency[i] << " at level: " << i << std::endl;
#endif
				translation_stats.total_tlb_latency += tlb_latency[i];
				translation_stats.tlb_latency_per_level[i] += tlb_latency[i];
				charged_tlb_latency += tlb_latency[i];
			}

			for (int j = 0; j < tlbs[hit_level].size(); j++)
			{
				if (tlbs[hit_level][j] == hit_tlb)
				{
					translation_stats.total_tlb_latency += hit_tlb->getLatency();
					charged_tlb_latency += hit_tlb->getLatency();
					translation_stats.tlb_latency_per_level[hit_level] += hit_tlb->getLatency();

#ifdef DEBUG_MMU
					std::cout << "Charging TLB Hit Latency: " << hit_tlb->getLatency() << " at level: " << hit_level << std::endl;
#endif
				}
			}
		}
```

If the translation is not found in the TLB hierarchy, we need to charge the TLB miss latency. For example, 
for a data access that misses in all levels, we charge (max(L1D-4KB, L1D-2MB)+L2) cycles latency.

```cpp
		SubsecondTime tlb_latency[tlbs.size()];
		if (!hit)
		{
			#ifdef DEBUG_MMU
				std::cout << "We have a TLB miss for address: " << std::hex << address << std::endl;
			#endif
			for (int i = 0; i < tlbs.size(); i++)
			{
				for (int j = 0; j < tlbs[i].size(); j++)
				{
					tlb_latency[i] = max(tlbs[i][j]->getLatency(), tlb_latency[i]);
				}
#ifdef DEBUG_MMU
				std::cout << "Charging TLB Latency: " << tlb_latency[i] << " at level: " << i << std::endl;
#endif
				translation_stats.total_tlb_latency += tlb_latency[i];
				charged_tlb_latency += tlb_latency[i];
			}
		}

		SubsecondTime total_walk_latency = SubsecondTime::Zero();
		IntPtr ppn_result;
```
In case of a TLB miss, we need to perform a page table walk. The page table walk will provide the ppn_result which is the physical address, the page size and a vector which contains the physical addresses visited during the page table walk (and the potential page fault).
```cpp
		if (!hit)
		{

			// std::cout << "Page walk starts here --------------------------------\n";
			PTWResult page_table_walk_result = page_table->initializeWalk(address, count);
			ppn_result = get<2>(page_table_walk_result);
			page_size = get<0>(page_table_walk_result);
			accessedAddresses accesses = get<1>(page_table_walk_result);
```
For every physical address accessed by the PTW, we need to send a packet to the cache hierarchy to read the appropriate cache lines. 
The elements of the accessedAddresses vector incorporate information about the dataflow of the memory accesses (for example, in radix we access the root, then the second level, then the third level, etc) while
in a hash table we access separately/in parallel each bucket that operates with different page sizes (e.g., [Skarlatos et al. ASPLOS 2020]).

   
```cpp
			translationPacket packet;
			packet.eip = eip;
			packet.instruction = instruction;
			packet.lock_signal = lock;
			packet.modeled = modeled;
			packet.count = count;
			packet.type = CacheBlockInfo::block_type_t::PAGE_TABLE;

			SubsecondTime latency = SubsecondTime::Zero();

			int levels = 0;
			int tables = 0;

			for (int i = 0; i < accesses.size(); i++)
			{
				int level = get<1>(accesses[i]);
				int table = get<0>(accesses[i]);
				if (level > levels)
					levels = level;
				if (table > tables)
					tables = table;
			}

			SubsecondTime latency_per_table_per_level[tables + 1][levels + 1] = {SubsecondTime::Zero()};

			bool is_page_fault = true;
			int correct_table = -1;

			for (int i = 0; i < accesses.size(); i++)
			{
#ifdef DEBUG_MMU
				std::cout << "[PTW Accesses] Accessing physical address: " << get<2>(accesses[i]) << " for table " << get<0>(accesses[i]) << " for level: " << get<1>(accesses[i]) << std::endl;
#endif

				packet.address = get<2>(accesses[i]);

				latency = accessCache(packet);

				if (latency_per_table_per_level[get<0>(accesses[i])][get<1>(accesses[i])] < latency)
					latency_per_table_per_level[get<0>(accesses[i])][get<1>(accesses[i])] = latency;

				if (get<3>(accesses[i]) == true)
				{
					is_page_fault = false;
					correct_table = get<0>(accesses[i]);
					break;
				}
			}

			SubsecondTime table_latency = SubsecondTime::Zero();

			if (!is_page_fault)
			{

				for (int j = 0; j < levels + 1; j++)
				{
					total_walk_latency += latency_per_table_per_level[correct_table][j];
				}
			}
			else
			{
#ifdef DEBUG_MMU
				std::cout << "Page Fault " << is_page_fault << std::endl;
#endif
				if (count)
					translation_stats.page_faults++;

				table_latency = SubsecondTime::Zero();
				for (int i = 0; i < tables + 1; i++)
				{
					for (int j = 0; j < levels + 1; j++)
					{
						table_latency += latency_per_table_per_level[i][j];
					}
				}
				total_walk_latency = table_latency;
			}
		}
```
If the address is found in the TLB we retrieve the page size and the PPN from the TLB. 

```cpp
   HIT:
      if (hit)
      {
         page_size = tlb_block_info->getPageSize();
         translation_stats.tlb_hit_page_sizes[page_size]++;
         ppn_result = tlb_block_info->getPPN();
      }
```cpp
		else
		{
			page_size = tlb_block_info->getPageSize();
			translation_stats.tlb_hit_page_sizes[page_size]++;
			ppn_result = tlb_block_info->getPPN();
		}
```
We need to update the TLB hierarchy by inserting he corresponding TLB entries if needed. The "allocate_on_miss" flag is used to determine if we need to allocate the entry in the TLB. 
There are two cases that we need to allocate the entry in the TLB: (i) we allocate on a miss or (ii) we need to allocate the entry in the TLB because we have evicted translations from the previous level. 
We need to be careful and allocate the entry ONLY when the TLB supports the page size of the address.

```cpp
		// TLB Allocations
		if (instruction)
			tlbs = tlb_subsystem->getInstructionPath();
		else
			tlbs = tlb_subsystem->getDataPath();
		std::map<int, vector<tuple<IntPtr, int>>> evicted_translations;

		// We need to allocate the the entry in every "allocate on miss" TLB

		for (int i = 0; i < tlbs.size(); i++)
		{
			// We will check where we need to allocate the page

			for (int j = 0; j < tlbs[i].size(); j++)
			{
				if ((i > 0) && (evicted_translations[i - 1].size() != 0))
				{
					tuple<bool, IntPtr, int> result;

#ifdef DEBUG_MMU
					std::cout << "There are evicted translations from level: " << i - 1 << std::endl;
#endif

					for (int k = 0; k < evicted_translations[i - 1].size(); k++)
					{
#ifdef DEBUG_MMU
						std::cout << "Evicted Translation: " << std::hex << get<0>(evicted_translations[i - 1][k]) << std::endl;
#endif
						if (tlbs[i][j]->supportsPageSize(page_size))
						{
#ifdef DEBUG_MMU
							std::cout << "Allocating evicted entry in TLB: Level = " << i << " Index =  " << j << std::endl;
#endif

							result = tlbs[i][j]->allocate(get<0>(evicted_translations[i - 1][k]), time, count, lock, get<1>(evicted_translations[i - 1][k]), ppn_result);
							if (get<0>(result) == true)
							{
								evicted_translations[i].push_back(make_tuple(get<1>(result), get<2>(result)));
							}
						}
					}
				}

				if (tlbs[i][j]->supportsPageSize(page_size) && tlbs[i][j]->getAllocateOnMiss() && (!hit || (hit && hit_level > i)))
				{
#ifdef DEBUG_MMU
					std::cout << "Allocating in TLB: Level = " << i << " Index = " << j << std::endl;
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
Finally, we calculate the physical address and return the total latency of the address translation. 
The latency of the address translation is the sum of the charged TLB latency and the total walk latency. 
In the upcoming version of Virtuoso, we will add support for TLB Miss Status Handle Registers to restrict the amount of parallel walks. 
For now, all PTWs occur in parallel which provides a conservative estimate of the overheads of address translation latency. 

Important note: ppn_result is always provided at the finest possible granularity: even if the page size is 2MB, the ppn result is always the physical address of the 4KB page inside the 2MB large page. 

```cpp
		// std::cout << "Finished address translation for virtual address: " << std::hex << address << std::endl;
		translation_stats.total_translation_latency += charged_tlb_latency + total_walk_latency;
		int page_size_in_bytes = pow(2, page_size);
		IntPtr physical_address = ppn_result * page_size_in_bytes + address % page_size_in_bytes;
		// std::cout << "Physical address: " << std::hex << physical_address << " with page size: " << page_size_in_bytes << std::endl;
		return std::make_pair(charged_tlb_latency + std::max(total_walk_latency, metadata_table_walk_latency), physical_address);
	}
```


## Running a simulation 

To run a simulation, you mainly need to specify the configuration file and the binary that you want to simulate.

You can find the corresponding configuration file in the "configs" directory.
   
   ``` bash
   xxxxxxxx@xxxxxx:~/virtuoso$ ls configs/
   ```

A good example to start with is:
``` cpp
virtuoso/config/virtuoso_configs/virtuoso_baseline.cfg

``` 

We set several different parameters related to the TLB subsystem, the page table, the physical memory allocator, the memory manager, and the OS emulator.
For example, this piece of code sets the number of levels of the TLB hierarchy, the prefetching policy, the page size predictor, and the superpage predictor. 

``` cpp
[perf_model/tlb_subsystem]
number_of_levels = 2
prefetch_enabled = false 
pagesize_predictor_name = "superpage"

[perf_model/superpage]
small_page_size = 12
large_page_size = 21
table_size = 2


[perf_model/tlb_level_1]
number_of_tlbs = 3


[perf_model/tlb_level_1/tlb1]
type = "Data"
size = 64
assoc = 4
page_size = 1
page_size_list = 12
allocate_on_miss = "true"
access_latency = 1

[perf_model/tlb_level_1/tlb2]
type = "Data"
size = 64
assoc = 4
page_size = 1
page_size_list = 21
allocate_on_miss = "true"
access_latency = 1

[perf_model/tlb_level_1/tlb3]
type = "Instruction"
size = 64
assoc = 4
page_size = 1
page_size_list = 12
allocate_on_miss = "true"
access_latency = 1

[perf_model/tlb_level_2]
number_of_tlbs = 1

[perf_model/tlb_level_2/tlb1]
type = "Unified"
size = 16
assoc = 8
page_size=2
page_size_list = 12,21
allocate_on_miss = "false"
access_latency = 12
```


and with the following piece of code you can set the parameters for the MMU. 
Here we chose to use a radix page table with 4 levels, without any metadata table and translation is enabled. 
The generic MMU is used as the default MMU. 

``` cpp

[perf_model/mmu]
page_table_type="radix"
page_table_name="radix_4level"
type="default"
metadata_table_name="none"
translation_enabled="true"
```


With the following piece of code you can set the parameters for the OS emulator.
We chose the allocator that provides both large and small pages, we set the memory size to 16GB, and we set the target fragmentation to 1.0 and the maximum order to 12. 
Target fragmentation of 1.0 means that we want to avoid any fragmentation. The metric we used to define the fragmentation is the number of free large pages in the system compared to the total number of large pages.

``` cpp
[perf_model/virtuos]
memory_allocator = "simple_thp"
enabled = "false"
memory_mapped_not_pipes = "true"


[perf_model/pmem_alloc]
memory_size = 16384
target_fragmentation = 1.0
target_memory = 0.0
fragmentation_file = ""
max_order = 12
```

Now we are set to run a simulation: we need to choose our application. We have two ways to run an application:
1. We can use traces
2. We can use compiled binaries

You can download traces from the following link: 

``` cpp
wget https://storage.googleapis.com/traces_virtual_memory/traces_victima
tar -xzf traces_victima
```

We suggest using traces as they are more flexible.
``` cpp
./run-sniper --genstats -d output_dir -c ./config/virtuoso_configs/virtuoso_baseline.cfg -s stop-by-icount:2000000 --traces=traces/rnd.sift
```

* --genstats: Generate statistics in file sim.stats
* -d output_dir: Output directory where all the simulation results will be stored
* -c ./config/virtuoso_configs/virtuoso_baseline.cfg: Configuration file
* -s stop-by-icount:2000000: Stop the simulation after 2000000 instructions, every 100K instructions you will receive a heartbeat message
* --traces=traces/rnd.sift: The trace file that you want to simulate


Under ./scripts/ you can find a script that automates the process of running a simulation and putting the results in a CSV file.
You need to customize the scripts to fit your purpose. 

``` cpp
./scripts/launch_jobs.py
./scripts/create_csv_exp.py or ./scripts/create_csv_exp.py
```

## Contact 

Konstantinos Kanellopoulos <konkanello@gmail.com>  
Konstantinos Sgouras <sgouraskon@gmail.com>







   
