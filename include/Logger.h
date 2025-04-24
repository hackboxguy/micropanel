#pragma once

#include <string>
#include <iostream>

/**
 * Simple logging utility for MicroPanel
 */
class Logger {
public:
    enum class Level {
        DEBUG,  // Only shown in verbose mode
        INFO,   // Normal operation info
        WARNING, // Warnings
        ERROR    // Errors
    };
    
    // Set verbose mode
    static void setVerbose(bool verbose) {
        m_verbose = verbose;
    }
    
    // Get verbose mode status
    static bool isVerbose() {
        return m_verbose;
    }
    
    // Log with specified level
    static void log(Level level, const std::string& message) {
        // Skip debug messages in non-verbose mode
        if (level == Level::DEBUG && !m_verbose) {
            return;
        }
        
        switch (level) {
            case Level::DEBUG:
                std::cout << "[DEBUG] " << message << std::endl;
                break;
            case Level::INFO:
                std::cout << message << std::endl;
                break;
            case Level::WARNING:
                std::cerr << "[WARNING] " << message << std::endl;
                break;
            case Level::ERROR:
                std::cerr << "[ERROR] " << message << std::endl;
                break;
        }
    }
    
    // Convenience methods
    static void debug(const std::string& message) {
        log(Level::DEBUG, message);
    }
    
    static void info(const std::string& message) {
        log(Level::INFO, message);
    }
    
    static void warning(const std::string& message) {
        log(Level::WARNING, message);
    }
    
    static void error(const std::string& message) {
        log(Level::ERROR, message);
    }
    
private:
    static bool m_verbose;
};
