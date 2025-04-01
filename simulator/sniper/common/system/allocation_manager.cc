// #include <sched.h>
// #include <linux/unistd.h>
// #include <sys/types.h>
// #include <sys/syscall.h>
// #include <unistd.h>
// #include <limits.h>
// #include <algorithm>
// #include <vector>
// #include <unordered_map>

// #include "allocation_manager.h"
// #include "simulator.h"
// #include "buddy_allocator.h"
// #include "log.h"
// #include "config.h"
// #include "virtuos.h"

// AllocationManager::AllocationManager(bool _eager, int _large_page_percentage) : large_page_percentage(_large_page_percentage),
// 																				eager(_eager)
// {
// 	srand(5436);

// 	int core_num;
// 	core_num = Config::getSingleton()->getTotalCores();

// 	for (int i = 0; i < core_num; i++)
// 	{

// 		std::unordered_map<uint64_t, uint64_t> core_virtual_map;
// 		std::unordered_map<uint64_t, uint64_t> pagemap;
// 		std::vector<Range> rangetable;
// 		std::unordered_map<IntPtr, uint64_t> vpncore;

// 		range_table.push_back(rangetable);
// 		allocation_map_per_core.push_back(core_virtual_map);
// 		virtual_to_physical_map.push_back(pagemap);
// 		vpn_map_per_core.push_back(vpncore);
// 	}
// }

// AllocationManager::~AllocationManager()
// {
// }

// int AllocationManager::getNumberOfRanges(int core_id)
// {

// 	return range_table[core_id].size();
// }

// void AllocationManager::printVirtualMap()
// {
// 	int core_num;
// 	core_num = Config::getSingleton()->getTotalCores();
// 	for (int i = 0; i < core_num; i++)
// 	{
// 		for (std::pair<uint64_t, uint64_t> element : allocation_map_per_core[i])
// 		{
// 			std::cout << "Pointer: " << element.first << " Size : " << element.second << std::endl;
// 		}
// 	}
// }

// void AllocationManager::pageFaultHandler(IntPtr vpn, uint64_t core_id)
// {
// 	if (!exists_in_range_table(vpn, core_id))
// 	{
// 		uint64_t ppn;
// 		ppn = Sim()->getVirtuOS()->getMemoryAllocator()->allocate(4 * 1024);
// 		// std::cout << "Allocated ppn: " << ppn << " for vpn: " << vpn << " core_id: " << core_id << std::endl;
// 		virtual_to_physical_map[core_id].insert(std::make_pair(vpn, ppn));
// 	}
// }

// int AllocationManager::defaultPageFaultHandler()
// {
// 	if ((rand() % 100) < large_page_percentage)
// 	{

// 		return 21;
// 	}

// 	else
// 	{

// 		return 12;
// 	}
// }

// void AllocationManager::printPageMap()
// {

// 	int core_num;
// 	core_num = Config::getSingleton()->getTotalCores();
// 	for (int i = 0; i < core_num; i++)
// 	{
// 		for (std::pair<uint64_t, uint64_t> element : virtual_to_physical_map[i])
// 		{
// 			std::cout << "VPN: " << element.first << " PPN: " << element.second << std::endl;
// 		}
// 	}
// }

// void AllocationManager::printRangeTable(std::vector<Range> range_table)
// {

// 	for (auto range : range_table)
// 	{
// 		std::cout << "Data structure range: PPN " << range.vpn << " Bounds: " << range.bounds << std::endl;
// 	}
// }

// bool AllocationManager::exists_in_range_table(IntPtr vpn, int core_id)
// {
// 	LOG_PRINT("[ALLOC]: VPN: %lx \n", vpn);
// 	for (auto range : range_table[core_id])
// 	{
// 		LOG_PRINT("[ALLOC]: Check range table %lx , %lx \n", range.vpn, range.bounds);

// 		// std::cout << "[ALLOC]: Check range table" << "VPN: " << range.vpn << "Bounds: " << range.bounds <<  std::endl;
// 		if (vpn >= range.vpn && vpn <= range.bounds)
// 			return true;
// 	}
// 	return false;
// }

// Range AllocationManager::access_range_table(IntPtr vpn, int core_id)
// {
// 	for (auto range : range_table[core_id])
// 	{
// 		if (vpn >= range.vpn && vpn <= range.bounds)
// 			return range;
// 	}
// }

// void AllocationManager::handleMalloc(uint64_t pointer, uint64_t size, int core_id)
// {
// 	allocation_map_per_core[core_id][pointer] = size;
// 	IntPtr current_vpn = pointer;

// 	current_vpn = (current_vpn >> 12); // 4KB page

// 	if ((size < (1 >> 12)) && (vpn_map_per_core[core_id].find(current_vpn) == vpn_map_per_core[core_id].end()))
// 	{
// 		uint64_t ppn;
// 		ppn = Sim()->getVirtuOS()->getMemoryAllocator()->allocate(4 * 1024);
// 		vpn_map_per_core[core_id][current_vpn] = ppn;
// 	}

// 	std::cout << "[ALLOC MANAGER] handle malloc - VPN:	" << std::hex << current_vpn << " size: " << size << std::endl;

// 	if (eager)
// 	{

// 		auto result = Sim()->getVirtuOS()->getMemoryAllocator()->allocate_eager_paging(size, core_id);
// 		for (auto range : result)
// 		{
// 			Range vpn_range;
// 			vpn_range.vpn = current_vpn;
// 			vpn_range.bounds = current_vpn + range.bounds;
// 			current_vpn = vpn_range.bounds;
// 			range_table[core_id].push_back(vpn_range);
// 			std::cout << "Data structure range: PPN " << range.vpn << " Bounds: " << range.bounds << std::endl;
// 		}
// 		// printRangeTable(ranges);
// 	}
// }

// void AllocationManager::handleCalloc(uint64_t pointer, uint64_t size, int core_id)
// {
// 	std::cout << "[ALLOC MANAGER] handle calloc - pointer:	" << pointer << " size: " << size << std::endl;
// 	allocation_map_per_core[core_id][pointer] = size;

// 	IntPtr current_vpn = pointer;
// 	current_vpn = (current_vpn >> 12);

// 	if ((size < (1 >> 12)) && (vpn_map_per_core[core_id].find(current_vpn) == vpn_map_per_core[core_id].end()))
// 	{
// 		uint64_t ppn;
// 		ppn = Sim()->getVirtuOS()->getMemoryAllocator()->allocate(4 * 1024);
// 		vpn_map_per_core[core_id][current_vpn] = ppn;
// 	}

// 	if (eager)
// 	{
// 		auto ppn_ranges = Sim()->getVirtuOS()->getMemoryAllocator()->allocate_eager_paging(size, core_id);

// 		for (auto range : ppn_ranges)
// 		{
// 			Range vpn_range;
// 			vpn_range.vpn = current_vpn;
// 			vpn_range.bounds = current_vpn + vpn_range.bounds;
// 			range_table[core_id].push_back(vpn_range);
// 			// std::cout << "Data structure range: PPN " << range.vpn << " Bounds: " << range.bounds << std::endl;
// 		}
// 		// printRangeTable(ranges);
// 	}
// }

// void AllocationManager::handleRealloc(uint64_t init_pointer, uint64_t pointer, uint64_t size, int core_id)
// {

// 	allocation_map_per_core[core_id].erase(init_pointer);
// 	allocation_map_per_core[core_id][pointer] = size;

// 	IntPtr current_vpn = pointer;
// 	current_vpn = (current_vpn >> 12);

// 	if ((size < (1 >> 12)) && (vpn_map_per_core[core_id].find(current_vpn) == vpn_map_per_core[core_id].end()))
// 	{
// 		uint64_t ppn;
// 		ppn = Sim()->getVirtuOS()->getMemoryAllocator()->allocate(4 * 1024);
// 		vpn_map_per_core[core_id][current_vpn] = ppn;
// 	}

// 	if (eager)
// 	{
// 		auto ppn_ranges = Sim()->getVirtuOS()->getMemoryAllocator()->allocate_eager_paging(size, core_id);

// 		for (auto range : ppn_ranges)
// 		{
// 			Range vpn_range;
// 			vpn_range.vpn = current_vpn;
// 			vpn_range.bounds = current_vpn + vpn_range.bounds;
// 			range_table[core_id].push_back(vpn_range);
// 			// std::cout << "Data structure range: PPN " << range.vpn << " Bounds: " << range.bounds << std::endl;
// 		}
// 		// printRangeTable(ranges);
// 	}
// }

// void AllocationManager::handleFree(uint64_t pointer, int core_id)
// {

// 	if (allocation_map_per_core[core_id].find(pointer) != allocation_map_per_core[core_id].end())
// 		allocation_map_per_core[core_id].erase(pointer);
// }
