#include "metadata_table_base.h"
#include "xmem_metadata_table.h"
#include "mmu_base.h"
#include "memory_manager.h"

namespace ParametricDramDirectoryMSI
{
	class MetadataFactory
	{
	public:
		static MetadataTableBase *createXmemMetadataTable(Core *_core, ShmemPerfModel *_m_shmem_perf_mode, MemoryManagementUnitBase *_mmu, MemoryManager *_memory_manager)
		{
			int size = Sim()->getCfg()->getInt("perf_model/xmem/size");
			int granularity = Sim()->getCfg()->getInt("perf_model/xmem/granularity");
			int cache_size = Sim()->getCfg()->getInt("perf_model/xmem/cache_size");
			int cache_associativity = Sim()->getCfg()->getInt("perf_model/xmem/cache_associativity");
			int cache_hit_latency = Sim()->getCfg()->getInt("perf_model/xmem/cache_hit_latency");
			int cache_miss_latency = Sim()->getCfg()->getInt("perf_model/xmem/cache_miss_latency");
			return new XmemMetadataTable(size, granularity, cache_size, cache_associativity, cache_hit_latency, cache_miss_latency, _core, _m_shmem_perf_mode, _mmu, _memory_manager);
		}
		static MetadataTableBase *createMetadataTable(String name, Core *core, ShmemPerfModel *shmem_perf_model, MemoryManagementUnitBase *mmu, MemoryManager *memory_manager)
		{
			if (name == "none")
			{
				return NULL;
			}
			else if (name == "xmem")
			{
				return createXmemMetadataTable(core, shmem_perf_model, mmu, memory_manager);
			}
			else
			{
				std::cout << "[CONFIG ERROR] Unknown metadata table type: " << name << std::endl;
				return NULL;
			}
		}
	};
}