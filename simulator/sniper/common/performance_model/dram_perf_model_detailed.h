#ifndef __DRAM_PERF_MODEL_DETAILED_H__
#define __DRAM_PERF_MODEL_DETAILED_H__

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
#include <queue>

class DramPerfModelDetailed : public DramPerfModel
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



       const UInt32 m_localdram_size; //Mov data if greater than yy


	   

   
      std::vector<QueueModel*> m_queue_model;
      std::vector<QueueModel*> m_rank_avail;
      std::vector<QueueModel*> m_bank_group_avail;


      // I want to represent DRAM availability intervals using a tree structure
      // There is a tree for each bank in the system and each tree has a node for each interval of time that the bank is busy

      

      typedef enum
      {
         METADATA,
         NOT_METADATA,
         NUMBER_OF_TYPES
      }  page_type;

      struct IntervalNode {
         SubsecondTime start_time; // Start of the interval
         SubsecondTime end_time;   // End of the interval
         IntPtr open_page;         // Page that is open during this interval
         // Priority: based on interval length (example)
         bool operator<(const IntervalNode& node) const {
            return (start_time) > (node.start_time);
         }
      };


      struct BankInfo
      {
         core_id_t core_id;
         IntPtr open_page;
         SubsecondTime t_avail;
         SubsecondTime max_time;
         IntPtr max_page;
         page_type open_page_type;
         std::priority_queue<IntervalNode> m_bank_busy_intervals;
      };

      std::vector<BankInfo> m_banks;

      UInt64 m_page_hits;
      UInt64 m_page_empty;
      UInt64 m_page_closing;
      UInt64 m_page_miss;
      UInt64 m_page_conflict_metadata_to_data;
      UInt64 m_page_conflict_data_to_metadata;
      UInt64 m_page_conflict_data_to_data;
      UInt64 m_page_conflict_metadata_to_metadata;

      UInt64 m_received_request_from_the_past;
      UInt64 m_received_request_from_the_unknown_past;
      UInt64 m_received_request_from_the_present;


      SubsecondTime m_total_queueing_delay;
      SubsecondTime m_total_access_latency;

      bool constant_time_policy; 
      bool selective_constant_time_policy; 
      bool open_row_policy;

      void parseDeviceAddress(IntPtr address, UInt32 &channel, UInt32 &rank, UInt32 &bank_group, UInt32 &bank, UInt32 &column, UInt64 &page);
      UInt64 parseAddressBits(UInt64 address, UInt32 &data, UInt32 offset, UInt32 size, UInt64 base_address);

      std::pair<SubsecondTime, IntervalNode>  fallsWithinInterval(UInt64 page, SubsecondTime pkt_time, IntPtr bank);
      void cleanupBusyIntervals(IntPtr bank);
      void printInterval(std::priority_queue<IntervalNode> intervals);

   public:
      DramPerfModelDetailed(core_id_t core_id, UInt32 cache_block_size, AddressHomeLookup* address_home_lookup);

      ~DramPerfModelDetailed();

      
      SubsecondTime getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size, core_id_t requester, IntPtr address, DramCntlrInterface::access_t access_type, ShmemPerf *perf,bool is_metadata);
};

#endif /* __DRAM_PERF_MODEL_Detailed_H__ */