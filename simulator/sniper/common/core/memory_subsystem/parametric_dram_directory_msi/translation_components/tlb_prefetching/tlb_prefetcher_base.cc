
#include "tlb_prefetcher_base.h"
#include "mmu_base.h"
#include "../memory_manager.h"
#include <tuple>
#include <cstdio>
#include <cstdlib>


#include "simulator.h"
#include "config.hpp"
#include "hooks_manager.h"
#include "stats.h"
#include <iostream>

namespace ParametricDramDirectoryMSI
{
	TLBPrefetcherBase::TLBPrefetcherBase(Core *_core, MemoryManagerBase *_memory_manager, ShmemPerfModel *_shmem_perf_model, String name)
		: m_common{},
		  m_addr_tag(0),
		  m_log_interval(20000),
		  m_summary_printed(false),
		  core(_core),
		  memory_manager(_memory_manager),
		  shmem_perf_model(_shmem_perf_model),
		  m_name(name)
	{
		log_file = std::ofstream();
		log_file_name = "tlb_prefetcher_core_" + std::to_string(core->getId()) + "_" + m_name.c_str() + ".log";
		log_file_name = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + log_file_name;
		log_file.open(log_file_name.c_str());

		if (Sim()->getCfg()->hasKey("perf_model/tlb_prefetcher_log_interval"))
			m_log_interval = static_cast<UInt64>(Sim()->getCfg()->getInt("perf_model/tlb_prefetcher_log_interval"));

		String cat = String("tlbpf_") + m_name;
		registerStatsMetric(cat, core->getId(), "invocations",         &m_common.invocations);
		registerStatsMetric(cat, core->getId(), "invocations_miss",    &m_common.invocations_miss);
		registerStatsMetric(cat, core->getId(), "invocations_pq_hit",  &m_common.invocations_pq_hit);
		registerStatsMetric(cat, core->getId(), "candidates_returned", &m_common.candidates_returned);
		registerStatsMetric(cat, core->getId(), "candidates_valid",    &m_common.candidates_valid);
		registerStatsMetric(cat, core->getId(), "candidates_self",     &m_common.candidates_self);
		registerStatsMetric(cat, core->getId(), "walks_noncanonical",  &m_common.candidates_noncanonical);

		// Print the end-of-run summary.  The hook runs while the prefetcher is
		// still alive; TLB teardown order is not something to rely on.
		Sim()->getHooksManager()->registerHook(HookType::HOOK_SIM_END, hookSimEnd, (UInt64)this);
	}

	std::string TLBPrefetcherBase::logPrefix() const
	{
		std::ostringstream os;
		os << "[TLBPF " << m_name.c_str() << " c" << core->getId() << "] ";
		return os.str();
	}

	void TLBPrefetcherBase::printStatusLine(const char *tag)
	{
		std::ostringstream os;
		const UInt64 v = m_common.candidates_valid;
		os << logPrefix() << tag
		   << " inv=" << m_common.invocations
		   << " miss=" << m_common.invocations_miss
		   << " pq_hit=" << m_common.invocations_pq_hit
		   << " cand=" << m_common.candidates_returned
		   << " valid=" << v
		   << " self=" << m_common.candidates_self;
		if (m_common.candidates_noncanonical)
			os << " noncanonical_refused=" << m_common.candidates_noncanonical;
		if (v)
			os << " (" << (100 * m_common.candidates_self / v) << "% self)";
		os << " |";
		appendSummary(os);
		std::cout << os.str() << std::endl;
	}

	SInt64 TLBPrefetcherBase::hookSimEnd(UInt64 self, UInt64 /*time*/)
	{
		TLBPrefetcherBase *p = reinterpret_cast<TLBPrefetcherBase *>(self);
		if (!p->m_summary_printed)
		{
			p->printStatusLine("summary");
			p->m_summary_printed = true;
		}
		return 0;
	}

	std::vector<query_entry> TLBPrefetcherBase::invoke(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count, PageTable *pt, bool instruction, bool tlb_hit, bool pq_hit, int page_size)
	{
		m_common.invocations++;
		if (!tlb_hit) m_common.invocations_miss++;
		if (pq_hit)   m_common.invocations_pq_hit++;
		// The prefetcher sees the untagged VA; its candidates get the tag back
		// below (see m_addr_tag).
		m_addr_tag = static_cast<uint64_t>(address) >> 48;
		address = stripTag(address);

		std::vector<query_entry> out = performPrefetch(address, eip, lock, modeled, count, pt, instruction, tlb_hit, pq_hit, page_size);

		m_common.candidates_returned += out.size();
		for (const auto &q : out)
		{
			if (q.ppn == 0)
				continue;
			m_common.candidates_valid++;
			// Compare at the candidate's own page granularity (a 2MB candidate
			// covers the accessed 4KB page too).
			const int ps = (q.page_size > 0) ? q.page_size : 12;
			if ((static_cast<uint64_t>(q.address) >> ps) == (static_cast<uint64_t>(address) >> ps))
				m_common.candidates_self++;
		}
		for (auto &q : out)
			q.address = withTag(q.address);

		if (m_log_interval && (m_common.invocations % m_log_interval) == 0)
			printStatusLine("progress");

		return out;
	}

	query_entry TLBPrefetcherBase::PTWTransparent(IntPtr address, IntPtr eip, Core::lock_signal_t lock, bool modeled, bool count, PageTable *pt)
	{
		query_entry q{};

		if (pt == NULL)
		{
			q.ppn = 0;
			return q;
		}

		// `address` is an untagged VA from the prefetcher (see m_addr_tag).
		// Refuse addresses the page table cannot represent (a VPN that went
		// negative and wrapped, or past 2^48).  The 4-level radix walk
		// decodes the low 48 bits, so such a target would alias to an
		// unrelated page and, with restart_walk, allocate it.  Returned as a
		// failed walk (ppn 0).  The bound is the full 48 bits, NOT the x86
		// 47-bit user limit: traces use the upper half of the 48-bit space
		// (95.7% of compute_int_149's addresses are >= 2^47).
		if ((static_cast<uint64_t>(address) >> 48) != 0)
		{
			m_common.candidates_noncanonical++;
			q.ppn = 0;
			return q;
		}

		// Preserve the caller's notion of time so that prefetching does not
		// advance the main thread.
		const SubsecondTime start_time = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

#ifdef DEBUG_TLB_PREFETCHER
		log_file << "[" << m_name << "] Performing PTW-transparent prefetch for address: " << address << " at time: " << start_time.getNS() << " ns" << std::endl;
#endif

		auto *mmu_base = static_cast<ParametricDramDirectoryMSI::MemoryManagementUnitBase *>(memory_manager->getMMU());
		auto ptw_result = mmu_base->performPTW(withTag(address), modeled, count, /*is_prefetch*/ true, eip, lock, pt, /*restart_walk*/ true);

#ifdef DEBUG_TLB_PREFETCHER
		log_file << "[" << m_name << "] PTW result: "
				 << " latency: " << ptw_result.latency.getNS() << " ns"
				 << " page_fault: " << ptw_result.page_fault
				 << " ppn: " << ptw_result.ppn
				 << " page_size: " << ptw_result.page_size
				 << " at time: " << shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD).getNS() << " ns" << std::endl;
#endif

		const SubsecondTime walk_latency = ptw_result.latency;
		const IntPtr ppn_result = ptw_result.ppn;
		const int page_size = ptw_result.page_size;
		const uint64_t payload_bits = ptw_result.payload_bits;

		// Restore the caller time after modeling the prefetch walk
		shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, start_time);

		q.timestamp = start_time + walk_latency;
		q.address = address;
		q.ppn = ppn_result;
		q.page_size = page_size;
		q.payload_bits = payload_bits;

		if (getenv("RECENCY_DEBUG")) {
			static long ptwt_n = 0;
			if (ptwt_n++ < 400)
				fprintf(stderr, "[PTWT] pf=%s addr=0x%lx walk_lat_ns=%lu ppn=0x%lx pgfault=%d start_ns=%lu avail_ns=%lu\n",
					m_name.c_str(), (unsigned long)address, (unsigned long)walk_latency.getNS(),
					(unsigned long)ppn_result, (int)ptw_result.page_fault,
					(unsigned long)start_time.getNS(), (unsigned long)q.timestamp.getNS());
		}
		return q;
	}

}
