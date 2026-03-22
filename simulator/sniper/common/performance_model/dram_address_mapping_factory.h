#ifndef __DRAM_ADDRESS_MAPPING_FACTORY_H__
#define __DRAM_ADDRESS_MAPPING_FACTORY_H__

#include "dram_address_mapping.h"
#include "dram_address_mapping_open_page.h"
#include "dram_address_mapping_closed_page.h"
#include "dram_address_mapping_xor.h"
#include "simulator.h"
#include "config.hpp"
#include "log.h"

#include <memory>

/**
 * @brief Factory for creating DRAM address mapping strategies
 * 
 * Creates the appropriate mapping based on configuration:
 * - perf_model/dram/ddr/address_mapping: 
 *     "open_page", "open_page_split_column", "closed_page", "xor"
 */
class DramAddressMappingFactory
{
public:
   /**
    * @brief Create a DRAM address mapping from configuration
    * @param home_lookup Address home lookup for linear address translation
    * @return Unique pointer to the configured mapping
    */
   static std::unique_ptr<DramAddressMapping> create(const AddressHomeLookup* home_lookup)
   {
      // Read common DRAM organization parameters
      UInt32 num_banks = Sim()->getCfg()->getInt("perf_model/dram/ddr/num_banks");
      UInt32 num_bank_groups = Sim()->getCfg()->getInt("perf_model/dram/ddr/num_bank_groups");
      UInt32 num_ranks = Sim()->getCfg()->getInt("perf_model/dram/ddr/num_ranks");
      UInt32 num_channels = Sim()->getCfg()->getInt("perf_model/dram/ddr/num_channels");
      UInt32 dram_page_size = Sim()->getCfg()->getInt("perf_model/dram/ddr/dram_page_size");
      UInt32 banks_per_group = num_banks / num_bank_groups;

      // Read mapping configuration
      String mapping_type = Sim()->getCfg()->getString("perf_model/dram/ddr/address_mapping");

      std::unique_ptr<DramAddressMapping> mapping;

      // Create mapping based on type
      if (mapping_type == "open_page")
      {
         UInt32 channel_bits = Sim()->getCfg()->getInt("perf_model/dram/ddr/open_page/channel_bits");
         UInt32 rank_bits = Sim()->getCfg()->getInt("perf_model/dram/ddr/open_page/rank_bits");
         UInt32 bank_bits = Sim()->getCfg()->getInt("perf_model/dram/ddr/open_page/bank_bits");
         UInt32 page_size_bits = Sim()->getCfg()->getInt("perf_model/dram/ddr/open_page/page_size_bits");

         mapping = std::make_unique<DramAddressMappingOpenPage>(
            home_lookup, num_channels, num_ranks, num_banks, num_bank_groups,
            dram_page_size, channel_bits, rank_bits, bank_bits, page_size_bits);
      }
      else if (mapping_type == "open_page_split_column")
      {
         UInt32 column_offset = Sim()->getCfg()->getInt("perf_model/dram/ddr/open_page_split_column/column_offset");
         UInt32 num_banks_log2 = floorLog2(num_banks);
         UInt32 dram_page_size_log2 = floorLog2(dram_page_size);
         UInt32 column_hi_offset = dram_page_size_log2 - column_offset + num_banks_log2;
         UInt32 bank_offset = dram_page_size_log2 - column_offset;

         mapping = std::make_unique<DramAddressMappingOpenPageSplitColumn>(
            home_lookup, num_banks, num_bank_groups, dram_page_size,
            column_offset, column_hi_offset, bank_offset, num_banks_log2);
      }
      else if (mapping_type == "closed_page")
      {
         UInt32 column_bits_shift = Sim()->getCfg()->getInt("perf_model/dram/ddr/closed_page/column_bits_shift");

         mapping = std::make_unique<DramAddressMappingClosedPage>(
            home_lookup, num_banks, num_bank_groups, dram_page_size, column_bits_shift);
      }
      else if (mapping_type == "xor")
      {
         // XOR mapping uses open_page as base and applies XOR scrambling
         UInt32 channel_bits = Sim()->getCfg()->getInt("perf_model/dram/ddr/xor/channel_bits");
         UInt32 rank_bits = Sim()->getCfg()->getInt("perf_model/dram/ddr/xor/rank_bits");
         UInt32 bank_bits = Sim()->getCfg()->getInt("perf_model/dram/ddr/xor/bank_bits");
         UInt32 page_size_bits = Sim()->getCfg()->getInt("perf_model/dram/ddr/xor/page_size_bits");
         UInt32 xor_offset = Sim()->getCfg()->getInt("perf_model/dram/ddr/xor/xor_offset");

         auto base_mapping = std::make_unique<DramAddressMappingOpenPage>(
            home_lookup, num_channels, num_ranks, num_banks, num_bank_groups,
            dram_page_size, channel_bits, rank_bits, bank_bits, page_size_bits);

         mapping = std::make_unique<DramAddressMappingXor>(
            std::move(base_mapping), num_bank_groups, banks_per_group,
            num_ranks, num_channels, xor_offset);
      }
      else
      {
         LOG_PRINT_ERROR("Unknown DRAM address mapping type: %s. "
                         "Valid options: open_page, open_page_split_column, closed_page, xor",
                         mapping_type.c_str());
      }

      return mapping;
   }
};

#endif /* __DRAM_ADDRESS_MAPPING_FACTORY_H__ */
