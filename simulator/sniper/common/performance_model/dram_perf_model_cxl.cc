/**
 * CXL Memory Performance Model Implementation
 * 
 * Models CXL.mem (Type 3) memory expanders based on silicon-validated
 * parameters from CXL-DMSim (arXiv:2411.02282).
 */

#include "dram_perf_model_cxl.h"
#include "simulator.h"
#include "config.hpp"
#include "config.h"
#include "stats.h"
#include "shmem_perf.h"
#include "utils.h"

#include <cmath>

// Helper to safely get config with default
static SInt64 getCfgIntSafe(const String& key, SInt64 default_val)
{
    if (Sim()->getCfg()->hasKey(key))
        return Sim()->getCfg()->getInt(key);
    return default_val;
}

static float getCfgFloatSafe(const String& key, float default_val)
{
    if (Sim()->getCfg()->hasKey(key))
        return Sim()->getCfg()->getFloat(key);
    return default_val;
}

static String getCfgStringSafe(const String& key, const String& default_val)
{
    if (Sim()->getCfg()->hasKey(key))
        return Sim()->getCfg()->getString(key);
    return default_val;
}

DramPerfModelCXL::DramPerfModelCXL(core_id_t core_id, UInt32 cache_block_size,
                                   AddressHomeLookup* address_home_lookup, const String& suffix)
    : DramPerfModel(core_id, cache_block_size, suffix)
    , m_core_id(core_id)
    , m_address_home_lookup(address_home_lookup)
    , m_cache_block_size(cache_block_size)
    
    // CXL-specific latencies (silicon-validated defaults from CXL-DMSim)
    , m_bridge_latency(SubsecondTime::NS() * getCfgIntSafe(
        "perf_model/cxl/bridge_latency", 50))  // 50ns typical
    , m_host_protocol_latency(SubsecondTime::NS() * getCfgIntSafe(
        "perf_model/cxl/host_protocol_latency", 12))  // 12ns typical
    , m_device_protocol_latency(SubsecondTime::NS() * getCfgIntSafe(
        "perf_model/cxl/device_protocol_latency", 15))  // 15ns ASIC, 60ns FPGA
    , m_link_latency(SubsecondTime::NS() * getCfgIntSafe(
        "perf_model/cxl/link_latency", 10))  // ~10ns link propagation
    
    // Backend DRAM parameters
    , m_backend_dram_latency(SubsecondTime::NS() * getCfgIntSafe(
        "perf_model/cxl/backend_dram_latency", 100))  // 100ns for DDR5
    , m_num_banks(getCfgIntSafe("perf_model/cxl/num_banks", 16))
    , m_num_bank_groups(getCfgIntSafe("perf_model/cxl/num_bank_groups", 4))
    , m_num_ranks(getCfgIntSafe("perf_model/cxl/num_ranks", 2))
    , m_num_channels(getCfgIntSafe("perf_model/cxl/num_channels", 1))
    , m_total_banks(m_num_banks * m_num_ranks * m_num_channels)
    
    // Row buffer timing
    , m_bank_keep_open(SubsecondTime::NS() * getCfgIntSafe(
        "perf_model/cxl/bank_keep_open", 15000))  // 15us
    , m_bank_open_delay(SubsecondTime::NS() * getCfgIntSafe(
        "perf_model/cxl/bank_open_delay", 15))  // tRCD ~15ns
    , m_bank_close_delay(SubsecondTime::NS() * getCfgIntSafe(
        "perf_model/cxl/bank_close_delay", 15))  // tRP ~15ns
    , m_dram_access_cost(SubsecondTime::NS() * getCfgIntSafe(
        "perf_model/cxl/dram_access_cost", 15))  // tCAS ~15ns
    
    // Page size
    , m_dram_page_size(getCfgIntSafe("perf_model/cxl/page_size", 8192))  // 8KB row buffer
    , m_dram_page_size_log2(floorLog2(m_dram_page_size))
    
    // CXL link bandwidth (PCIe 5.0 x16 = 32 GB/s per direction)
    , m_cxl_link_bandwidth(8 * getCfgFloatSafe(
        "perf_model/cxl/link_bandwidth", 32.0))  // 32 GB/s default
    , m_cxl_link_bandwidth_gbps(getCfgFloatSafe("perf_model/cxl/link_bandwidth", 32.0))
    
    // Queue depths
    , m_link_request_fifo_depth(getCfgIntSafe(
        "perf_model/cxl/link_request_fifo_depth", 128))
    , m_link_response_fifo_depth(getCfgIntSafe(
        "perf_model/cxl/link_response_fifo_depth", 128))
    , m_device_request_fifo_depth(getCfgIntSafe(
        "perf_model/cxl/device_request_fifo_depth", 48))
    , m_device_response_fifo_depth(getCfgIntSafe(
        "perf_model/cxl/device_response_fifo_depth", 48))
    
    // Initialize statistics
    , m_cxl_reads(0)
    , m_cxl_writes(0)
    , m_page_hits(0)
    , m_page_misses(0)
    , m_page_empty(0)
    , m_total_access_latency(SubsecondTime::Zero())
    , m_total_cxl_overhead_latency(SubsecondTime::Zero())
    , m_total_backend_dram_latency(SubsecondTime::Zero())
    , m_total_queueing_delay(SubsecondTime::Zero())
    , m_bytes_transferred(0)
{
    // Calculate total CXL overhead (fixed cost per access)
    m_total_cxl_overhead = m_bridge_latency + m_host_protocol_latency + 
                           m_device_protocol_latency + m_link_latency;
    
    // Initialize bank state
    m_banks.resize(m_total_banks);
    
    // Create queue model for CXL link contention
    String queue_model_type = getCfgStringSafe("perf_model/cxl/queue_model/type", "contention");
    m_cxl_link_queue_model = QueueModel::create("cxl-link-queue" + m_suffix, m_core_id, 
                                                 queue_model_type, 
                                                 m_cxl_link_bandwidth.getRoundedLatency(8 * m_cache_block_size));
    
    // Register statistics
    String cxl_name("cxl" + m_suffix);
    registerStatsMetric(cxl_name, m_core_id, "reads", &m_cxl_reads);
    registerStatsMetric(cxl_name, m_core_id, "writes", &m_cxl_writes);
    registerStatsMetric(cxl_name, m_core_id, "page_hits", &m_page_hits);
    registerStatsMetric(cxl_name, m_core_id, "page_misses", &m_page_misses);
    registerStatsMetric(cxl_name, m_core_id, "page_empty", &m_page_empty);
    registerStatsMetric(cxl_name, m_core_id, "bytes_transferred", &m_bytes_transferred);
    
    LOG_PRINT("CXL Memory Model initialized for core %d", m_core_id);
    LOG_PRINT("  CXL overhead: bridge=%luns, host_proto=%luns, dev_proto=%luns, link=%luns",
              m_bridge_latency.getNS(), m_host_protocol_latency.getNS(),
              m_device_protocol_latency.getNS(), m_link_latency.getNS());
    LOG_PRINT("  Total CXL overhead: %luns", m_total_cxl_overhead.getNS());
    LOG_PRINT("  Backend DRAM latency: %luns", m_backend_dram_latency.getNS());
    LOG_PRINT("  CXL link bandwidth: %.2f GB/s", m_cxl_link_bandwidth_gbps);
}

DramPerfModelCXL::~DramPerfModelCXL()
{
    delete m_cxl_link_queue_model;
}

//=============================================================================
// Address mapping helpers
//=============================================================================

UInt32 DramPerfModelCXL::getBank(IntPtr address) const
{
    // Simple interleaved bank mapping
    return (address >> 6) % m_num_banks;  // 64-byte cache line granularity
}

UInt32 DramPerfModelCXL::getBankGroup(IntPtr address) const
{
    return (address >> (6 + floorLog2(m_num_banks))) % m_num_bank_groups;
}

UInt32 DramPerfModelCXL::getRank(IntPtr address) const
{
    return (address >> (6 + floorLog2(m_num_banks) + floorLog2(m_num_bank_groups))) % m_num_ranks;
}

UInt32 DramPerfModelCXL::getChannel(IntPtr address) const
{
    return (address >> (6 + floorLog2(m_num_banks) + floorLog2(m_num_bank_groups) + 
                        floorLog2(m_num_ranks))) % m_num_channels;
}

IntPtr DramPerfModelCXL::getPage(IntPtr address) const
{
    return address >> m_dram_page_size_log2;
}

//=============================================================================
// Backend DRAM latency calculation (with row buffer modeling)
//=============================================================================

SubsecondTime DramPerfModelCXL::getBackendDramLatency(SubsecondTime pkt_time, 
                                                       IntPtr address,
                                                       DramCntlrInterface::access_t access_type)
{
    // Calculate which bank and page
    UInt32 bank_idx = getChannel(address) * m_num_ranks * m_num_banks + 
                      getRank(address) * m_num_banks + getBank(address);
    IntPtr page = getPage(address);
    
    BankInfo& bank = m_banks[bank_idx];
    SubsecondTime latency = SubsecondTime::Zero();
    
    // Check row buffer state
    if (bank.is_page_open)
    {
        // Check if row buffer has timed out
        if (pkt_time > bank.t_avail + m_bank_keep_open)
        {
            // Row buffer closed due to timeout
            bank.is_page_open = false;
            bank.open_page = 0;
        }
    }
    
    if (!bank.is_page_open)
    {
        // Page empty: need to open row
        latency = m_bank_open_delay + m_dram_access_cost;
        bank.is_page_open = true;
        bank.open_page = page;
        m_page_empty++;
    }
    else if (bank.open_page == page)
    {
        // Page hit: row buffer hit
        latency = m_dram_access_cost;
        m_page_hits++;
    }
    else
    {
        // Page miss: need to close current row and open new one
        latency = m_bank_close_delay + m_bank_open_delay + m_dram_access_cost;
        bank.open_page = page;
        m_page_misses++;
    }
    
    // Update bank availability time
    SubsecondTime access_start = std::max(pkt_time, bank.t_avail);
    bank.t_avail = access_start + latency;
    
    // Add any waiting time if bank was busy
    if (pkt_time < bank.t_avail - latency)
    {
        latency += (bank.t_avail - latency - pkt_time);
    }
    
    return latency;
}

//=============================================================================
// CXL link latency (with contention modeling)
//=============================================================================

SubsecondTime DramPerfModelCXL::getCXLLinkLatency(SubsecondTime pkt_time, UInt64 pkt_size)
{
    SubsecondTime processing_time = m_cxl_link_bandwidth.getRoundedLatency(8 * pkt_size);
    
    // Model queueing delay on CXL link
    SubsecondTime queue_delay = m_cxl_link_queue_model->computeQueueDelay(
        pkt_time, processing_time, m_core_id);
    
    m_total_queueing_delay += queue_delay;
    
    return processing_time + queue_delay;
}

//=============================================================================
// Main access latency calculation
//=============================================================================

SubsecondTime DramPerfModelCXL::getAccessLatency(SubsecondTime pkt_time, 
                                                  UInt64 pkt_size,
                                                  core_id_t requester, 
                                                  IntPtr address,
                                                  DramCntlrInterface::access_t access_type,
                                                  ShmemPerf *perf, 
                                                  bool is_metadata)
{
    // Track access type
    if (access_type == DramCntlrInterface::READ)
        m_cxl_reads++;
    else
        m_cxl_writes++;
    
    m_num_accesses++;
    m_bytes_transferred += pkt_size;
    
    //=========================================================================
    // CXL Memory Access Latency Components:
    //
    // 1. CXL Link Transfer (request): bandwidth-limited, queued
    // 2. Bridge Latency: PCIe/CXL bridge traversal
    // 3. Host Protocol Latency: CXL.mem packet processing at host
    // 4. Device Protocol Latency: CXL controller at device
    // 5. Backend DRAM Access: actual memory access in CXL device
    // 6. CXL Link Transfer (response): bandwidth-limited, queued
    //=========================================================================
    
    // 1. Request transfer on CXL link
    SubsecondTime link_request_latency = getCXLLinkLatency(pkt_time, m_cache_block_size);
    
    // 2-4. Fixed CXL overhead (bridge + host_proto + dev_proto + link propagation)
    SubsecondTime cxl_overhead = m_total_cxl_overhead;
    
    // 5. Backend DRAM access
    SubsecondTime current_time = pkt_time + link_request_latency + cxl_overhead;
    SubsecondTime backend_dram_latency = getBackendDramLatency(current_time, address, access_type);
    
    // 6. Response transfer on CXL link
    current_time += backend_dram_latency;
    SubsecondTime link_response_latency = getCXLLinkLatency(current_time, pkt_size);
    
    // Total latency
    SubsecondTime total_latency = link_request_latency + cxl_overhead + 
                                   backend_dram_latency + link_response_latency;
    
    // Update statistics
    m_total_access_latency += total_latency;
    m_total_cxl_overhead_latency += cxl_overhead + link_request_latency + link_response_latency;
    m_total_backend_dram_latency += backend_dram_latency;
    
    // Update performance tracking (use DRAM_DEVICE for CXL as well)
    if (perf)
    {
        perf->updateTime(pkt_time, ShmemPerf::DRAM);
        perf->updateTime(pkt_time + total_latency, ShmemPerf::DRAM_DEVICE);
    }
    
    return total_latency;
}

//=============================================================================
// Bandwidth utilization
//=============================================================================

double DramPerfModelCXL::getBandwidthUtilization() const
{
    // Calculate approximate bandwidth utilization based on bytes transferred
    // and total access latency
    if (m_total_access_latency == SubsecondTime::Zero())
        return 0.0;
    
    double bytes_per_second = (double)m_bytes_transferred / 
                              m_total_access_latency.getInternalDataForced() * 1e15;
    double max_bandwidth = m_cxl_link_bandwidth_gbps * 1e9;
    
    return bytes_per_second / max_bandwidth;
}
