#ifndef __DRAM_ADDRESS_MAPPING_H__
#define __DRAM_ADDRESS_MAPPING_H__

#include "fixed_types.h"
#include "address_home_lookup.h"
#include "utils.h"

/**
 * @brief DRAM address components after parsing
 */
struct DramAddress
{
   UInt32 channel;
   UInt32 rank;
   UInt32 bank_group;
   UInt32 bank;
   UInt32 column;
   UInt64 page;
};

/**
 * @brief Abstract base class for DRAM address mapping strategies
 * 
 * Defines the interface for parsing physical addresses into DRAM components
 * (channel, rank, bank group, bank, column, page/row).
 */
class DramAddressMapping
{
public:
   virtual ~DramAddressMapping() = default;

   /**
    * @brief Parse a physical address into DRAM components
    * @param address Physical memory address
    * @return DramAddress struct with all components
    */
   virtual DramAddress parse(IntPtr address) const = 0;

   /**
    * @brief Get a descriptive name for this mapping scheme
    */
   virtual const char* getName() const = 0;
};

#endif /* __DRAM_ADDRESS_MAPPING_H__ */
