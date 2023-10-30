#include "xmem_metadata_table.h"
#include "virtuos.h"

namespace ParametricDramDirectoryMSI
{

	XmemMetadataTable::XmemMetadataTable(int _size, int _granularity, int _cache_size, int _cache_associativity, int _cache_hit_latency, int cache_miss_latency, Core *_core, ShmemPerfModel *_m_shmem_perf_model, MemoryManagementUnitBase *_mmu, MemoryManager *_memory_manager) : MetadataTableBase(), size(_size), granularity(_granularity), m_shmem_perf_model(_m_shmem_perf_model), mmu(_mmu), memory_manager(_memory_manager), core(_core)
	{
		if (_core != NULL)
		{
			atom_table = Sim()->getVirtuOS()->getMemoryAllocator()->allocate(1024 * ((size * 1024 * 1024) / _granularity));
			buffer = new UtopiaCache("xmem_cache", "xmem_cache", _core->getId(), (int)log2(granularity), _cache_size, _cache_associativity, ComponentLatency(_core->getDvfsDomain(), _cache_hit_latency), ComponentLatency(_core->getDvfsDomain(), cache_miss_latency));
		}
	}
	MetadataWalkResult XmemMetadataTable::initializeWalk(IntPtr address)
	{
		vector<IntPtr> addresses;
		SubsecondTime s1 = m_shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
		UtopiaCache::where_t result = buffer->lookup(address, s1, true, false);
		if (result == UtopiaCache::HIT)
		{

			SubsecondTime final_charged_latency = buffer->access_latency.getLatency();

			return MetadataWalkResult(final_charged_latency, addresses, CacheBlockInfo::block_type_t::EXPRESSIVE);
		}
		else
		{

			int index = address >> ((int)log2(granularity));
			IntPtr cache_address = atom_table * 4096 + index;
			addresses.push_back(cache_address);

			SubsecondTime final_charged_latency = buffer->miss_latency.getLatency();

			return MetadataWalkResult(final_charged_latency, addresses, CacheBlockInfo::block_type_t::EXPRESSIVE);
		}
	}
}