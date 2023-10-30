#include "metadata_table_base.h"
#include "memory_manager.h"
#include "nuca_cache.h"
#include "utopia.h"
#include "mmu_base.h"
#include "memory_manager.h"
using namespace std;
namespace ParametricDramDirectoryMSI
{

	class XmemMetadataTable : public MetadataTableBase
	{

	protected:
		int size;
		int granularity;
		IntPtr atom_table;
		UtopiaCache *buffer;
		CacheCntlr *cache;
		ShmemPerfModel *m_shmem_perf_model;
		CacheCntlr *cache_ctrl;
		NucaCache *nuca;
		Core *core;
		MemoryManager *memory_manager;
		MemoryManagementUnitBase *mmu;

	public:
		XmemMetadataTable(int size, int granularity, int cache_size, int cache_associativity, int cache_hit_latency, int cache_miss_latency, Core *_core, ShmemPerfModel *_m_shmem_perf_mode, MemoryManagementUnitBase *_mmu, MemoryManager *_memory_manager);
		virtual MetadataWalkResult initializeWalk(IntPtr address);
	};
}