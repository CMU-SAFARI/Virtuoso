#ifndef IP_STRIDE_PREFETCHER
#define IP_STRIDE_PREFETCHER

#include "prefetcher.h"

/* Implements IP-stride prefetcher proposed by Baer and Chen in 1991
 * DOI: 10.1145/125826.125932
 */

enum rpt_state_t
{
	INVALID = 0,
	INIT,
	TRANSIENT,
	STEADY,
	NO_PRED,
	NUM_RPT_STATES = NO_PRED
};

class RPTEntry
{
public:
	bool valid;
	IntPtr eip;
	IntPtr prev_offset;
	int32_t stride;
	rpt_state_t state;
	uint32_t lru;

	RPTEntry() : valid(false), eip(0xdeadbeef), prev_offset(0), stride(0), state(INVALID), lru(0) {}
	~RPTEntry(){}
};

class IPStridePrefetcher : public Prefetcher
{
private:
	core_id_t m_core_id;
	bool m_allocate_on_miss;
	uint32_t m_num_prefetches;
	uint32_t m_num_streams;
	uint32_t m_stop_at_page;
	uint32_t m_lookahead;
	// bool m_buffer_prefetch;
	std::vector<RPTEntry*> m_rpt;

	struct
	{
		UInt64 pref_called;
		UInt64 rpt_hit;
		UInt64 non_zero_curr_stride;
		UInt64 curr_stride_correct;
		UInt64 steady;
		UInt64 rpt_replacement;
		UInt64 gen_prefetch;
	} stats;

public:
	IPStridePrefetcher(String configName, core_id_t _core_id);
	~IPStridePrefetcher();
	int32_t find(IntPtr eip);
	void update_age(int32_t current);
	int32_t find_replacement();
	std::vector<IntPtr> generatePrefetchAddress(RPTEntry *rpt_entry, IntPtr current_page, IntPtr current_offset);
	std::vector<IntPtr> getNextAddress(IntPtr current_address, core_id_t core_id, Core::mem_op_t mem_op_type, bool cache_hit, bool prefetch_hit, IntPtr eip);
};

#endif /* IP_STRIDE_PREFETCHER */

