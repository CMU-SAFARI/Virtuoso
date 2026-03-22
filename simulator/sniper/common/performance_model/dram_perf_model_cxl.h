#ifndef __DRAM_PERF_MODEL_CXL_H__
#define __DRAM_PERF_MODEL_CXL_H__

/**
 * CXL Memory Performance Model for Sniper
 * 
 * Models CXL.mem (Type 3) memory expanders with configurable latency overhead.
 * Based on silicon-validated parameters from CXL-DMSim (arXiv:2411.02282):
 *   - CXL-ASIC: ~284ns total latency (2.18× local DDR), 82-83% bandwidth
 *   - CXL-FPGA: ~375ns total latency (2.88× local DDR), 45-69% bandwidth
 * 
 * Key latency components modeled:
 *   - bridge_latency: PCIe/CXL bridge traversal (~50ns)
 *   - host_protocol_latency: CXL.mem packet processing at host (~12ns)
 *   - device_protocol_latency: CXL controller processing at device (~15-60ns)
 *   - link_latency: PCIe link propagation
 *   - Backend DRAM access (uses existing DRAM models)
 */

#include "dram_perf_model.h"
#include "queue_model.h"
#include "fixed_types.h"
#include "subsecond_time.h"
#include "dram_cntlr_interface.h"
#include "address_home_lookup.h"
#include "contention_model.h"

#include <vector>

class ShmemPerf;

class DramPerfModelCXL : public DramPerfModel
{
private:
    const core_id_t m_core_id;
    const AddressHomeLookup* m_address_home_lookup;
    const UInt32 m_cache_block_size;

    //=========================================================================
    // CXL-specific timing parameters (from silicon validation)
    //=========================================================================
    
    // Bridge latency: PCIe/CXL bridge traversal
    // Typical: 50ns (CXL-DMSim validated)
    const SubsecondTime m_bridge_latency;
    
    // Host protocol latency: CXL.mem packet processing at host side
    // Typical: 12ns (CXL-DMSim validated)
    const SubsecondTime m_host_protocol_latency;
    
    // Device protocol latency: CXL controller processing at device
    // CXL-ASIC: ~15ns, CXL-FPGA: ~60ns (silicon validated)
    const SubsecondTime m_device_protocol_latency;
    
    // Link latency: PCIe link propagation delay
    // Typical: 5-20ns depending on link distance
    const SubsecondTime m_link_latency;
    
    // Total fixed overhead = bridge + host_proto + device_proto + link
    // This is added on top of backend DRAM access time
    SubsecondTime m_total_cxl_overhead;

    //=========================================================================
    // Backend DRAM parameters (for CXL device's internal memory)
    //=========================================================================
    
    // Backend DRAM access latency (the memory inside the CXL device)
    // CXL-ASIC uses DDR5, CXL-FPGA uses DDR4
    const SubsecondTime m_backend_dram_latency;
    
    // Bank parameters for backend DRAM
    const UInt32 m_num_banks;
    const UInt32 m_num_bank_groups;
    const UInt32 m_num_ranks;
    const UInt32 m_num_channels;
    const UInt32 m_total_banks;
    
    // Row buffer timing
    const SubsecondTime m_bank_keep_open;
    const SubsecondTime m_bank_open_delay;
    const SubsecondTime m_bank_close_delay;
    const SubsecondTime m_dram_access_cost;
    
    // Page size for row buffer
    const UInt32 m_dram_page_size;
    const UInt32 m_dram_page_size_log2;

    //=========================================================================
    // CXL Link bandwidth and queuing
    //=========================================================================
    
    // CXL link bandwidth (PCIe 5.0 x16 = 64 GB/s bidirectional, 32 GB/s per direction)
    const ComponentBandwidth m_cxl_link_bandwidth;
    const float m_cxl_link_bandwidth_gbps;  // Store raw GB/s for statistics
    
    // Queue model for CXL link contention
    QueueModel* m_cxl_link_queue_model;
    
    // Queue depths (from CXL-DMSim: link_req_fifo_depth, link_rsp_fifo_depth)
    const UInt32 m_link_request_fifo_depth;
    const UInt32 m_link_response_fifo_depth;
    
    // Device-side queue depths
    const UInt32 m_device_request_fifo_depth;
    const UInt32 m_device_response_fifo_depth;

    //=========================================================================
    // Backend DRAM bank state tracking
    //=========================================================================
    
    struct BankInfo
    {
        IntPtr open_page;
        SubsecondTime t_avail;
        bool is_page_open;
        
        BankInfo() : open_page(0), t_avail(SubsecondTime::Zero()), is_page_open(false) {}
    };
    
    std::vector<BankInfo> m_banks;

    //=========================================================================
    // Statistics
    //=========================================================================
    
    // Access counts
    UInt64 m_cxl_reads;
    UInt64 m_cxl_writes;
    
    // Row buffer statistics
    UInt64 m_page_hits;
    UInt64 m_page_misses;
    UInt64 m_page_empty;
    
    // Latency tracking
    SubsecondTime m_total_access_latency;
    SubsecondTime m_total_cxl_overhead_latency;
    SubsecondTime m_total_backend_dram_latency;
    SubsecondTime m_total_queueing_delay;
    
    // Bandwidth utilization
    UInt64 m_bytes_transferred;

    //=========================================================================
    // Helper methods
    //=========================================================================
    
    UInt32 getBank(IntPtr address) const;
    UInt32 getBankGroup(IntPtr address) const;
    UInt32 getRank(IntPtr address) const;
    UInt32 getChannel(IntPtr address) const;
    IntPtr getPage(IntPtr address) const;
    
    SubsecondTime getBackendDramLatency(SubsecondTime pkt_time, IntPtr address, 
                                         DramCntlrInterface::access_t access_type);
    SubsecondTime getCXLLinkLatency(SubsecondTime pkt_time, UInt64 pkt_size);

public:
    DramPerfModelCXL(core_id_t core_id, UInt32 cache_block_size, 
                     AddressHomeLookup* address_home_lookup, const String& suffix = "");
    ~DramPerfModelCXL();

    SubsecondTime getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size, 
                                    core_id_t requester, IntPtr address, 
                                    DramCntlrInterface::access_t access_type, 
                                    ShmemPerf *perf, bool is_metadata) override;

    // Statistics accessors
    UInt64 getCXLReads() const { return m_cxl_reads; }
    UInt64 getCXLWrites() const { return m_cxl_writes; }
    UInt64 getPageHits() const { return m_page_hits; }
    UInt64 getPageMisses() const { return m_page_misses; }
    SubsecondTime getTotalCXLOverhead() const { return m_total_cxl_overhead_latency; }
    SubsecondTime getTotalBackendDramLatency() const { return m_total_backend_dram_latency; }
    SubsecondTime getTotalQueueingDelay() const { return m_total_queueing_delay; }
    double getBandwidthUtilization() const;
};

#endif /* __DRAM_PERF_MODEL_CXL_H__ */
