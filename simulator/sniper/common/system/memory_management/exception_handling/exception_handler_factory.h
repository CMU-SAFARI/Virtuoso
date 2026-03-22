#ifndef EXCEPTION_HANDLER_FACTORY_H
#define EXCEPTION_HANDLER_FACTORY_H

#include "misc/exception_handler_base.h"
#include "sniper_space_exception_handler.h"
#include "user_space_exception_handler.h"
#include "eager_paging_exception_handler.h"
#include "spot_exception_handler.h"
#include "utopia_exception_handler.h"
#include "simulator.h"
#include "config.hpp"

#include <string>
#include <iostream>

/**
 * @brief Factory class for creating exception handlers
 * 
 * This factory creates the appropriate exception handler based on:
 * 1. Whether userspace MimicOS is enabled (VirtuosExceptionHandler)
 * 2. The configured exception handler type for sniper-space mode
 */
class ExceptionHandlerFactory {
public:
    /**
     * @brief Create an exception handler for the given core
     * 
     * @param core The core that will own this exception handler
     * @return ExceptionHandlerBase* The created exception handler
     */
    static ExceptionHandlerBase* createExceptionHandler(Core* core) {
        bool userspace_mimicos_enabled = Sim()->getCfg()->getBool("general/enable_userspace_mimicos");
        
        if (userspace_mimicos_enabled) {
            std::cout << "[EXCEPTION_HANDLER_FACTORY] Creating VirtuosExceptionHandler for core " << core->getId() << std::endl;
            return new VirtuosExceptionHandler(core);
        }
        
        // Sniper-space mode: check which handler type is configured
        String handler_type = "default";
        
        // Try to read the exception handler type from config
        // Default to "default" if not specified
        try {
            handler_type = Sim()->getCfg()->getString("general/exception_handler_type");
        } catch (...) {
            // Config key doesn't exist, use default
            handler_type = "default";
        }
        
        return createSniperSpaceHandler(core, handler_type);
    }

private:
    /**
     * @brief Create a sniper-space exception handler of the specified type
     * 
     * @param core The core that will own this exception handler
     * @param handler_type The type of handler to create
     * @return ExceptionHandlerBase* The created exception handler
     */
    static ExceptionHandlerBase* createSniperSpaceHandler(Core* core, const String& handler_type) {
        std::cout << "[EXCEPTION_HANDLER_FACTORY] Creating sniper-space handler of type: " << handler_type << " for core " << core->getId() << std::endl;
        
        if (handler_type == "default" || handler_type == "sniper") {
            return new SniperExceptionHandler(core);
        }
        else if (handler_type == "eager_paging") {
            return new EagerPagingExceptionHandler(core);
        }
        else if (handler_type == "spot") {
            return new SpotExceptionHandler(core);
        }
        else if (handler_type == "utopia") {
            return new UtopiaExceptionHandler(core);
        }
        else {
            std::cerr << "[EXCEPTION_HANDLER_FACTORY] Unknown handler type: " << handler_type << ", using default" << std::endl;
            return new SniperExceptionHandler(core);
        }
    }
};

#endif // EXCEPTION_HANDLER_FACTORY_H
