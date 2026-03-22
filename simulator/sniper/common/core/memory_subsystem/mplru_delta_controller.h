/**
 * @file mplru_delta_controller.h
 * @brief Delta-based MPLRU controller (wraps existing MPLRUController)
 * 
 * This controller implements the original Δ-based push-then-backoff policy.
 * It wraps the existing static MPLRUController with the IMPLRUController interface.
 * 
 * Key features:
 * 1. Engage gate with hysteresis
 * 2. Exploration phase
 * 3. Δ-controller (marginal benefit test)
 * 4. Absolute guardrail
 * 5. Net benefit test
 * 
 * This is the DEFAULT controller type.
 */

#ifndef MPLRU_DELTA_CONTROLLER_H
#define MPLRU_DELTA_CONTROLLER_H

#include "mplru_controller_iface.h"
#include "mplru_controller_impl.h"

/**
 * @brief Wrapper for existing MPLRUController implementing the interface
 * 
 * This class delegates all calls to the static MPLRUController.
 * It provides backward compatibility - the existing delta controller
 * logic remains unchanged.
 */
class MPLRUDeltaController : public IMPLRUController
{
public:
   MPLRUDeltaController() = default;
   virtual ~MPLRUDeltaController() = default;
   
   void initialize(UInt32 num_cores) override
   {
      MPLRUController::initialize(num_cores);
   }
   
   void cleanup() override
   {
      MPLRUController::cleanup();
   }
   
   void loadConfig(core_id_t core_id, const String& cfgname) override
   {
      MPLRUController::loadConfig(core_id, cfgname);
   }
   
   void tryProcessEpoch(core_id_t core_id) override
   {
      MPLRUController::tryProcessEpoch(core_id);
   }
   
   int getMetaLevel(core_id_t core_id) const override
   {
      return MPLRUController::getMetaLevel(core_id);
   }
   
   bool isEngaged(core_id_t core_id) const override
   {
      return MPLRUController::isEngaged(core_id);
   }
   
   bool isInitialized() const override
   {
      return MPLRUController::isInitialized();
   }
   
   const char* getTypeName() const override
   {
      return "delta";
   }
};

#endif // MPLRU_DELTA_CONTROLLER_H
