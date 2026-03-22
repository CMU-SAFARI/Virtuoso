#ifndef __DRAM_ADDRESS_MAPPING_CLOSED_PAGE_H__
#define __DRAM_ADDRESS_MAPPING_CLOSED_PAGE_H__

#include "dram_address_mapping.h"

/**
 * @brief Closed-page address mapping
 * 
 * Closed-page mapping optimizes for bank-level parallelism:
 *   Bank bits at bottom, column interleaved with page
 * 
 * Sequential accesses are distributed across banks rather than
 * hitting the same row buffer.
 */
class DramAddressMappingClosedPage : public DramAddressMapping
{
public:
   DramAddressMappingClosedPage(
      const AddressHomeLookup* home_lookup,
      UInt32 num_banks,
      UInt32 num_bank_groups,
      UInt32 dram_page_size,
      UInt32 column_bits_shift)
      : m_home_lookup(home_lookup)
      , m_num_banks(num_banks)
      , m_num_bank_groups(num_bank_groups)
      , m_dram_page_size(dram_page_size)
      , m_column_bits_shift(column_bits_shift)
   {}

   DramAddress parse(IntPtr address) const override
   {
      DramAddress result;
      result.channel = 0;
      result.rank = 0;

      UInt64 linear = m_home_lookup->getLinearAddress(address);
      UInt64 bits = linear >> 6;

      // Closed-page: bank bits at bottom, column interleaved with page
      result.bank_group = bits % m_num_bank_groups;
      result.bank = bits % m_num_banks;
      bits /= m_num_banks;

      result.column = (bits >> m_column_bits_shift) % m_dram_page_size;
      result.page = (((bits >> m_column_bits_shift) / m_dram_page_size) << m_column_bits_shift)
                  | (bits & ((1 << m_column_bits_shift) - 1));

      return result;
   }

   const char* getName() const override { return "ClosedPage"; }

private:
   const AddressHomeLookup* m_home_lookup;
   UInt32 m_num_banks;
   UInt32 m_num_bank_groups;
   UInt32 m_dram_page_size;
   UInt32 m_column_bits_shift;
};

#endif /* __DRAM_ADDRESS_MAPPING_CLOSED_PAGE_H__ */
