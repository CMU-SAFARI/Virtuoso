#ifndef __DRAM_PERF_MODEL_DISAGG_H__
#define __DRAM_PERF_MODEL_DISAGG_H__

#include "dram_perf_model.h"
#include "queue_model.h"
#include "fixed_types.h"
#include "subsecond_time.h"
#include "dram_cntlr_interface.h"
#include "address_home_lookup.h"

#include <vector>
#include <bitset>
#include <map>
#include <list>
#include <algorithm>

class DramPerfModelDisagg : public DramPerfModel
{
   private:
      const core_id_t m_core_id;
      const AddressHomeLookup* m_address_home_lookup;
      //ParametricDramDirectoryMSI::MemoryManager * m_memory_manager; //Adding link to mmu
      const UInt32 m_num_banks;       // number of banks in a rank
      const UInt32 m_num_banks_log2;
      const UInt32 m_num_bank_groups; // number of bank groups in a rank
      const UInt32 m_num_ranks;
      const UInt32 m_rank_offset;
      const UInt32 m_num_channels;
      const UInt32 m_channel_offset;
      const UInt32 m_home_lookup_bit;
      const UInt32 m_total_ranks;
      const UInt32 m_banks_per_channel;
      const UInt32 m_banks_per_bank_group;
      const UInt32 m_total_banks;
      const UInt32 m_total_bank_groups;
      const UInt32 m_data_bus_width;  // bus between dram and memory controller
      const UInt32 m_dram_speed;      // MHz, 533, 667, etc.
      const UInt32 m_dram_page_size;  // dram page size in bytes
      const UInt32 m_dram_page_size_log2;
      const bool m_open_page_mapping;
      const UInt32 m_column_offset;
      const UInt32 m_column_hi_offset;
      const UInt32 m_bank_offset;
      const bool m_randomize_address;
      const UInt32 m_randomize_offset;
      const UInt32 m_column_bits_shift; // position of column bits for closed-page mapping (after cutting interleaving/channel/rank/bank from bottom)
      const ComponentBandwidth m_bus_bandwidth;
      // const ComponentBandwidth m_r_bus_bandwidth; //remote
      // const ComponentBandwidth m_r_part_bandwidth; //remote
      // const ComponentBandwidth m_r_part2_bandwidth; //remote
      const SubsecondTime m_bank_keep_open;
      const SubsecondTime m_bank_open_delay;
      const SubsecondTime m_bank_close_delay;
      const SubsecondTime m_dram_access_cost;
      const SubsecondTime m_intercommand_delay;
      const SubsecondTime m_intercommand_delay_short;
      const SubsecondTime m_intercommand_delay_long;
      const SubsecondTime m_controller_delay;
      const SubsecondTime m_refresh_interval;
      const SubsecondTime m_refresh_length;


      // const SubsecondTime m_r_added_latency; //Additional remote latency
      // const UInt32 m_r_datamov_threshold; //Mov data if greater than yy
       const UInt32 m_localdram_size; //Mov data if greater than yy
      // const bool m_enable_remote_mem; //Enable remote memory with the same DDR type as local for now
      // const bool m_r_simulate_tlb_overhead; //Simulate tlb overhead
      // const bool m_r_simulate_datamov_overhead; //Simulate tlb overhead
      // const UInt32 m_r_mode ; //Randomly assigned = 0; Cache = 1  
      // const UInt32 m_r_partitioning_ratio ; // % in local memory 
      // const bool m_r_simulate_sw_pagereclaim_overhead; //Simulate tlb overhead
      // const bool m_r_exclusive_cache; //Simulate tlb overhead
      // const bool m_remote_init; 
      // const bool m_r_enable_nl_prefetcher; 
      // const UInt32 m_r_disturbance_factor; 
      // const bool m_r_dontevictdirty; make
      // const bool m_r_enable_selective_moves; 
      // const UInt32 m_r_partition_queues; 
      // const bool m_r_cacheline_gran; 
      // const UInt32 m_r_reserved_bufferspace; 
      // const UInt32 m_r_limit_redundant_moves; 
      // const bool m_r_throttle_redundant_moves; 
	//Local 

      std::vector<QueueModel*> m_queue_model;
      std::vector<QueueModel*> m_rank_avail;
      std::vector<QueueModel*> m_bank_group_avail;
      

      typedef enum
      {
         METADATA,
         NOT_METADATA,
         NUMBER_OF_TYPES
      }page_type;
      struct BankInfo
      {
         core_id_t core_id;
         IntPtr open_page;
         SubsecondTime t_avail;
         page_type open_page_type;
      };
      std::vector<BankInfo> m_banks;

     //Remote memory
      // std::vector<QueueModel*> m_r_queue_model;
      // std::vector<QueueModel*> m_r_rank_avail;
      // std::vector<QueueModel*> m_r_bank_group_avail;

      // std::vector<BankInfo> m_r_banks;

      // std::map<UInt64, UInt32> m_remote_access_tracker; 
      std::list<UInt64> m_local_pages;
      // std::list<UInt64> m_remote_pages;
      std::list<UInt64> m_dirty_pages;
      std::map<UInt64, SubsecondTime> m_inflight_pages; 
      std::map<UInt64, UInt32> m_inflight_redundant; 
      std::map<UInt64, SubsecondTime> m_inflightevicted_pages; 


      UInt64 m_page_hits;
      UInt64 m_page_empty;
      UInt64 m_page_closing;
      UInt64 m_page_miss;
      UInt64 m_page_conflict_metadata_to_data;
      UInt64 m_page_conflict_data_to_metadata;
      UInt64 m_page_conflict_data_to_data;
      UInt64 m_page_conflict_metadata_to_metadata;
      // UInt64 m_remote_reads;
      // UInt64 m_remote_writes;
      UInt64 m_data_moves;
      UInt64 m_page_prefetches;
      UInt64 m_inflight_hits;
      UInt64 m_writeback_pages;
      UInt64 m_local_evictions;
      UInt64 m_extra_pages;
      UInt64 m_redundant_moves;
      UInt64 m_max_bufferspace;

      SubsecondTime m_total_queueing_delay;
      SubsecondTime m_total_access_latency;

      bool constant_time_policy; 
      bool selective_constant_time_policy; 
      bool open_row_policy;

      void parseDeviceAddress(IntPtr address, UInt32 &channel, UInt32 &rank, UInt32 &bank_group, UInt32 &bank, UInt32 &column, UInt64 &page);
      UInt64 parseAddressBits(UInt64 address, UInt32 &data, UInt32 offset, UInt32 size, UInt64 base_address);
      // SubsecondTime possiblyEvict(UInt64 phys_page, SubsecondTime pkt_time, core_id_t requester); 
      // void possiblyPrefetch(UInt64 phys_page, SubsecondTime pkt_time, core_id_t requester); 

   public:
      //DramPerfModelDisagg(String name, core_id_t core_id, AddressHomeLookup* address_home_lookup, UInt32 cache_block_size, ParametricDramDirectoryMSI::MemoryManager* memory_manager);
      DramPerfModelDisagg(core_id_t core_id, UInt32 cache_block_size, AddressHomeLookup* address_home_lookup);

      ~DramPerfModelDisagg();

      //bool isRemoteAccess(IntPtr address, core_id_t requester, DramCntlrInterface::access_t access_type); 
      //Changing this... not sure whether queue_delay is needed); 
      //SubsecondTime getAccessLatencyRemote(SubsecondTime pkt_time, UInt64 pkt_size, core_id_t requester, IntPtr address, DramCntlrInterface::access_t access_type, ShmemPerf *perf, SubsecondTime &queue_delay);
      // SubsecondTime getAccessLatencyRemote(SubsecondTime pkt_time, UInt64 pkt_size, core_id_t requester, IntPtr address, DramCntlrInterface::access_t access_type, ShmemPerf *perf);

      //SubsecondTime getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size, core_id_t requester, IntPtr address, DramCntlrInterface::access_t access_type, ShmemPerf *perf, SubsecondTime &queue_delay);
      SubsecondTime getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size, core_id_t requester, IntPtr address, DramCntlrInterface::access_t access_type, ShmemPerf *perf,bool is_metadata);
};

#endif /* __DRAM_PERF_MODEL_DISAGG_H__ */