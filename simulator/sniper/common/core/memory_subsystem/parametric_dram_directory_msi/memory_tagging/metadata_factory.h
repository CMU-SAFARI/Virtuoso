#include "metadata_table_base.h"
#include "xmem_metadata_table.h"
#include "mmu_base.h"
#include "memory_manager.h"

namespace ParametricDramDirectoryMSI
{
	class MetadataFactory
	{
	public:

		static MetadataTableBase *createMetadataTable(String name, Core *core, ShmemPerfModel *shmem_perf_model, MemoryManagementUnitBase *mmu, MemoryManagerBase *memory_manager)
		{
			if (name == "none")
			{
				return NULL;
			}
			assert(0);
			return NULL;	
		}
	};
}