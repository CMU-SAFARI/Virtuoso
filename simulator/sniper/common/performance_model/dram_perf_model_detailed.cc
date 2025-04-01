#include "dram_perf_model_detailed.h"
#include "simulator.h"
#include "config.h"
#include "config.hpp"
#include "stats.h"
#include "shmem_perf.h"
#include "subsecond_time.h"
#include "utils.h"

//#define DEBUG_PRINT


DramPerfModelDetailed::DramPerfModelDetailed(core_id_t core_id, UInt32 cache_block_size, AddressHomeLookup* address_home_lookup)
   : DramPerfModel(core_id, cache_block_size)
   , m_core_id(core_id)
   , m_address_home_lookup(address_home_lookup)
   , m_num_banks           (Sim()->getCfg()->getInt("perf_model/dram/ddr/num_banks"))
   , m_num_banks_log2      (floorLog2(m_num_banks))
   , m_num_bank_groups     (Sim()->getCfg()->getInt("perf_model/dram/ddr/num_bank_groups"))
   , m_num_ranks           (Sim()->getCfg()->getInt("perf_model/dram/ddr/num_ranks"))
   , m_rank_offset         (Sim()->getCfg()->getInt("perf_model/dram/ddr/rank_offset"))
   , m_num_channels        (Sim()->getCfg()->getInt("perf_model/dram/ddr/num_channels"))
   , m_channel_offset      (Sim()->getCfg()->getInt("perf_model/dram/ddr/channel_offset"))
   , m_home_lookup_bit     (Sim()->getCfg()->getInt("perf_model/dram_directory/home_lookup_param"))
   , m_total_ranks         (m_num_ranks * m_num_channels)
   , m_banks_per_channel   (m_num_banks * m_num_ranks)
   , m_banks_per_bank_group (m_num_banks / m_num_bank_groups)
   , m_total_banks         (m_banks_per_channel * m_num_channels)
   , m_total_bank_groups   (m_num_bank_groups * m_num_ranks * m_num_channels)
   , m_data_bus_width      (Sim()->getCfg()->getInt("perf_model/dram/ddr/data_bus_width"))   // In bits
   , m_dram_speed          (Sim()->getCfg()->getInt("perf_model/dram/ddr/dram_speed"))       // In MHz
   , m_dram_page_size      (Sim()->getCfg()->getInt("perf_model/dram/ddr/dram_page_size"))
   , m_dram_page_size_log2 (floorLog2(m_dram_page_size))
   , m_open_page_mapping   (Sim()->getCfg()->getBool("perf_model/dram/ddr/open_page_mapping"))
   , m_column_offset       (Sim()->getCfg()->getInt("perf_model/dram/ddr/column_offset"))
   , m_column_hi_offset    (m_dram_page_size_log2 - m_column_offset + m_num_banks_log2) // Offset for higher order column bits
   , m_bank_offset         (m_dram_page_size_log2 - m_column_offset) // Offset for bank bits
   , m_randomize_address   (Sim()->getCfg()->getBool("perf_model/dram/ddr/randomize_address"))
   , m_randomize_offset    (Sim()->getCfg()->getInt("perf_model/dram/ddr/randomize_offset"))
   , m_column_bits_shift   (Sim()->getCfg()->getInt("perf_model/dram/ddr/column_bits_shift"))
   , m_bus_bandwidth       (m_dram_speed * m_data_bus_width / 1000) // In bits/ns: MT/s=transfers/us * bits/transfer
   , m_bank_keep_open      (SubsecondTime::NS() * static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/ddr/bank_keep_open")))
   , m_bank_open_delay     (SubsecondTime::NS() * static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/ddr/bank_open_delay")))
   , m_bank_close_delay    (SubsecondTime::NS() * static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/ddr/bank_close_delay")))
   , m_dram_access_cost    (SubsecondTime::NS() * static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/ddr/access_cost")))
   , m_intercommand_delay  (SubsecondTime::NS() * static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/ddr/intercommand_delay")))
   , m_intercommand_delay_short  (SubsecondTime::NS() * static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/ddr/intercommand_delay_short")))
   , m_intercommand_delay_long  (SubsecondTime::NS() * static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/ddr/intercommand_delay_long")))
   , m_controller_delay    (SubsecondTime::NS() * static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/ddr/controller_delay")))
   , m_refresh_interval    (SubsecondTime::NS() * static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/ddr/refresh_interval")))
   , m_refresh_length      (SubsecondTime::NS() * static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/ddr/refresh_length")))
   , m_localdram_size       (Sim()->getCfg()->getInt("perf_model/dram/localdram_size"))// Move data if greater than
   , m_banks               (m_total_banks)
   , m_page_hits           (0)
   , m_page_empty          (0)
   , m_page_closing        (0)
   , m_page_miss           (0)
   , m_page_conflict_metadata_to_data(0)
   , m_page_conflict_data_to_metadata(0)
   , m_page_conflict_data_to_data(0)
   , m_page_conflict_metadata_to_metadata(0)
   , m_received_request_from_the_past(0)
   , m_received_request_from_the_unknown_past(0)
   , m_received_request_from_the_present(0)
   , m_total_queueing_delay(SubsecondTime::Zero())
   , m_total_access_latency(SubsecondTime::Zero())
   , constant_time_policy(Sim()->getCfg()->getBool("perf_model/dram/ddr/constant_time_policy"))
   , selective_constant_time_policy(Sim()->getCfg()->getBool("perf_model/dram/ddr/selective_constant_time_policy"))
   , open_row_policy(Sim()->getCfg()->getBool("perf_model/dram/ddr/open_row_policy"))
 {


    
     String name("dram"); 
   if (Sim()->getCfg()->getBool("perf_model/dram/queue_model/enabled"))
   {

      for(UInt32 channel = 0; channel < m_num_channels; ++channel) {
         
         m_queue_model.push_back(QueueModel::create(
            name + "-queue-" + itostr(channel), core_id, Sim()->getCfg()->getString("perf_model/dram/queue_model/type"),
            m_bus_bandwidth.getRoundedLatency(8))); // bytes to bits

      } 
      
   }

   
   registerStatsMetric("dram", core_id, "total-access-latency", &m_total_access_latency); 
   for(UInt32 rank = 0; rank < m_total_ranks; ++rank) {
      m_rank_avail.push_back(QueueModel::create(
         name + "-rank-" + itostr(rank), core_id, "history_list",
         (m_num_bank_groups > 1) ? m_intercommand_delay_short : m_intercommand_delay));

   }
   
   for(UInt32 group = 0; group < m_total_bank_groups; ++group) {
      m_bank_group_avail.push_back(QueueModel::create(
         name + "-bank-group-" + itostr(group), core_id, "history_list",
         m_intercommand_delay_long));
   }


   for (UInt32 bank = 0; bank < m_total_banks; ++bank)
   {
      m_banks[bank].core_id = -1;
      m_banks[bank].open_page = -1;
      m_banks[bank].t_avail = SubsecondTime::Zero();
      m_banks[bank].max_time = SubsecondTime::Zero();
      m_banks[bank].max_page = -1;
      m_banks[bank].open_page_type = NOT_METADATA;
   }
   
   LOG_ASSERT_ERROR(cache_block_size == 64, "Hardcoded for 64-byte cache lines");
   LOG_ASSERT_ERROR(m_column_offset <= m_dram_page_size_log2, "Column offset exceeds bounds!");
   if(m_randomize_address)
      LOG_ASSERT_ERROR(m_num_bank_groups == 4 || m_num_bank_groups == 8, "Number of bank groups incorrect for address randomization!");
   
   registerStatsMetric("ddr", core_id, "page-hits", &m_page_hits);
   registerStatsMetric("ddr", core_id, "page-empty", &m_page_empty);
   registerStatsMetric("ddr", core_id, "page-closing", &m_page_closing);
   registerStatsMetric("ddr", core_id, "page-miss", &m_page_miss);
   registerStatsMetric("ddr", core_id, "page-conflict-data-to-metadata", &m_page_conflict_data_to_metadata);
   registerStatsMetric("ddr", core_id, "page-conflict-metadata-to-data", &m_page_conflict_metadata_to_data);
   registerStatsMetric("ddr", core_id, "page-conflict-metadata-to-metadata", &m_page_conflict_metadata_to_metadata);
   registerStatsMetric("ddr", core_id, "page-conflict-data-to-data", &m_page_conflict_data_to_data);


   registerStatsMetric("dram", core_id, "received-request-from-the-past", &m_received_request_from_the_past);
   registerStatsMetric("dram", core_id, "received-request-from-the-unknown-past", &m_received_request_from_the_unknown_past);
   registerStatsMetric("dram", core_id, "received-request-from-present", &m_received_request_from_the_present);

   
}

DramPerfModelDetailed::~DramPerfModelDetailed()
{

   if (m_queue_model.size())
   {
      for(UInt32 channel = 0; channel < m_num_channels; ++channel)
         delete m_queue_model[channel];
   }
   if (m_rank_avail.size())
   {
      for(UInt32 rank = 0; rank < m_total_ranks; ++rank)
         delete m_rank_avail[rank];
   }

   if (m_bank_group_avail.size())
   {
      for(UInt32 group = 0; group < m_total_bank_groups; ++group)
         delete m_bank_group_avail[group];
   }

}

UInt64
DramPerfModelDetailed::parseAddressBits(UInt64 address, UInt32 &data, UInt32 offset, UInt32 size, UInt64 base_address = 0)
{
   //parse data from the address based on the offset and size, return the address without the bits used to parse the data.
   UInt32 log2_size = floorLog2(size);
   if (base_address != 0) {
      data = (base_address >> offset) % size;
   } else {
      data = (address >> offset) % size;
   }
   return ((address >> (offset + log2_size)) << offset) | (address & ((1 << offset) - 1));
}

void
DramPerfModelDetailed::parseDeviceAddress(IntPtr address, UInt32 &channel, UInt32 &rank, UInt32 &bank_group, UInt32 &bank, UInt32 &column, UInt64 &page)
{
   // Construct DDR address which has bits used for interleaving removed
   UInt64 linearAddress = m_address_home_lookup->getLinearAddress(address);
   UInt64 address_bits = linearAddress >> 6;


   #ifdef DEBUG_PRINT
      std::cout << "Address bits: " << std::bitset<64>(address_bits) << std::endl;
   #endif

   if (m_open_page_mapping)
   {
      // Open-page mapping: column address is bottom bits, then bank, then page
      if(m_column_offset)
      {
         // Column address is split into 2 halves ColHi and ColLo and
         // the address looks like: | Page | ColHi | Bank | ColLo |
         // m_column_offset specifies the number of ColHi bits
         column = (((address_bits >> m_column_hi_offset) << m_bank_offset)
               | (address_bits & ((1 << m_bank_offset) - 1))) % m_dram_page_size;
         address_bits = address_bits >> m_bank_offset;
         bank_group = address_bits % m_num_bank_groups;
         bank = address_bits % m_num_banks;
         address_bits = address_bits >> (m_num_banks_log2 + m_column_offset);
      }
      else
      {
         channel = address_bits % m_num_channels; address_bits /= m_num_channels;
         
         #ifdef DEBUG_PRINT
            std::cout << "Channel: " << channel << std::endl;
         #endif
         column = address_bits % m_dram_page_size; address_bits /= m_dram_page_size;

         #ifdef DEBUG_PRINT
            std::cout << "Column: " << column << std::endl;
         #endif




         rank = address_bits % m_num_ranks; address_bits /= m_num_ranks;
         
         #ifdef DEBUG_PRINT
            std::cout << "Rank: " << rank << std::endl;
         #endif

         bank_group = address_bits % m_num_bank_groups;

         #ifdef DEBUG_PRINT
            std::cout << "Bank Group: " << bank_group << std::endl;
         #endif

         bank = address_bits % m_num_banks; address_bits /= m_num_banks;

         #ifdef DEBUG_PRINT
            std::cout << "Bank: " << bank << std::endl;
         #endif
      }
      page = address_bits;

      #ifdef DEBUG_PRINT
         std::cout << "Page: " << page << std::endl;
      #endif

#if 0
      // Test address parsing done in this function for open page mapping
      std::bitset<10> bs_col (column);
      std::string str_col = bs_col.to_string<char,std::string::traits_type,std::string::allocator_type>();
      std::stringstream ss_original, ss_recomputed;
      ss_original << std::bitset<64>(linearAddress >> m_block_size_log2) << std::endl;
      ss_recomputed << std::bitset<50>(page) << str_col.substr(0,m_column_offset) << std::bitset<4>(bank)
         << str_col.substr(m_column_offset, str_col.length()-m_column_offset) << std::endl;
      LOG_ASSERT_ERROR(ss_original.str() == ss_recomputed.str(), "Error in device address parsing!");
#endif
   }
   else
   {
      bank_group = address_bits % m_num_bank_groups;
      bank = address_bits % m_num_banks;
      address_bits /= m_num_banks;

      // Closed-page mapping: column address is bits X+banksize:X, row address is everything else
      // (from whatever is left after cutting channel/rank/bank from the bottom)
      column = (address_bits >> m_column_bits_shift) % m_dram_page_size;
      page = (((address_bits >> m_column_bits_shift) / m_dram_page_size) << m_column_bits_shift)
           | (address_bits & ((1 << m_column_bits_shift) - 1));
   }

   if(m_randomize_address)
   {
      std::bitset<3> row_bits(page >> m_randomize_offset);                 // Row[offset+2:offset]
      UInt32 row_bits3 = row_bits.to_ulong();
      row_bits[2] = 0;
      UInt32 row_bits2 = row_bits.to_ulong();
      bank_group ^= ((m_num_bank_groups == 8) ? row_bits3 : row_bits2);    // BankGroup XOR Row
      bank /= m_num_bank_groups;
      bank ^= row_bits2;                                                   // Bank XOR Row
      bank = m_banks_per_bank_group * bank_group + bank;
      rank = (m_num_ranks > 1) ? rank ^ row_bits[0] : rank;                // Rank XOR Row
   }

   //printf("[%2d] address %12lx linearAddress %12lx channel %2x rank %2x bank_group %2x bank %2x page %8lx crb %4u\n", m_core_id, address, linearAddress, channel, rank, bank_group, bank, page, (((channel * m_num_ranks) + rank) * m_num_banks) + bank);
}


std::pair<SubsecondTime, DramPerfModelDetailed::IntervalNode>  DramPerfModelDetailed::fallsWithinInterval(UInt64 page, SubsecondTime pkt_time, IntPtr bank) {

    if (m_banks[bank].m_bank_busy_intervals.empty()) {
        // No intervals, bank is immediately available
        return {pkt_time, IntervalNode()}; // Return pkt_time as the available cycle
    }

    std::priority_queue<IntervalNode> temp = m_banks[bank].m_bank_busy_intervals;

   IntervalNode last_interval;

   last_interval.start_time = SubsecondTime::Zero();
   last_interval.end_time = SubsecondTime::Zero();
   last_interval.open_page = -1;

    while (!temp.empty()) {
        IntervalNode interval = temp.top();
        temp.pop();

        if (interval.start_time > pkt_time) {

            if(last_interval.open_page == -1){

               m_received_request_from_the_unknown_past++;

               #ifdef DEBUG_PRINT
                  std::cout << "DRAM received request from the unknown past Counter: " << m_received_request_from_the_unknown_past << std::endl;
                  std::cout << "pkt_time: " << pkt_time.getNS() << " interval.start_time: " << interval.start_time.getNS() << " interval.end_time: " << interval.end_time.getNS() << "max_time: " << m_banks[bank].max_time.getNS() << std::endl;
               #endif


            }
            else {

               m_received_request_from_the_past++;

               #ifdef DEBUG_PRINT
                  std::cout << "DRAM received request from the past Counter: " << m_received_request_from_the_past << std::endl;
               #endif
            }
            // Found an interval that starts after pkt_time
            return {pkt_time, last_interval}; // Return pkt_time as the available cycle
        }

        if (interval.start_time <= pkt_time && interval.end_time >= pkt_time) {

            m_received_request_from_the_past++;

            #ifdef DEBUG_PRINT
               std::cout << "DRAM received request from the past Counter: " << m_received_request_from_the_past << std::endl;
            #endif
         
            SubsecondTime next_avail = interval.end_time + SubsecondTime::NS(1); // +1 ensures availability after this interval ends
            // Overlap found, return the next available cycle after this interval
            return {next_avail , interval}; // +1 ensures availability after this interval ends
        }
        last_interval = interval;
    }

    // If no intervals overlap with pkt_time, bank is free

    m_received_request_from_the_present++;
   #ifdef DEBUG_PRINT
      std::cout << "DRAM received request from the present Counter: " << m_received_request_from_the_present << std::endl;
   #endif

    return {pkt_time, last_interval}; // Return pkt_time as the available cycle
    
}

void
DramPerfModelDetailed::cleanupBusyIntervals( IntPtr bank){
   m_banks[bank].m_bank_busy_intervals.pop();
}

void DramPerfModelDetailed::printInterval(std::priority_queue<IntervalNode> intervals){
  
   std::priority_queue<IntervalNode> temp = intervals;
   while(!temp.empty()){
      IntervalNode interval = temp.top();
      temp.pop();

      #ifdef DEBUG_PRINT
         std::cout << "Start: " << interval.start_time.getNS() << " End: " << interval.end_time.getNS() << " Page: " << interval.open_page << std::endl;
      #endif

   }
}


SubsecondTime
DramPerfModelDetailed::getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size, core_id_t requester, IntPtr address, DramCntlrInterface::access_t access_type, ShmemPerf *perf,bool is_metadata)
{
   UInt64 phys_page =  address & ~((UInt64(1) << 12) - 1); //Assuming 4K page 

   UInt64 cacheline =  address & ~((UInt64(1) << 6) - 1); //Assuming 4K page 

    std::map<UInt64, SubsecondTime>::iterator i;
    SubsecondTime max=SubsecondTime::Zero();

       if(Sim()->getClockSkewMinimizationServer()->getGlobalTime()>pkt_time){
          max=Sim()->getClockSkewMinimizationServer()->getGlobalTime();
       }
       else{
          max=pkt_time;
       }


   UInt32 channel, rank, bank_group, bank, column;
   UInt64 page;
   parseDeviceAddress(address, channel, rank, bank_group, bank, column, page);


   #ifdef DEBUG_PRINT
    std::cout << "Accessing DRAM data at page " << page << " in bank " << bank << " in bank group " << bank_group << " in rank " << rank << " in channel " << channel << std::endl;
	#endif

   SubsecondTime t_now = pkt_time;
   perf->updateTime(t_now);

   // DDR controller pipeline delay
   t_now += m_controller_delay;
   perf->updateTime(t_now, ShmemPerf::DRAM_CNTLR);

   // DDR refresh
   if (m_refresh_interval != SubsecondTime::Zero())
   {
      SubsecondTime refresh_base = (t_now.getPS() / m_refresh_interval.getPS()) * m_refresh_interval;
      if (t_now - refresh_base < m_refresh_length)
      {
         t_now = refresh_base + m_refresh_length;
         perf->updateTime(t_now, ShmemPerf::DRAM_REFRESH);
      }
   }

   // Page hit/miss
   UInt64 crb = (channel * m_num_ranks * m_num_banks) + (rank * m_num_banks) + bank; // Combine channel, rank, bank to index m_banks
   LOG_ASSERT_ERROR(crb < m_total_banks, "Bank index out of bounds");
   BankInfo &bank_info = m_banks[crb];
   
   SubsecondTime t_avail = t_now;

   if (t_now > bank_info.max_time)
   {
      bank_info.max_time = t_now;
      bank_info.open_page = page;
   }
   else{

      auto result = fallsWithinInterval(page, t_now, crb);
      
      // Update the bank_info struct with the interval information
      t_avail = result.first;

      bank_info.t_avail = result.first;
      bank_info.open_page = result.second.open_page;
   }

   #ifdef DEBUG_PRINT
      printf("[%2d] %s (%12lx, %4lu, %4lu), t_open = %lu, t_now = %lu, bank_info.t_avail = %lu\n", m_core_id, bank_info.open_page == page && bank_info.t_avail + m_bank_keep_open >= t_now ? "Page Hit: " : "Page Miss:", address, crb, page, t_now.getNS() - bank_info.t_avail.getNS(), t_now.getNS(), bank_info.t_avail.getNS());
   #endif
   // Page hit/miss
   if ((bank_info.open_page == page)                  // Last access was to this row
      && (bank_info.t_avail + m_bank_keep_open) >= t_now   // Bank hasn't been closed in the meantime
   )
   {
      // |============| -> Bank became avail and row was open -> |============| -> We will close the bank after m_bank_keep_open
      //               If t_now is far in the future, after the bank has been closed, we will have to wait for the bank to open again    

      //      |=======| If t_now is in the middle of the bank being open, we will have to wait for the bank to be available

      if (bank_info.t_avail > t_now)
      {
         t_now = bank_info.t_avail;
         perf->updateTime(t_now, ShmemPerf::DRAM_BANK_PENDING);
      #ifdef DEBUG_PRINT
         std::cout << "Row hit but DRAM bank busy, waiting for it to become available at " << t_now.getNS() << std::endl;
      #endif
      }
      else{
         #ifdef DEBUG_PRINT
            std::cout << "Row hit and DRAM bank available at " << t_now.getNS() << std::endl;
         #endif
      }
      ++m_page_hits;
   }
   else
   {
      // Wait for bank to become available
      if (bank_info.t_avail > t_now){
         t_now = bank_info.t_avail;
         #ifdef DEBUG_PRINT
         std::cout << "Row miss, waiting for DRAM bank to become available at " << t_now.getNS() << std::endl;
         #endif
      }
      
      // Close page
      if (bank_info.t_avail + m_bank_keep_open >= t_now)
      {
         if(bank_info.open_page_type == page_type::METADATA && !is_metadata){
            ++m_page_conflict_metadata_to_data;
         }
         if(bank_info.open_page_type == page_type::NOT_METADATA && is_metadata){
            ++m_page_conflict_data_to_metadata;
         }
         if(bank_info.open_page_type == page_type::METADATA && is_metadata){
            ++m_page_conflict_metadata_to_metadata;
         }
         if(bank_info.open_page_type == page_type::NOT_METADATA && !is_metadata){
            ++m_page_conflict_data_to_data;
         }
         t_now += m_bank_close_delay;

         #ifdef DEBUG_PRINT
            std::cout << "Closing DRAM bank at " << t_now.getNS() << std::endl;
         #endif

         ++m_page_miss;
      }
      else if (bank_info.t_avail + m_bank_keep_open + m_bank_close_delay > t_now)
      {
         // Bank was being closed, we have to wait for that to complete
         t_now = bank_info.t_avail + m_bank_keep_open +m_bank_close_delay;
         
         ++m_page_closing;
         #ifdef DEBUG_PRINT
            std::cout << "Row miss, waiting for DRAM bank to close at " << t_now.getNS() << std::endl;
         #endif
      }
      else 
      {
         // Bank was already closed, no delay.
         ++m_page_empty;

      }
      // Open page
      t_now += m_bank_open_delay;
      #ifdef DEBUG_PRINT
         std::cout << "Opening DRAM bank at " << t_now.getNS() << std::endl;
      #endif

      perf->updateTime(t_now, ShmemPerf::DRAM_BANK_CONFLICT);
      
      if(is_metadata){
         bank_info.open_page_type=page_type::METADATA;
      }
      else{
         bank_info.open_page_type=page_type::NOT_METADATA;
      }
      if(open_row_policy)
         bank_info.open_page = page;
      else 
         bank_info.open_page = 0;

   }
   bank_info.core_id = requester;
   // Rank access time and availability
   UInt64 cr = (channel * m_num_ranks) + rank;
   LOG_ASSERT_ERROR(cr < m_total_ranks, "Rank index out of bounds");
   SubsecondTime rank_avail_request = (m_num_bank_groups > 1) ? m_intercommand_delay_short : m_intercommand_delay;
   SubsecondTime rank_avail_delay = m_rank_avail.size() ? m_rank_avail[cr]->computeQueueDelay(t_now, rank_avail_request, requester) : SubsecondTime::Zero();

   // Bank group access time and availability
   UInt64 crbg = (channel * m_num_ranks * m_num_bank_groups) + (rank * m_num_bank_groups) + bank_group;
   LOG_ASSERT_ERROR(crbg < m_total_bank_groups, "Bank-group index out of bounds");
   SubsecondTime group_avail_delay = m_bank_group_avail.size() ? m_bank_group_avail[crbg]->computeQueueDelay(t_now, m_intercommand_delay_long, requester) : SubsecondTime::Zero();

   // Device access time (tCAS)
   t_now += m_dram_access_cost;
   perf->updateTime(t_now, ShmemPerf::DRAM_DEVICE);

   #ifdef DEBUG_PRINT
      std::cout << "Finished reading DRAM data at " << t_now.getNS() << std::endl;
   #endif

   // Mark bank as busy until it can receive its next command
   // Done before waiting for the bus to be free: sort of assumes best-case bus scheduling
   bank_info.t_avail = t_now;

   if (bank_info.t_avail > bank_info.max_time){
      bank_info.max_time = bank_info.t_avail;
      bank_info.max_page = page;
   }

   IntervalNode node{t_avail, t_now, page};
   bank_info.m_bank_busy_intervals.push(node);
   printInterval(bank_info.m_bank_busy_intervals);


   //if size of the busy intervals vector is greater than 1000, 
   // remove the element with the earliest start time
   if(bank_info.m_bank_busy_intervals.size() > 100){
         cleanupBusyIntervals(crb);
   }

   #ifdef DEBUG_PRINT
      std::cout << "Inserted busy interval for DRAM bank " << crb << " from " << t_avail.getNS() << " to " << t_now.getNS() << "with page: " << page << std::endl;
   #endif

   // Add the wait time for the larger of bank group and rank availability delay
   t_now += (rank_avail_delay > group_avail_delay) ? rank_avail_delay : group_avail_delay;
   perf->updateTime(t_now, ShmemPerf::DRAM_DEVICE);

   // DDR bus latency and queuing delay
   SubsecondTime ddr_processing_time = m_bus_bandwidth.getRoundedLatency(8 * pkt_size); // bytes to bits
   SubsecondTime ddr_queue_delay = m_queue_model.size() ? m_queue_model[channel]->computeQueueDelay(t_now, ddr_processing_time, requester) : SubsecondTime::Zero();
   t_now += ddr_queue_delay;


   #ifdef DEBUG_PRINT
      std::cout << "There is a queue delay of " << ddr_queue_delay.getNS() << " ns" << std::endl;
   #endif

   perf->updateTime(t_now, ShmemPerf::DRAM_QUEUE);
   t_now += ddr_processing_time;
   perf->updateTime(t_now, ShmemPerf::DRAM_BUS);


   #ifdef DEBUG_PRINT
      std::cout << "There is a bus delay of " << ddr_processing_time.getNS() << " ns" << std::endl;
   #endif


   #ifdef DEBUG_PRINT
      std::cout << "Final DRAM Access Latency: " << t_now.getNS() - pkt_time.getNS() << " Request finished at " << t_now.getNS() << std::endl;
   #endif
   
	//std::cout << "Result: Local" << std::endl;  
	return t_now - pkt_time;
   
}