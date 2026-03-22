#include "simulator.h"
#include "dram_perf_model.h"
#include "dram_perf_model_constant.h"
#include "dram_perf_model_readwrite.h"
#include "dram_perf_model_normal.h"
#include "dram_perf_model_detailed.h"
#include "dram_perf_model_cxl.h"
#include "config.hpp"

// Factory with explicit type parameter (for tiered memory)
DramPerfModel* DramPerfModel::createDramPerfModel(core_id_t core_id, UInt32 cache_block_size,
                                                   AddressHomeLookup* address_home_lookup,
                                                   const String& type,
                                                   const String& suffix)
{
   if (type == "constant")
   {
      return new DramPerfModelConstant(core_id, cache_block_size, suffix);
   }
   else if (type == "readwrite")
   {
      return new DramPerfModelReadWrite(core_id, cache_block_size, suffix);
   }
   else if (type == "normal")
   {
      return new DramPerfModelNormal(core_id, cache_block_size, suffix);
   }
   else if (type == "ddr")
   {
      return new DramPerfModelDetailed(core_id, cache_block_size, address_home_lookup, suffix);
   }
   else if (type == "cxl")
   {
      return new DramPerfModelCXL(core_id, cache_block_size, address_home_lookup, suffix);
   }
   else
   {
      LOG_PRINT_ERROR("Invalid DRAM model type %s", type.c_str());
      return nullptr;
   }
}

// Factory using config (original behavior)
DramPerfModel* DramPerfModel::createDramPerfModel(core_id_t core_id, UInt32 cache_block_size,
                                                   AddressHomeLookup* address_home_lookup)
{
   String type = Sim()->getCfg()->getString("perf_model/dram/type");
   return createDramPerfModel(core_id, cache_block_size, address_home_lookup, type);
}
