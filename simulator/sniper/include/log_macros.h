/**
 * @file log_macros.h
 * @brief Simple logging macros for consistent output formatting
 * 
 * Drop-in replacement macros for existing log statements.
 * Provides consistent formatting without requiring major code changes.
 * 
 * Usage:
 *   #include "log_macros.h"
 *   
 *   // Instead of:
 *   //   log_file << "[MMU] Something happened" << std::endl;
 *   // Use:
 *   //   LOG_WRITE(log_file, "MMU", "Something happened");
 */

#pragma once

#include <iomanip>
#include <sstream>

// Format: [COMPONENT|CoreN] message
// Example: [MMU        |C0] Page fault at 0x7fff1234

#define LOG_COMPONENT_WIDTH 12

// Basic log write with component name
#define LOG_WRITE(stream, component, msg) \
    stream << "[" << std::setw(LOG_COMPONENT_WIDTH) << std::left << component << "] " << msg << std::endl

// Log with core ID
#define LOG_WRITE_CORE(stream, component, core_id, msg) \
    stream << "[" << std::setw(LOG_COMPONENT_WIDTH) << std::left << component << "|C" << core_id << "] " << msg << std::endl

// Log with simulation time
#define LOG_WRITE_TIME(stream, component, time_ns, msg) \
    stream << "[" << std::setw(LOG_COMPONENT_WIDTH) << std::left << component << "] @" << std::setw(12) << time_ns << "ns " << msg << std::endl

// Log with core ID and simulation time
#define LOG_WRITE_FULL(stream, component, core_id, time_ns, msg) \
    stream << "[" << std::setw(LOG_COMPONENT_WIDTH) << std::left << component << "|C" << core_id << "] @" << std::setw(12) << time_ns << "ns " << msg << std::endl

// Log an address in hex format
#define LOG_ADDR(addr) "0x" << std::hex << addr << std::dec

// Log a key-value pair
#define LOG_KV(key, val) key << "=" << val

// Section separator
#define LOG_SECTION(stream, title) \
    stream << "\n" << std::string(60, '-') << "\n  " << title << "\n" << std::string(60, '-') << std::endl

// Formatted init message
#define LOG_INIT(stream, component, core_id) \
    stream << "[" << std::setw(LOG_COMPONENT_WIDTH) << std::left << component << "|C" << core_id << "] Initialized" << std::endl
