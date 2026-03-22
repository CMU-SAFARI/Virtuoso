/**
 * @file mplru_controller_factory.h
 * @brief Factory for creating MPLRU controllers based on config
 * 
 * The factory reads the controller type from config and creates
 * the appropriate implementation:
 *   - "delta" → MPLRUDeltaController (default)
 *   - "bandit" → MPLRUBanditController
 */

#ifndef MPLRU_CONTROLLER_FACTORY_H
#define MPLRU_CONTROLLER_FACTORY_H

#include "mplru_controller_iface.h"
#include "fixed_types.h"
#include <memory>

/**
 * @brief Factory for MPLRU controller creation
 * 
 * This class provides a singleton pattern for managing the global
 * controller instance. The controller type is determined at first
 * initialization based on config.
 */
class MPLRUControllerFactory
{
public:
   /**
    * @brief Controller type enumeration
    */
   enum class ControllerType {
      DELTA,   // Original Δ-based controller (default)
      BANDIT   // IPC-only bandit controller
   };
   
   /**
    * @brief Initialize the controller based on config
    * @param num_cores Number of cores in the system
    * @param cfgname Config path prefix (e.g., "perf_model/nuca")
    * 
    * Reads "mplru/controller/type" from config:
    *   - "delta" (default) → MPLRUDeltaController
    *   - "bandit" → MPLRUBanditController
    */
   static void initialize(UInt32 num_cores, const String& cfgname);
   
   /**
    * @brief Clean up and release the controller
    */
   static void cleanup();
   
   /**
    * @brief Get the controller instance
    * @return Pointer to the controller (nullptr if not initialized)
    */
   static IMPLRUController* getController();
   
   /**
    * @brief Check if the factory has been initialized
    */
   static bool isInitialized();
   
   /**
    * @brief Get the current controller type
    */
   static ControllerType getControllerType();
   
   /**
    * @brief Parse controller type from string
    * @param type_str "delta" or "bandit"
    * @return ControllerType enum value (defaults to DELTA)
    */
   static ControllerType parseType(const String& type_str);

private:
   static IMPLRUController* s_controller;
   static ControllerType s_type;
   static bool s_initialized;
};

#endif // MPLRU_CONTROLLER_FACTORY_H
