#ifndef __DRAM_ADDRESS_MAPPING_OPEN_PAGE_H__
#define __DRAM_ADDRESS_MAPPING_OPEN_PAGE_H__

#include "dram_address_mapping.h"

/**
 * @brief Open-page address mapping
 * 
 * Standard open-page mapping optimizes for row buffer locality:
 *   | Row/Page | Bank | Rank | Column | Channel | (cache line offset removed)
 * 
 * Sequential accesses within a DRAM page (row) hit in the row buffer.
 */
class DramAddressMappingOpenPage : public DramAddressMapping
{
public:
   DramAddressMappingOpenPage(
      const AddressHomeLookup* home_lookup,
      UInt32 num_channels,
      UInt32 num_ranks,
      UInt32 num_banks,
      UInt32 num_bank_groups,
      UInt32 dram_page_size,
      UInt32 channel_bits,
      UInt32 rank_bits,
      UInt32 bank_bits,
      UInt32 page_size_bits)
      : m_home_lookup(home_lookup)
      , m_num_channels(num_channels)
      , m_num_ranks(num_ranks)
      , m_num_banks(num_banks)
      , m_num_bank_groups(num_bank_groups)
      , m_dram_page_size(dram_page_size)
      , m_channel_bits(channel_bits)
      , m_rank_bits(rank_bits)
      , m_bank_bits(bank_bits)
      , m_page_size_bits(page_size_bits)
   {}

   DramAddress parse(IntPtr address) const override
   {
      DramAddress result;
      
      UInt64 linear = m_home_lookup->getLinearAddress(address);
      UInt64 bits = linear >> 6;  // Remove cache line offset (64 bytes)

      // | Page | Bank | Rank | Column | Channel |
      result.channel = bits % m_num_channels;
      bits >>= m_channel_bits;

      result.column = bits % m_dram_page_size;
      bits >>= m_page_size_bits;

      result.rank = bits % m_num_ranks;
      bits >>= m_rank_bits;

      result.bank_group = bits % m_num_bank_groups;
      result.bank = bits % m_num_banks;
      bits >>= m_bank_bits;

      result.page = bits;

      return result;
   }

   const char* getName() const override { return "OpenPage"; }

private:
   const AddressHomeLookup* m_home_lookup;
   UInt32 m_num_channels;
   UInt32 m_num_ranks;
   UInt32 m_num_banks;
   UInt32 m_num_bank_groups;
   UInt32 m_dram_page_size;
   UInt32 m_channel_bits;
   UInt32 m_rank_bits;
   UInt32 m_bank_bits;
   UInt32 m_page_size_bits;
};

/**
 * @brief Open-page mapping with split column address
 * 
 * Split column address mode reduces row buffer conflicts:
 *   | Page | ColHi | Bank | ColLo |
 */
class DramAddressMappingOpenPageSplitColumn : public DramAddressMapping
{
public:
   DramAddressMappingOpenPageSplitColumn(
      const AddressHomeLookup* home_lookup,
      UInt32 num_banks,
      UInt32 num_bank_groups,
      UInt32 dram_page_size,
      UInt32 column_offset,
      UInt32 column_hi_offset,
      UInt32 bank_offset,
      UInt32 num_banks_log2)
      : m_home_lookup(home_lookup)
      , m_num_banks(num_banks)
      , m_num_bank_groups(num_bank_groups)
      , m_dram_page_size(dram_page_size)
      , m_column_offset(column_offset)
      , m_column_hi_offset(column_hi_offset)
      , m_bank_offset(bank_offset)
      , m_num_banks_log2(num_banks_log2)
   {}

   DramAddress parse(IntPtr address) const override
   {
      DramAddress result;
      result.channel = 0;
      result.rank = 0;

      UInt64 linear = m_home_lookup->getLinearAddress(address);
      UInt64 bits = linear >> 6;

      // Split column: | Page | ColHi | Bank | ColLo |
      result.column = (((bits >> m_column_hi_offset) << m_bank_offset)
                      | (bits & ((1 << m_bank_offset) - 1))) % m_dram_page_size;
      bits >>= m_bank_offset;

      result.bank_group = bits % m_num_bank_groups;
      result.bank = bits % m_num_banks;
      bits >>= (m_num_banks_log2 + m_column_offset);

      result.page = bits;

      return result;
   }

   const char* getName() const override { return "OpenPageSplitColumn"; }

private:
   const AddressHomeLookup* m_home_lookup;
   UInt32 m_num_banks;
   UInt32 m_num_bank_groups;
   UInt32 m_dram_page_size;
   UInt32 m_column_offset;
   UInt32 m_column_hi_offset;
   UInt32 m_bank_offset;
   UInt32 m_num_banks_log2;
};

#endif /* __DRAM_ADDRESS_MAPPING_OPEN_PAGE_H__ */
