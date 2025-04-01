#include <math.h>
#include "ip_stride_prefetcher.h"
#include "simulator.h"
#include "config.hpp"
#include "stats.h"

const IntPtr PAGE_SIZE = 4096;
const IntPtr CACHE_BLOCK_SIZE = 64;
const IntPtr LOG2_PAGE_SIZE = log2(PAGE_SIZE);
const IntPtr LOG2_CACHE_BLOCK_SIZE = log2(CACHE_BLOCK_SIZE);
const IntPtr CACHE_BLOCK_MASK = (CACHE_BLOCK_SIZE-1);
const IntPtr NUM_CACHE_BLOCKS_IN_PAGE = (PAGE_SIZE/CACHE_BLOCK_SIZE);

IPStridePrefetcher::IPStridePrefetcher(String configName, core_id_t _core_id)
	: m_core_id(_core_id)
	, m_allocate_on_miss(Sim()->getCfg()->getBoolDefault("perf_model/" + configName + "/prefetcher/ip_stride/allocate_on_miss", false))
	, m_num_prefetches(Sim()->getCfg()->getInt("perf_model/" + configName + "/prefetcher/ip_stride/num_prefetches"))
	, m_num_streams(Sim()->getCfg()->getInt("perf_model/" + configName + "/prefetcher/ip_stride/num_streams"))
	, m_stop_at_page(Sim()->getCfg()->getBoolDefault("perf_model/" + configName + "/prefetcher/ip_stride/stop_at_page_boundary", true))
	, m_lookahead(Sim()->getCfg()->getInt("perf_model/" + configName + "/prefetcher/ip_stride/lookahead"))
{
	m_rpt.resize(m_num_streams, NULL);
	for(UInt32 index = 0; index < m_rpt.size(); ++index)
	{
		m_rpt[index] = new RPTEntry();
	}

	bzero(&stats, sizeof(stats));
	registerStatsMetric("ip_stride", m_core_id, "pref_called", &stats.pref_called);
	registerStatsMetric("ip_stride", m_core_id, "rpt_hit", &stats.rpt_hit);
	registerStatsMetric("ip_stride", m_core_id, "non_zero_curr_stride", &stats.non_zero_curr_stride);
	registerStatsMetric("ip_stride", m_core_id, "curr_stride_correct", &stats.curr_stride_correct);
	registerStatsMetric("ip_stride", m_core_id, "steady", &stats.steady);
	registerStatsMetric("ip_stride", m_core_id, "rpt_replacement", &stats.rpt_replacement);
	registerStatsMetric("ip_stride", m_core_id, "gen_prefetch", &stats.gen_prefetch);
}


IPStridePrefetcher::~IPStridePrefetcher()
{

}


std::vector<IntPtr>
IPStridePrefetcher::getNextAddress(IntPtr current_address, core_id_t _core_id, Core::mem_op_t mem_op_type, bool cache_hit, bool prefetch_hit, IntPtr eip)
{
	IntPtr current_page = current_address >> LOG2_PAGE_SIZE;
	IntPtr current_offset = (current_address >> LOG2_CACHE_BLOCK_SIZE) & CACHE_BLOCK_MASK;
	// std::cout << "eip: " << std::hex << eip << std::dec
	// 			<< " current_address: " << std::hex << current_address << std::dec
	// 			<< " current_page: " << std::hex << current_page << std::dec
	// 			<< " current_offset: " << current_offset << std::endl;
	std::vector<IntPtr> pref_addr;

	if(mem_op_type == Core::WRITE)
	{
		return pref_addr;
	}

	stats.pref_called++;
	int32_t index = find(eip);
	if(index != -1)
	{
		stats.rpt_hit++;
		RPTEntry *rpt_entry = m_rpt[index];
		int32_t curr_stride = (current_offset - rpt_entry->prev_offset);

		if(curr_stride)
		{
			// std::cout << "match" << std::endl;
			stats.non_zero_curr_stride++;
			if(curr_stride == rpt_entry->stride)
			{
				stats.curr_stride_correct++;
				if(rpt_entry->state == INIT || rpt_entry->state == TRANSIENT || rpt_entry->state == STEADY)
				{
					rpt_entry->state = STEADY;
				}
				else if(rpt_entry->state == NO_PRED)
				{
					rpt_entry->state = TRANSIENT;
				}
			}
			else
			{
				if(rpt_entry->state == INIT)
				{
					rpt_entry->state = TRANSIENT;
				}
				else if(rpt_entry->state == STEADY)
				{
					rpt_entry->state = INIT;
				}
				else if(rpt_entry->state == TRANSIENT || rpt_entry->state == NO_PRED)
				{
					rpt_entry->state = NO_PRED;
				}
			}

			rpt_entry->prev_offset = current_offset;
			rpt_entry->stride = curr_stride;
			update_age(index);

			/* prefetch generation */
			if(rpt_entry->state == STEADY)
			{
				stats.steady++;
				pref_addr = generatePrefetchAddress(rpt_entry, current_page, current_offset);
			}
		}
	}
	else
	{
		if(!m_allocate_on_miss || !cache_hit)
		{
			stats.rpt_replacement++;
			index = find_replacement();
			RPTEntry *rpt_entry = m_rpt[index];
			rpt_entry->valid = true;
			rpt_entry->eip = eip;
			rpt_entry->prev_offset = current_offset;
			rpt_entry->stride = 0;
			rpt_entry->state = INIT;
			update_age(index);
		}
	}

	return pref_addr;
}

int32_t IPStridePrefetcher::find(IntPtr eip)
{
	/* full IP search */
	for(uint32_t index = 0; index < m_rpt.size(); ++index)
	{
		if(m_rpt[index]->valid && m_rpt[index]->eip == eip)
		{
			return index;
		}
	}

	return -1;
}

void IPStridePrefetcher::update_age(int32_t current)
{
	for(uint32_t index = 0; index < m_rpt.size(); ++index)
	{
		if((int32_t)index == current)
		{
			m_rpt[index]->lru = 0;
		}
		else
		{
			m_rpt[index]->lru++;
		}
	}
}

int32_t IPStridePrefetcher::find_replacement()
{
	uint32_t max_lru = 0;
	int32_t replacement_index = -1;
	for(uint32_t index = 0; index < m_rpt.size(); ++index)
	{
		if(!m_rpt[index]->valid)
		{
			return (int32_t)index;
		}
		else if(m_rpt[index]->lru >= max_lru)
		{
			max_lru = m_rpt[index]->lru;
			replacement_index = index;
		}
	}

	return replacement_index;
}

std::vector<IntPtr> IPStridePrefetcher::generatePrefetchAddress(RPTEntry *rpt_entry, IntPtr current_page, IntPtr current_offset)
{
	// std::cout << "curr_addr: " << std::hex << ((current_page << LOG2_PAGE_SIZE) + (current_offset << LOG2_CACHE_BLOCK_SIZE)) << std::dec 
	// 			<< " stride: " << rpt_entry->stride
	// 			<< " pref_addr:";
	std::vector<IntPtr> addresses;
	for(unsigned int index = 1; index <= m_num_prefetches; ++index)
	{
		int32_t prefetch_offset = current_offset + (m_lookahead + index ) * rpt_entry->stride;
		// But stay within the page if requested
		if (!m_stop_at_page || (prefetch_offset >= 0 && prefetch_offset < (int32_t)NUM_CACHE_BLOCKS_IN_PAGE))
		{
			IntPtr prefetch_address = (current_page << LOG2_PAGE_SIZE) + ((IntPtr)prefetch_offset << LOG2_CACHE_BLOCK_SIZE);
			// std::cout << " " << std::hex << prefetch_address << std::dec << ",";
			addresses.push_back(prefetch_address);
			stats.gen_prefetch++;
		}
	}
	// std::cout << std::endl;
	return addresses;
}