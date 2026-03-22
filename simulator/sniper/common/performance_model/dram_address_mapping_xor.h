#ifndef __DRAM_ADDRESS_MAPPING_XOR_H__
#define __DRAM_ADDRESS_MAPPING_XOR_H__

#include "dram_address_mapping.h"
#include <memory>

/**
 * @brief XOR-based address mapping decorator (MOP4CLXOR-style)
 * 
 * Wraps another mapping and applies XOR scrambling to spread accesses
 * across banks/ranks/channels even with contiguous physical addresses
 * (e.g., 2MB huge pages).
 * 
 * This is implemented as a decorator pattern - it wraps any base mapping
 * and adds XOR randomization on top.
 */
class DramAddressMappingXor : public DramAddressMapping
{
public:
   /**
    * @brief Construct XOR mapping decorator
    * @param base_mapping     The underlying mapping to wrap (takes ownership)
    * @param num_bank_groups  Number of bank groups
    * @param banks_per_group  Number of banks per bank group
    * @param num_ranks        Number of ranks
    * @param num_channels     Number of channels
    * @param xor_offset       Bit offset in page number for XOR bits
    */
   DramAddressMappingXor(
      std::unique_ptr<DramAddressMapping> base_mapping,
      UInt32 num_bank_groups,
      UInt32 banks_per_group,
      UInt32 num_ranks,
      UInt32 num_channels,
      UInt32 xor_offset)
      : m_base(std::move(base_mapping))
      , m_num_bank_groups(num_bank_groups)
      , m_banks_per_group(banks_per_group)
      , m_num_ranks(num_ranks)
      , m_num_channels(num_channels)
      , m_xor_offset(xor_offset)
      , m_bg_bits(floorLog2(num_bank_groups))
      , m_bank_bits(floorLog2(banks_per_group))
      , m_rank_bits(floorLog2(num_ranks))
   {}

   DramAddress parse(IntPtr address) const override
   {
      // First get the base mapping result
      DramAddress result = m_base->parse(address);

      // Apply XOR scrambling using higher page bits
      UInt32 xor_bits = static_cast<UInt32>(result.page >> m_xor_offset);

      // XOR bank_group
      result.bank_group ^= xor_bits & (m_num_bank_groups - 1);

      // XOR bank-within-group
      UInt32 bank_in_group = result.bank / m_num_bank_groups;
      bank_in_group ^= (xor_bits >> m_bg_bits) & (m_banks_per_group - 1);
      result.bank = m_banks_per_group * result.bank_group + bank_in_group;

      // XOR rank (if multiple ranks)
      if (m_num_ranks > 1)
         result.rank ^= (xor_bits >> (m_bg_bits + m_bank_bits)) & (m_num_ranks - 1);

      // XOR channel (if multiple channels)
      if (m_num_channels > 1)
         result.channel ^= (xor_bits >> (m_bg_bits + m_bank_bits + m_rank_bits)) & (m_num_channels - 1);

      return result;
   }

   const char* getName() const override { return "XOR"; }

private:
   std::unique_ptr<DramAddressMapping> m_base;
   UInt32 m_num_bank_groups;
   UInt32 m_banks_per_group;
   UInt32 m_num_ranks;
   UInt32 m_num_channels;
   UInt32 m_xor_offset;
   UInt32 m_bg_bits;
   UInt32 m_bank_bits;
   UInt32 m_rank_bits;
};

#endif /* __DRAM_ADDRESS_MAPPING_XOR_H__ */
