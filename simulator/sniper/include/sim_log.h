/**
 * @kanellok 
 * @file sim_log.h
 * @brief Centralized logging utility for Sniper simulator
 * 
 * Provides consistent, structured logging across all simulator components.
 * Supports debug levels (NONE, BASIC, DETAILED) via debug_config.h.
 * 
 * Unified Logging:
 *   When UNIFIED_LOG_ENABLED is set to 1 in debug_config.h, all components
 *   also write to a single unified log file (unified_debug.log) for timing
 *   analysis across components.
 * 
 * Usage:
 *   #include "sim_log.h"
 *   
 *   // In class constructor:
 *   SimLog log("MMU", core_id);
 *   
 *   // Logging:
 *   log.info("Page fault at address", address);
 *   log.debug("Cache hit", hit_where);
 *   log.trace("Detailed info", val1, val2);  // Only if DEBUG_DETAILED
 */

#pragma once

#include <fstream>
#include <sstream>
#include <string>
#include <iomanip>
#include <iostream>
#include <mutex>
#include "simulator.h"
#include "config.hpp"
#include "debug_config.h"

/**
 * @brief Singleton class for unified logging across all components
 * 
 * When UNIFIED_LOG_ENABLED is 1, all SimLog instances also write to this
 * unified log file, allowing timing analysis across components.
 */
class UnifiedLog {
private:
    std::ofstream m_file;
    std::mutex m_mutex;
    bool m_initialized;
    bool m_enabled;
    
    UnifiedLog() : m_initialized(false), m_enabled(false) {}
    
public:
    static UnifiedLog& instance() {
        static UnifiedLog inst;
        return inst;
    }
    
    // Initialize the unified log (call once after Sim() is available)
    void init() {
        if (m_initialized) return;
        m_initialized = true;
        
#if UNIFIED_LOG_ENABLED
        m_enabled = true;
        std::string path = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) 
                          + "/" + UNIFIED_LOG_FILENAME;
        m_file.open(path);
        if (m_file.is_open()) {
            m_file << "=== UNIFIED DEBUG LOG ===" << std::endl;
            m_file << "All component logs in timing order" << std::endl;
            m_file << std::string(60, '=') << std::endl << std::endl;
        }
#endif
    }
    
    bool isEnabled() const { return m_enabled && m_file.is_open(); }
    
    // Thread-safe write to unified log
    void write(const std::string& component, int core_id, const std::string& message) {
        if (!isEnabled()) return;
        
        std::lock_guard<std::mutex> lock(m_mutex);
        
        // Format: [COMPONENT|Cn] message
        if (core_id >= 0) {
            m_file << "[" << std::setw(12) << std::left << component 
                   << "|C" << core_id << "] " << message << std::endl;
        } else {
            m_file << "[" << std::setw(15) << std::left << component << "] " 
                   << message << std::endl;
        }
    }
    
    // Write with simulation time
    void writeWithTime(const std::string& component, int core_id, uint64_t sim_time_ns, 
                       const std::string& message) {
        if (!isEnabled()) return;
        
        std::lock_guard<std::mutex> lock(m_mutex);
        
        std::ostringstream prefix;
        prefix << "@" << std::setw(12) << sim_time_ns << "ns ";
        if (core_id >= 0) {
            prefix << "[" << std::setw(12) << std::left << component 
                   << "|C" << core_id << "] ";
        } else {
            prefix << "[" << std::setw(15) << std::left << component << "] ";
        }
        m_file << prefix.str() << message << std::endl;
    }
    
    // Flush the unified log
    void flush() {
        if (isEnabled()) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_file.flush();
        }
    }
    
    ~UnifiedLog() {
        if (m_file.is_open()) {
            m_file.close();
        }
    }
    
    // Prevent copying
    UnifiedLog(const UnifiedLog&) = delete;
    UnifiedLog& operator=(const UnifiedLog&) = delete;
};

class SimLog {
public:
    enum Level {
        LEVEL_NONE = 0,
        LEVEL_INFO = 1,     // Always shown (init, important events)
        LEVEL_DEBUG = 2,    // Basic debugging
        LEVEL_TRACE = 3     // Detailed tracing
    };

private:
    std::ofstream m_file;
    std::string m_component;
    int m_core_id;
    int m_debug_level;
    bool m_enabled;

    // Format helpers
    std::string formatTimestamp() {
        // Could add simulation time here if needed
        return "";
    }

    std::string formatPrefix(Level level) {
        std::ostringstream oss;
        if (m_core_id >= 0) {
            oss << "[" << std::setw(12) << std::left << m_component 
                << "|C" << m_core_id << "] ";
        } else {
            oss << "[" << std::setw(12) << std::left << m_component << "] ";
        }
        (void)level;  // May be used in future for level prefixing
        return oss.str();
    }

    // Variadic template for flexible argument printing
    template<typename T>
    void appendArgs(std::ostringstream& oss, const T& arg) {
        oss << arg;
    }

    template<typename T, typename... Args>
    void appendArgs(std::ostringstream& oss, const T& first, const Args&... rest) {
        oss << first << " ";
        appendArgs(oss, rest...);
    }
    
    // Write to both component file and unified log
    void writeToLogs(Level level, const std::string& message) {
        // Write to component-specific file
        m_file << formatPrefix(level) << message << std::endl;
        
        // Also write to unified log if enabled
        UnifiedLog::instance().write(m_component, m_core_id, message);
    }
    
    void writeToLogsWithTime(Level level, uint64_t sim_time_ns, const std::string& message) {
        // Write to component-specific file
        std::ostringstream oss;
        oss << "@" << std::setw(12) << sim_time_ns << "ns: " << message;
        m_file << formatPrefix(level) << oss.str() << std::endl;
        
        // Also write to unified log with time
        UnifiedLog::instance().writeWithTime(m_component, m_core_id, sim_time_ns, message);
    }

public:
    /**
     * @brief Construct a new SimLog object
     * @param component Component name (e.g., "MMU", "TLB", "BUDDY")
     * @param core_id Core ID (-1 for global components)
     * @param debug_level Debug level from debug_config.h
     */
    SimLog(const std::string& component, int core_id = -1, int debug_level = DEBUG_NONE)
        : m_component(component), m_core_id(core_id), m_debug_level(debug_level), m_enabled(true) {
        
        // Initialize unified log on first SimLog creation
        UnifiedLog::instance().init();
        
        if (debug_level == DEBUG_NONE) {
            m_enabled = false;
            return;
        }
        
        // Build filename
        std::string filename = component;
        // Convert to lowercase and replace spaces
        for (char& c : filename) {
            if (c == ' ') c = '_';
            c = std::tolower(c);
        }
        
        if (core_id >= 0) {
            filename += "." + std::to_string(core_id);
        }
        filename += ".log";
        
        std::string path = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) 
                          + "/" + filename;
        m_file.open(path);
    }

    ~SimLog() {
        if (m_file.is_open()) {
            m_file.close();
        }
    }

    // Check if logging is enabled at a given level
    bool isEnabled(Level level = LEVEL_DEBUG) const {
        if (!m_enabled) return false;
        if (level == LEVEL_INFO) return true;
        if (level == LEVEL_DEBUG && m_debug_level >= DEBUG_BASIC) return true;
        if (level == LEVEL_TRACE && m_debug_level >= DEBUG_DETAILED) return true;
        return false;
    }

    // Info level - always logged when logging is enabled
    template<typename... Args>
    void info(const Args&... args) {
        if (!m_enabled) return;
        std::ostringstream oss;
        appendArgs(oss, args...);
        writeToLogs(LEVEL_INFO, oss.str());
    }

    // Debug level - logged at DEBUG_BASIC and above
    template<typename... Args>
    void debug(const Args&... args) {
        if (!isEnabled(LEVEL_DEBUG)) return;
        std::ostringstream oss;
        appendArgs(oss, args...);
        writeToLogs(LEVEL_DEBUG, oss.str());
    }

    // Trace level - logged only at DEBUG_DETAILED
    template<typename... Args>
    void trace(const Args&... args) {
        if (!isEnabled(LEVEL_TRACE)) return;
        std::ostringstream oss;
        appendArgs(oss, args...);
        writeToLogs(LEVEL_TRACE, oss.str());
    }

    // Formatted hex address logging
    void logAddress(Level level, const std::string& msg, uint64_t addr) {
        if (!isEnabled(level)) return;
        std::ostringstream oss;
        oss << msg << " 0x" << std::hex << addr << std::dec;
        writeToLogs(level, oss.str());
    }

    // Log with timestamp (simulation time)
    template<typename... Args>
    void logWithTime(Level level, uint64_t sim_time_ns, const Args&... args) {
        if (!isEnabled(level)) return;
        std::ostringstream oss;
        appendArgs(oss, args...);
        writeToLogsWithTime(level, sim_time_ns, oss.str());
    }

    // Section separator for readability
    void section(const std::string& title) {
        if (!m_enabled) return;
        m_file << "\n" << std::string(50, '-') << std::endl;
        m_file << "  " << title << std::endl;
        m_file << std::string(50, '-') << std::endl;
        
        // Also write to unified log
        UnifiedLog::instance().write(m_component, m_core_id, "--- " + title + " ---");
    }

    // Aliases for compatibility
    template<typename... Args>
    void log(const Args&... args) { info(args...); }

    template<typename... Args>
    void detailed(const Args&... args) { trace(args...); }

    // Format address as hex string (utility method)
    static std::string hex(uint64_t addr) {
        std::ostringstream oss;
        oss << "0x" << std::hex << addr << std::dec;
        return oss.str();
    }

    // Direct access to stream for custom formatting
    std::ofstream& stream() { return m_file; }
};

// Convenience macros for conditional logging
#define SIM_LOG_INFO(logger, ...) \
    do { if ((logger).isEnabled(SimLog::LEVEL_INFO)) (logger).info(__VA_ARGS__); } while(0)

#define SIM_LOG_DEBUG(logger, ...) \
    do { if ((logger).isEnabled(SimLog::LEVEL_DEBUG)) (logger).debug(__VA_ARGS__); } while(0)

#define SIM_LOG_TRACE(logger, ...) \
    do { if ((logger).isEnabled(SimLog::LEVEL_TRACE)) (logger).trace(__VA_ARGS__); } while(0)
