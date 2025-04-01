#include <math.h>
#include "stream_prefetcher.h"
#include "simulator.h"
#include "config.hpp"
#include "stats.h"

const IntPtr PAGE_SIZE = 4096;
const IntPtr CACHE_BLOCK_SIZE = 64;
const IntPtr LOG2_PAGE_SIZE = log2(PAGE_SIZE);
const IntPtr LOG2_CACHE_BLOCK_SIZE = log2(CACHE_BLOCK_SIZE);
const IntPtr CACHE_BLOCK_MASK = (CACHE_BLOCK_SIZE-1);
const IntPtr NUM_CACHE_BLOCKS_IN_PAGE = (PAGE_SIZE/CACHE_BLOCK_SIZE);

Streamer::Streamer(String configName, core_id_t _core_id)
	: m_core_id(_core_id)
	, m_allocate_on_miss(Sim()->getCfg()->getBoolDefault("perf_model/" + configName + "/prefetcher/streamer/allocate_on_miss", false))
	, m_num_prefetches(Sim()->getCfg()->getInt("perf_model/" + configName + "/prefetcher/streamer/num_prefetches"))
	, m_num_streams(Sim()->getCfg()->getInt("perf_model/" + configName + "/prefetcher/streamer/num_streams"))
	, m_prefetch_front(Sim()->getCfg()->getInt("perf_model/" + configName + "/prefetcher/streamer/prefetch_front"))
	, m_max_conf(Sim()->getCfg()->getInt("perf_model/" + configName + "/prefetcher/streamer/max_conf"))
	, m_conf_thresh(Sim()->getCfg()->getInt("perf_model/" + configName + "/prefetcher/streamer/conf_thresh"))
{
	m_stream_table.resize(m_num_streams, NULL);
	for(uint32_t index = 0; index < m_num_streams; ++index)
	{
		m_stream_table[index] = new StreamEntry();
	}

	bzero(&stats, sizeof(stats));
	registerStatsMetric("streamer", m_core_id, "pref_called", &stats.pref_called);
	registerStatsMetric("streamer", m_core_id, "table_hit", &stats.table_hit);
	registerStatsMetric("streamer", m_core_id, "table_replacement", &stats.table_replacement);
	registerStatsMetric("streamer", m_core_id, "incr_conf", &stats.incr_conf);
	registerStatsMetric("streamer", m_core_id, "decr_conf", &stats.decr_conf);
	registerStatsMetric("streamer", m_core_id, "steady", &stats.steady);
	registerStatsMetric("streamer", m_core_id, "gen_prefetch", &stats.gen_prefetch);
}

Streamer::~Streamer()
{

}

std::vector<IntPtr>
Streamer::getNextAddress(IntPtr current_address, core_id_t _core_id, Core::mem_op_t mem_op_type, bool cache_hit, bool prefetch_hit, IntPtr eip)
{
	IntPtr current_page = current_address >> LOG2_PAGE_SIZE;
	IntPtr current_offset = (current_address >> LOG2_CACHE_BLOCK_SIZE) & CACHE_BLOCK_MASK;
	std::vector<IntPtr> pref_addr;

	if(mem_op_type == Core::WRITE)
	{
		return pref_addr;
	}

	stats.pref_called++;
	int index = find(current_page);
	if(index != -1)
	{
		stats.table_hit++;
		StreamEntry *stream_entry = m_stream_table[index];
		if(current_offset != stream_entry->last_offset)
		{
			int8_t curr_dir = current_offset > stream_entry->last_offset ? 1 : -1;
			if(curr_dir == stream_entry->dir)
			{
				stats.incr_conf++;
				incr_conf(stream_entry);
			}
			else
			{
				stats.decr_conf++;
				decr_conf(stream_entry);
			}
			stream_entry->last_offset = current_offset;
			stream_entry->dir = curr_dir;
			update_age(index);
			if(stream_entry->conf >= m_conf_thresh)
			{
				stats.steady++;
				pref_addr = generatePrefetchAddress(stream_entry, current_page, current_offset);
			}
		}
	}
	else
	{
		if(!m_allocate_on_miss || !cache_hit)
		{
			stats.table_replacement++;
			index = find_replacement();
			StreamEntry* stream_entry = m_stream_table[index];
			stream_entry->valid = true;
			stream_entry->page = current_page;
			stream_entry->last_offset = current_offset;
			stream_entry->dir = 0;
			stream_entry->conf = false;
			update_age(index);
		}
	}
	return pref_addr;
}

int32_t Streamer::find(IntPtr page)
{
	/* full page_id search */
	for(uint32_t index = 0; index < m_stream_table.size(); ++index)
	{
		if(m_stream_table[index]->valid && m_stream_table[index]->page == page)
		{
			return index;
		}
	}
	return -1;
}

void Streamer::update_age(int32_t current)
{
	for(uint32_t index = 0; index < m_stream_table.size(); ++index)
	{
		if((int32_t)index == current)
		{
			m_stream_table[index]->lru = 0;
		}
		else
		{
			m_stream_table[index]->lru++;
		}
	}
}

int32_t Streamer::find_replacement()
{
	uint32_t max_lru = 0;
	int32_t replacement_index = -1;
	for(uint32_t index = 0; index < m_stream_table.size(); ++index)
	{
		if(!m_stream_table[index]->valid)
		{
			return (int32_t)index;
		}
		else if(m_stream_table[index]->lru >= max_lru)
		{
			max_lru = m_stream_table[index]->lru;
			replacement_index = index;
		}
	}
	return replacement_index;
}

std::vector<IntPtr> Streamer::generatePrefetchAddress(StreamEntry *stream_entry, IntPtr current_page, IntPtr current_offset)
{
	std::vector<IntPtr> addresses;
	for(unsigned int index = 1; index <= m_num_prefetches; ++index)
	{
		int32_t prefetch_offset = current_offset + (m_prefetch_front + index ) * stream_entry->dir;
		if (prefetch_offset >= 0 && prefetch_offset < (int32_t)NUM_CACHE_BLOCKS_IN_PAGE)
		{
			IntPtr prefetch_address = (current_page << LOG2_PAGE_SIZE) + ((IntPtr)prefetch_offset << LOG2_CACHE_BLOCK_SIZE);
			addresses.push_back(prefetch_address);
			stats.gen_prefetch++;
		}
	}
	return addresses;
}
