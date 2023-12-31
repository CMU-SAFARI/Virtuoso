#ifndef TLB_H
#define TLB_H

#include "fixed_types.h"
#include "cache.h"
#include <util.h>
#include <unordered_map>
#include "pagetable.h"
#include "lock.h"
#include "hash_map_set.h"
#include <vector>
#include <queue>

namespace ParametricDramDirectoryMSI
{
	class TLBPrefetcherBase;
	enum TLBtype
	{
		Instruction,
		Data,
		Unified
	};
	typedef struct queue_entry_s
	{
		SubsecondTime timestamp;
		int page_size;
		IntPtr ppn;
		IntPtr address;
	} query_entry;
	class Compare
	{
	public:
		bool operator()(query_entry a, query_entry b)
		{
			return a.timestamp > b.timestamp;
		}
	};
	class TLB
	{
	private:
		UInt32 m_size;
		UInt32 m_associativity;
		UInt32 m_num_entries;
		Cache m_cache;
		String m_type;
		String m_name;
		core_id_t m_core_id;
		TLBPrefetcherBase **prefetchers;
		int number_of_prefetchers;
		int *m_page_size_list;
		int m_page_sizes;
		bool m_allocate_miss;
		bool m_prefetch;
		int max_prefetch_count;
		ComponentLatency m_access_latency;
		std::priority_queue<query_entry, std::vector<query_entry>, Compare> entry_priority_queue;

		struct TLBStats
		{
			UInt64 m_access, m_hit, m_miss, m_eviction;
		} tlb_stats;

	public:
		TLB(String name, String cfgname, core_id_t core_id, ComponentLatency access_latency, UInt32 num_entries, UInt32 associativity, int *page_size_list, int page_sizes, String tlb_type, bool allocate_on_miss, bool prefetch = false, TLBPrefetcherBase **tpb = NULL, int number_of_prefetchers = 0, int max_prefetch_count = 1000);
		CacheBlockInfo *lookup(IntPtr address, SubsecondTime now, bool model_count, Core::lock_signal_t lock, IntPtr eip, bool modeled, bool count, PageTable *pt);
		std::tuple<bool, IntPtr, int> allocate(IntPtr address, SubsecondTime now, bool count, Core::lock_signal_t lock, int page_size, IntPtr ppn, bool self_alloc = false);
		TLBtype getType() { return (m_type == "Instruction") ? Instruction : (m_type == "Data") ? Data
																								: Unified; };
		String getName() { return m_name; };
		bool getAllocateOnMiss() { return m_allocate_miss; };
		bool getPrefetch() { return m_prefetch; };
		SubsecondTime getLatency() { return m_access_latency.getLatency(); };
		bool supportsPageSize(int page_size)
		{
			for (int i = 0; i < m_page_sizes; i++)
			{
				if (m_page_size_list[i] == page_size)
					return true;
			}
			return false;
		}
	};

}

#endif // TLB_H
