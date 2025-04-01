#ifndef STREAM_PREFETCHER
#define STREAM_PREFETCHER

#include "prefetcher.h"

class StreamEntry
{
public:
	bool valid;	
	IntPtr page;
	IntPtr last_offset;
	int8_t dir;
	int8_t conf;
	uint32_t lru;

public:
	StreamEntry() : page(0xdeadbeef), last_offset(0), dir(0), conf(0), lru(0) {}
	~StreamEntry(){}
};


class Streamer : public Prefetcher
{
private:
	core_id_t m_core_id;
	bool m_allocate_on_miss;
	uint32_t m_num_prefetches;
	uint32_t m_num_streams;
	uint32_t m_prefetch_front;
	uint8_t m_max_conf, m_conf_thresh;
	std::vector<StreamEntry*> m_stream_table;

	struct
	{
		UInt64 pref_called;
		UInt64 table_hit;
		UInt64 table_replacement;
		UInt64 incr_conf;
		UInt64 decr_conf;
		UInt64 steady;
		UInt64 gen_prefetch;
	} stats;

public:
	Streamer(String configName, core_id_t _core_id);
	~Streamer();
	int32_t find(IntPtr page);
	void update_age(int32_t current);
	int32_t find_replacement();
	std::vector<IntPtr> generatePrefetchAddress(StreamEntry *stream_entry, IntPtr current_page, IntPtr current_offset);
	std::vector<IntPtr> getNextAddress(IntPtr current_address, core_id_t core_id, Core::mem_op_t mem_op_type, bool cache_hit, bool prefetch_hit, IntPtr eip);
	inline void incr_conf(StreamEntry *entry) {if(entry->conf < m_max_conf) entry->conf++;}
	inline void decr_conf(StreamEntry *entry) {if(entry->conf) entry->conf--;}
};

#endif /* STREAM_PREFETCHER */

