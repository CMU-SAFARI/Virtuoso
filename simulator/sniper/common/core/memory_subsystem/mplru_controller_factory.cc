/**
 * @file mplru_controller_factory.cc
 * @brief Implementation of MPLRU controller factory
 */

#include "mplru_controller_factory.h"
#include "mplru_delta_controller.h"
#include "mplru_bandit_controller.h"
#include "simulator.h"
#include "config.hpp"
#include <iostream>

// Static member initialization
IMPLRUController* MPLRUControllerFactory::s_controller = nullptr;
MPLRUControllerFactory::ControllerType MPLRUControllerFactory::s_type = ControllerType::DELTA;
bool MPLRUControllerFactory::s_initialized = false;

MPLRUControllerFactory::ControllerType 
MPLRUControllerFactory::parseType(const String& type_str)
{
   if (type_str == "bandit") {
      return ControllerType::BANDIT;
   }
   // Default to delta for any other value (including "delta", empty, etc.)
   return ControllerType::DELTA;
}

void MPLRUControllerFactory::initialize(UInt32 num_cores, const String& cfgname)
{
   if (s_initialized) {
      return;
   }
   
   // Read controller type from config
   // Default is "delta" to preserve existing behavior
   String type_str = "delta";
   try {
      type_str = Sim()->getCfg()->getString(cfgname + "/mplru/controller/type");
   } catch (...) {
      // Config key not found, use default
      type_str = "delta";
   }
   
   s_type = parseType(type_str);
   
   // Create the appropriate controller
   switch (s_type) {
      case ControllerType::BANDIT:
         s_controller = new MPLRUBanditController();
         break;
      case ControllerType::DELTA:
      default:
         s_controller = new MPLRUDeltaController();
         break;
   }
   
   // Initialize the controller
   s_controller->initialize(num_cores);
   
   s_initialized = true;
   
   std::cerr << "[MPLRUControllerFactory] Created controller type='" 
             << s_controller->getTypeName() 
             << "' for " << num_cores << " cores" << std::endl;
}

void MPLRUControllerFactory::cleanup()
{
   if (!s_initialized) {
      return;
   }
   
   if (s_controller) {
      s_controller->cleanup();
      delete s_controller;
      s_controller = nullptr;
   }
   
   s_initialized = false;
}

IMPLRUController* MPLRUControllerFactory::getController()
{
   return s_controller;
}

bool MPLRUControllerFactory::isInitialized()
{
   return s_initialized;
}

MPLRUControllerFactory::ControllerType MPLRUControllerFactory::getControllerType()
{
   return s_type;
}
