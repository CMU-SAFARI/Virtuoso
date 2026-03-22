#ifndef CACHE_H
#define CACHE_H

#include "cache_base.h"
#include "cache_set.h"
#include "cache_block_info.h"
#include "utils.h"
#include "hash_map_set.h"
#include "cache_perf_model.h"
#include "shmem_perf_model.h"
#include "log.h"
#include "core.h"
#include "fault_injection.h"
#include "stats.h"
#include <memory>
#include <vector>

// Define to enable the set usage histogram
// #define ENABLE_SET_USAGE_HIST

// Snapshot data for a single cache block
struct CacheBlockSnapshot {
	bool valid;
	UInt8 block_type;  // CacheBlockInfo::block_type_t
	UInt8 recency;     // LRU position (0 = MRU, higher = older)
	int reuse_count;   // How many times this block was reused
};

// Full cache snapshot
struct CacheSnapshot {
	UInt32 num_sets;
	UInt32 num_ways;
	std::vector<std::vector<CacheBlockSnapshot>> blocks;  // [set][way]
};

class Cache : public CacheBase
{
private:
	bool m_enabled;

	// Cache counters
	UInt64 m_num_accesses;
	UInt64 m_num_hits;

	// Generic Cache Info
	cache_t m_cache_type;
	CacheSet **m_sets;
	CacheSet **m_fake_sets;
	CacheSetInfo *m_set_info;

	FaultInjector *m_fault_injector;

	//unique ptr since there might be a bug that alters the page size list
	std::unique_ptr<int[]> m_pagesizes; //@kanellok TLB supported page size vector
	int m_number_of_page_sizes;
	bool m_is_tlb;

	int reuse_levels[3];

	float average_data_reuse;
	float average_metadata_reuse;
	float average_tlb_reuse;

	UInt64 sum_data_reuse;
	UInt64 sum_data_utilization;

	UInt64 sum_metadata_reuse;
	UInt64 sum_metadata_utilization;


	UInt64 number_of_data_reuse;
	UInt64 number_of_metadata_reuse;

	int metadata_passthrough_loc;

	/* Reuse prediction implementation */
	/* 0, 1-2, 3-5, 5-10, >10 */

	UInt64 data_reuse[5];
	UInt64 metadata_reuse[5];

	UInt64 data_util[8];
	UInt64 metadata_util[8];

#ifdef ENABLE_SET_USAGE_HIST
	UInt64 *m_set_usage_hist;
#endif

	// L2 content logging (similar to NUCA cache)
	core_id_t m_core_id;
	std::ofstream m_content_log;
	UInt64 m_last_log_access_count;
	bool m_content_log_initialized;
	UInt64 m_total_accesses;  // Total accesses for logging trigger

public:
	std::vector<uint64_t> m_page_walk_cacheblocks;	/* timeseries stats */

	// constructors/destructors
	Cache(String name,
		  String cfgname,
		  core_id_t core_id,
		  UInt32 num_sets,
		  UInt32 associativity, UInt32 cache_block_size,
		  String replacement_policy,
		  cache_t cache_type,
		  hash_t hash = CacheBase::HASH_MASK,
		  FaultInjector *fault_injector = NULL,
		  AddressHomeLookup *ahl = NULL, bool is_tlb = false, int *page_size = NULL, int number_of_page_sizes = 0);
	~Cache();

	Lock &getSetLock(IntPtr addr);

	bool invalidateSingleLine(IntPtr addr);
	bool invalidateSingleLineTLB(IntPtr addr, int page_size);
	bool containsTLB(IntPtr addr, int page_size) const;
	CacheBlockInfo *accessSingleLine(IntPtr addr,
									 access_t access_type, Byte *buff, UInt32 bytes, SubsecondTime now, bool update_replacement, bool tlb_entry = false, bool is_metadata = false);
	CacheBlockInfo *accessSingleLineTLB(IntPtr addr,
										access_t access_type, Byte *buff, UInt32 bytes, SubsecondTime now, bool update_replacement);

	void insertSingleLine(IntPtr addr, Byte *fill_buff,
						  bool *eviction, IntPtr *evict_addr,
						  CacheBlockInfo *evict_block_info, Byte *evict_buff, SubsecondTime now, CacheCntlr *cntlr = NULL, CacheBlockInfo::block_type_t btype = CacheBlockInfo::block_type_t::DATA);
	void insertSingleLineTLB(IntPtr addr, Byte *fill_buff,
							 bool *eviction, IntPtr *evict_addr,
							 CacheBlockInfo *evict_block_info, Byte *evict_buff, SubsecondTime now, CacheCntlr *cntlr = NULL, CacheBlockInfo::block_type_t btype = CacheBlockInfo::block_type_t::DATA, int page_size = 12, IntPtr ppn = 0);
	CacheBlockInfo *peekSingleLine(IntPtr addr);
	CacheBlockInfo *peekBlock(UInt32 set_index, UInt32 way) const { return m_sets[set_index]->peekBlock(way); }
	void updateSetReplacement(IntPtr addr);

	std::vector<uint64_t> getPTWTranslationStats() { return m_page_walk_cacheblocks; }

	// Update Cache Counters
	void updateCounters(bool cache_hit);
	void updateHits(Core::mem_op_t mem_op_type, UInt64 hits);

	void enable() { m_enabled = true; }
	void disable() { m_enabled = false; }

	CacheSet *getCacheSet(UInt32 set_index);

	void measureStats();
	void markMetadata(IntPtr address, CacheBlockInfo::block_type_t blocktype);
	
	// Get a snapshot of the entire cache state for visualization
	CacheSnapshot getCacheSnapshot() const;
	
	// Save a cache snapshot as a heatmap image (PPM format)
	void saveSnapshotHeatmap(const std::string& filename) const;
	
	// Log cache content distribution (metadata vs data) - similar to NUCA cache
	void logCacheContentDistribution(UInt64 access_count);
};

template <class T>
UInt32 moduloHashFn(T key, UInt32 hash_fn_param, UInt32 num_buckets)
{
	return (key >> hash_fn_param) % num_buckets;
}

#endif /* CACHE_H */
