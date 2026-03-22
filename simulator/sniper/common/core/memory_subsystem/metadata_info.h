#ifndef __METADATA_INFO_H__
#define __METADATA_INFO_H__

#include "fixed_types.h"

/**
 * @brief Structure to hold metadata properties for PTW tracking
 * 
 * This structure is used to propagate page table walk information
 * through the memory hierarchy, from MMU to DRAM.
 */
struct MetadataInfo
{
   bool is_metadata;        // True if this is a metadata access (page table, TLB, etc.)
   UInt32 ptw_depth;        // Total depth of the page table walk (e.g., 4 for 4-level paging)
   UInt32 ptw_level;        // Current level being accessed (0 = PML4, 1 = PDPT, 2 = PD, 3 = PT)
   UInt32 table_id;         // Which table (for multi-table walks, e.g., different page sizes)
   UInt64 ptw_id;           // Unique identifier for this PTW (for correlation)
   bool is_pte;             // True if this is the final PTE access that completes the walk
   
   // Data access tracking (set when data access follows a PTW)
   bool is_data_after_ptw;  // True if this is a data access that followed a PTW
   bool ptw_hit_dram;       // True if the preceding PTW accessed DRAM (not just caches)
   UInt32 ptw_dram_accesses; // Number of DRAM accesses during the PTW
   IntPtr data_va;          // Virtual address of the data being accessed
   IntPtr data_pa;          // Physical address of the data being accessed
   
   // Default constructor - regular data access
   MetadataInfo()
      : is_metadata(false)
      , ptw_depth(0)
      , ptw_level(0)
      , table_id(0)
      , ptw_id(0)
      , is_pte(false)
      , is_data_after_ptw(false)
      , ptw_hit_dram(false)
      , ptw_dram_accesses(0)
      , data_va(0)
      , data_pa(0)
   {}
   
   // Constructor for metadata access
   MetadataInfo(UInt32 depth, UInt32 level, UInt32 table, UInt64 id, bool pte)
      : is_metadata(true)
      , ptw_depth(depth)
      , ptw_level(level)
      , table_id(table)
      , ptw_id(id)
      , is_pte(pte)
      , is_data_after_ptw(false)
      , ptw_hit_dram(false)
      , ptw_dram_accesses(0)
      , data_va(0)
      , data_pa(0)
   {}
   
   // Simple constructor for backward compatibility (just is_metadata flag)
   explicit MetadataInfo(bool metadata)
      : is_metadata(metadata)
      , ptw_depth(0)
      , ptw_level(0)
      , table_id(0)
      , ptw_id(0)
      , is_pte(false)
      , is_data_after_ptw(false)
      , ptw_hit_dram(false)
      , ptw_dram_accesses(0)
      , data_va(0)
      , data_pa(0)
   {}
   
   // Constructor for data access after PTW
   static MetadataInfo dataAfterPtw(UInt64 ptw_id, IntPtr va, IntPtr pa, bool hit_dram, UInt32 dram_accesses) {
      MetadataInfo info;
      info.is_metadata = false;
      info.is_data_after_ptw = true;
      info.ptw_id = ptw_id;
      info.ptw_hit_dram = hit_dram;
      info.ptw_dram_accesses = dram_accesses;
      info.data_va = va;
      info.data_pa = pa;
      return info;
   }
};

/**
 * @brief Per-core context for passing metadata info through the memory hierarchy
 * 
 * This provides per-core storage that can be accessed from any thread,
 * allowing the MMU (user thread) to set context that DRAM (sim thread) can read.
 * 
 * Usage:
 *   - MMU sets context: MetadataContext::set(core_id, info)
 *   - DRAM reads context: MetadataContext::get(requester_core_id)
 *   - MMU clears context: MetadataContext::clear(core_id)
 */
class MetadataContext
{
private:
   static const UInt32 MAX_CORES = 128;  // Maximum supported cores
   static MetadataInfo s_core_info[MAX_CORES];
   static bool s_core_info_valid[MAX_CORES];
   
public:
   // Initialize the context storage (call once at startup)
   static void init() {
      for (UInt32 i = 0; i < MAX_CORES; ++i) {
         s_core_info[i] = MetadataInfo();
         s_core_info_valid[i] = false;
      }
   }
   
   // Set the context for a specific core before initiating a memory access
   static void set(core_id_t core_id, const MetadataInfo& info) {
      if (core_id >= 0 && static_cast<UInt32>(core_id) < MAX_CORES) {
         s_core_info[core_id] = info;
         s_core_info_valid[core_id] = true;
      }
   }
   
   // Get the context for a specific core (returns default MetadataInfo if not set)
   static const MetadataInfo& get(core_id_t core_id) {
      static MetadataInfo default_info;
      if (core_id >= 0 && static_cast<UInt32>(core_id) < MAX_CORES) {
         return s_core_info[core_id];
      }
      return default_info;
   }
   
   // Check if context is valid for a specific core
   static bool isValid(core_id_t core_id) {
      if (core_id >= 0 && static_cast<UInt32>(core_id) < MAX_CORES) {
         return s_core_info_valid[core_id];
      }
      return false;
   }
   
   // Clear the context for a specific core after the memory access completes
   static void clear(core_id_t core_id) {
      if (core_id >= 0 && static_cast<UInt32>(core_id) < MAX_CORES) {
         s_core_info_valid[core_id] = false;
         s_core_info[core_id] = MetadataInfo();
      }
   }
};

#endif /* __METADATA_INFO_H__ */
