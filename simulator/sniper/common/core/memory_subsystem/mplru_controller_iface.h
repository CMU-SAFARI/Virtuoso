/**
 * @file mplru_controller_iface.h
 * @brief Interface for MPLRU controllers (Delta and Bandit implementations)
 * 
 * This interface defines the contract for MPLRU controllers that determine
 * the metadata priority level (meta_level) for cache replacement.
 * 
 * Two implementations:
 *   1. MPLRUDeltaController: Original Δ-based push-then-backoff policy
 *   2. MPLRUBanditController: IPC-only multi-armed bandit with probing
 * 
 * The factory pattern allows runtime selection via config:
 *   perf_model/nuca/mplru/controller/type = "delta" | "bandit"
 */

#ifndef MPLRU_CONTROLLER_IFACE_H
#define MPLRU_CONTROLLER_IFACE_H

#include "fixed_types.h"
#include <string>

/**
 * @brief Abstract interface for MPLRU controllers
 * 
 * All controller implementations must implement this interface.
 * The replacement policy (CacheSetMPLRU) interacts with controllers
 * exclusively through this interface.
 */
class IMPLRUController
{
public:
   virtual ~IMPLRUController() = default;
   
   /**
    * @brief Initialize the controller for a given number of cores
    * @param num_cores Number of cores in the system
    */
   virtual void initialize(UInt32 num_cores) = 0;
   
   /**
    * @brief Clean up the controller and release resources
    */
   virtual void cleanup() = 0;
   
   /**
    * @brief Load configuration for a core
    * @param core_id Core ID
    * @param cfgname Config path prefix (e.g., "perf_model/nuca")
    */
   virtual void loadConfig(core_id_t core_id, const String& cfgname) = 0;
   
   /**
    * @brief Try to process epoch if enough instructions have passed
    * @param core_id Core ID
    * 
    * Called by the replacement policy on each cache access.
    * Internally checks if it's time for a new epoch and triggers
    * the control algorithm if needed.
    */
   virtual void tryProcessEpoch(core_id_t core_id) = 0;
   
   /**
    * @brief Get the current policy_id for a core
    * @param core_id Core ID
    * @return Current policy ID [0..11]
    * 
    * Metadata protection arms (M0-M5):
    *   0 = M0: OFF (vanilla LRU)
    *   1 = M1: 25% bias (evict data if in bottom 25% LRU)
    *   2 = M2: 50% bias (evict data if in bottom 50% LRU)
    *   3 = M3: hard bias (always evict data first)
    *   4 = M4: 25% partition (reserve 25% ways for metadata)
    *   5 = M5: 50% partition (reserve 50% ways for metadata)
    * 
    * Data protection arms (D0-D5):
    *   6 = D0: OFF (vanilla LRU, same as M0)
    *   7 = D1: 25% bias (evict metadata if in bottom 25% LRU)
    *   8 = D2: 50% bias (evict metadata if in bottom 50% LRU)
    *   9 = D3: hard bias (always evict metadata first)
    *   10 = D4: 25% partition (reserve 25% ways for data)
    *   11 = D5: 50% partition (reserve 50% ways for data)
    */
   virtual int getMetaLevel(core_id_t core_id) const = 0;
   
   /**
    * @brief Check if translation-priority is engaged for a core
    * @param core_id Core ID
    * @return true if controller is actively prioritizing metadata
    */
   virtual bool isEngaged(core_id_t core_id) const = 0;
   
   /**
    * @brief Check if the controller is initialized
    * @return true if initialized and ready
    */
   virtual bool isInitialized() const = 0;
   
   /**
    * @brief Get controller type name for logging
    * @return "delta" or "bandit"
    */
   virtual const char* getTypeName() const = 0;
};

#endif // MPLRU_CONTROLLER_IFACE_H
