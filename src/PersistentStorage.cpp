#include "PersistentStorage.h"
#include "Logger.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>

// Helper function to check if file exists
bool fileExists(const std::string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0 && (info.st_mode & S_IFREG);
}

// Helper function to check if directory exists
bool directoryExists(const std::string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0 && (info.st_mode & S_IFDIR);
}

// Helper function to create directory
bool createDirectory(const std::string& path) {
    return mkdir(path.c_str(), 0755) == 0;
}

// Helper function to get parent path
std::string getParentPath(const std::string& path) {
    char* pathCopy = strdup(path.c_str());
    char* dir = dirname(pathCopy);
    std::string result(dir);
    free(pathCopy);
    return result;
}

// Helper function to rename file atomically
bool renameFile(const std::string& oldPath, const std::string& newPath) {
    return rename(oldPath.c_str(), newPath.c_str()) == 0;
}

// Static instance for singleton
PersistentStorage& PersistentStorage::getInstance() {
    static PersistentStorage instance;
    return instance;
}

PersistentStorage::PersistentStorage() 
    : m_initialized(false), m_isDirty(false), m_savePending(false) {
    // Nothing to do in constructor
}

PersistentStorage::~PersistentStorage() {
    // Ensure pending changes are saved when object is destroyed
    if (m_isDirty && m_initialized) {
        saveToFile();
    }
}

bool PersistentStorage::initialize(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // If already initialized with the same path, just return success
    if (m_initialized && m_storageFilePath == filePath) {
        return true;
    }
    
    // If no file path provided and already initialized, keep using current path
    if (filePath.empty() && m_initialized) {
        return true;
    }
    
    // If no file path provided and not initialized, we can't proceed
    if (filePath.empty() && !m_initialized) {
        Logger::error("Cannot initialize PersistentStorage without a file path");
        return false;
    }
    
    // Set the new file path
    m_storageFilePath = filePath;
    
    // Create parent directory if it doesn't exist
    try {
        std::string parentPath = getParentPath(m_storageFilePath);
        if (!parentPath.empty() && !directoryExists(parentPath)) {
            if (!createDirectory(parentPath)) {
                Logger::error("Failed to create directory for storage file: " + parentPath);
                return false;
            }
        }
    } catch (const std::exception& e) {
        Logger::error("Failed to create directory for storage file: " + std::string(e.what()));
        return false;
    }

    // Try to load existing data
    if (!loadFromFile()) {
        // If loading fails, start with empty data
        m_data = nlohmann::json::object();
        
        // This is not an error - it could be the first run
        Logger::info("Starting with empty persistent storage (file not found or invalid)");
    }
    
    m_initialized = true;
    return true;
}


/*bool PersistentStorage::loadFromFile() {
    try {
        // Check if file exists
        if (!fileExists(m_storageFilePath)) {
            return false;
        }
        
        // Open the file
        std::ifstream file(m_storageFilePath);
        if (!file.is_open()) {
            Logger::error("Failed to open storage file: " + m_storageFilePath);
            return false;
        }
        
        // Parse JSON
        m_data = nlohmann::json::parse(file);
        
        // Check if root is an object
        if (!m_data.is_object()) {
            Logger::error("Storage file does not contain a valid JSON object");
            m_data = nlohmann::json::object();
            return false;
        }
        
        Logger::debug("Successfully loaded persistent storage from " + m_storageFilePath);
        return true;
    } catch (const nlohmann::json::parse_error& e) {
        Logger::error("JSON parse error in storage file: " + std::string(e.what()));
        // Reset to empty object on parse error
        m_data = nlohmann::json::object();
        return false;
    } catch (const std::exception& e) {
        Logger::error("Error loading storage file: " + std::string(e.what()));
        return false;
    }
}
*/
bool PersistentStorage::loadFromFile() {
    try {
        // Check if file exists
        if (!fileExists(m_storageFilePath)) {
            return false;
        }

        // Open the file
        std::ifstream file(m_storageFilePath);
        if (!file.is_open()) {
            Logger::error("Failed to open storage file: " + m_storageFilePath);
            return false;
        }

        // Check if file is empty and return false if it is
        if (file.peek() == std::ifstream::traits_type::eof()) {
            Logger::info("Storage file is empty, will initialize with empty object");
            m_data = nlohmann::json::object();
            return false;
        }

        // Parse JSON
        try {
            m_data = nlohmann::json::parse(file);
        } catch (const nlohmann::json::parse_error& e) {
            Logger::error("JSON parse error in storage file: " + std::string(e.what()));
            m_data = nlohmann::json::object();
            return false;
        }

        // Check if root is an object
        if (!m_data.is_object()) {
            Logger::error("Storage file does not contain a valid JSON object");
            m_data = nlohmann::json::object();
            return false;
        }

        Logger::debug("Successfully loaded persistent storage from " + m_storageFilePath);
        return true;
    } catch (const std::exception& e) {
        Logger::error("Error loading storage file: " + std::string(e.what()));
        return false;
    }
}

bool PersistentStorage::saveToFile() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // If not initialized or no changes to save, return early
    if (!m_initialized || !m_isDirty) {
        return m_initialized;
    }
    
    try {
        // Create a temporary filename
        std::string tempFile = m_storageFilePath + ".tmp";
        
        // Write to temporary file first
        std::ofstream file(tempFile);
        if (!file.is_open()) {
            Logger::error("Failed to open temporary storage file for writing: " + tempFile);
            return false;
        }
        
        // Write formatted JSON
        file << m_data.dump(2);
        file.close();
        
        // Check for write errors
        if (!file) {
            Logger::error("Error writing to temporary storage file");
            return false;
        }
        
        // Rename temporary file to actual file (atomic operation)
        if (!renameFile(tempFile, m_storageFilePath)) {
            Logger::error("Failed to rename temporary file to target file");
            return false;
        }
        
        Logger::debug("Successfully saved persistent storage to " + m_storageFilePath);
        m_isDirty = false;
        m_savePending = false;
        return true;
    } catch (const std::exception& e) {
        Logger::error("Error saving storage file: " + std::string(e.what()));
        return false;
    }
}

void PersistentStorage::scheduleSave() {
    // If a save is already pending, don't schedule another one
    if (m_savePending) {
        return;
    }
    
    m_savePending = true;
    
    // Start a thread that waits a bit then saves
    std::thread([this]() {
        // Wait 2 seconds before saving
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // Check if save is still pending (could have been canceled)
        if (m_savePending) {
            saveToFile();
        }
    }).detach();
}

void PersistentStorage::cancelScheduledSave() {
    m_savePending = false;
}

// String values
bool PersistentStorage::setValue(const std::string& moduleId, const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_initialized) {
        Logger::warning("Attempted to set value before initializing storage");
        return false;
    }
    
    // Ensure module section exists
    if (!m_data.contains(moduleId) || !m_data[moduleId].is_object()) {
        m_data[moduleId] = nlohmann::json::object();
    }
    
    // Set the value
    m_data[moduleId][key] = value;
    m_isDirty = true;
    
    // Schedule a delayed save
    scheduleSave();
    
    return true;
}

std::string PersistentStorage::getValue(const std::string& moduleId, const std::string& key, const std::string& defaultValue) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_initialized) {
        Logger::warning("Attempted to get value before initializing storage");
        return defaultValue;
    }
    
    // Check if module and key exist
    if (m_data.contains(moduleId) && m_data[moduleId].is_object() && 
        m_data[moduleId].contains(key) && m_data[moduleId][key].is_string()) {
        return m_data[moduleId][key].get<std::string>();
    }
    
    return defaultValue;
}

// Integer values
bool PersistentStorage::setValue(const std::string& moduleId, const std::string& key, int value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_initialized) {
        Logger::warning("Attempted to set value before initializing storage");
        return false;
    }
    
    // Ensure module section exists
    if (!m_data.contains(moduleId) || !m_data[moduleId].is_object()) {
        m_data[moduleId] = nlohmann::json::object();
    }
    
    // Set the value
    m_data[moduleId][key] = value;
    m_isDirty = true;
    
    // Schedule a delayed save
    scheduleSave();
    
    return true;
}

int PersistentStorage::getValue(const std::string& moduleId, const std::string& key, int defaultValue) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_initialized) {
        Logger::warning("Attempted to get value before initializing storage");
        return defaultValue;
    }
    
    // Check if module and key exist
    if (m_data.contains(moduleId) && m_data[moduleId].is_object() && 
        m_data[moduleId].contains(key) && m_data[moduleId][key].is_number_integer()) {
        return m_data[moduleId][key].get<int>();
    }
    
    return defaultValue;
}

// Boolean values
bool PersistentStorage::setValue(const std::string& moduleId, const std::string& key, bool value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_initialized) {
        Logger::warning("Attempted to set value before initializing storage");
        return false;
    }
    
    // Ensure module section exists
    if (!m_data.contains(moduleId) || !m_data[moduleId].is_object()) {
        m_data[moduleId] = nlohmann::json::object();
    }
    
    // Set the value
    m_data[moduleId][key] = value;
    m_isDirty = true;
    
    // Schedule a delayed save
    scheduleSave();
    
    return true;
}

bool PersistentStorage::getValue(const std::string& moduleId, const std::string& key, bool defaultValue) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_initialized) {
        Logger::warning("Attempted to get value before initializing storage");
        return defaultValue;
    }
    
    // Check if module and key exist
    if (m_data.contains(moduleId) && m_data[moduleId].is_object() && 
        m_data[moduleId].contains(key) && m_data[moduleId][key].is_boolean()) {
        return m_data[moduleId][key].get<bool>();
    }
    
    return defaultValue;
}

// Double values
bool PersistentStorage::setValue(const std::string& moduleId, const std::string& key, double value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_initialized) {
        Logger::warning("Attempted to set value before initializing storage");
        return false;
    }
    
    // Ensure module section exists
    if (!m_data.contains(moduleId) || !m_data[moduleId].is_object()) {
        m_data[moduleId] = nlohmann::json::object();
    }
    
    // Set the value
    m_data[moduleId][key] = value;
    m_isDirty = true;
    
    // Schedule a delayed save
    scheduleSave();
    
    return true;
}

double PersistentStorage::getValue(const std::string& moduleId, const std::string& key, double defaultValue) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_initialized) {
        Logger::warning("Attempted to get value before initializing storage");
        return defaultValue;
    }
    
    // Check if module and key exist
    if (m_data.contains(moduleId) && m_data[moduleId].is_object() && 
        m_data[moduleId].contains(key) && m_data[moduleId][key].is_number()) {
        return m_data[moduleId][key].get<double>();
    }
    
    return defaultValue;
}

// Check if value exists
bool PersistentStorage::hasValue(const std::string& moduleId, const std::string& key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_initialized) {
        return false;
    }
    
    return m_data.contains(moduleId) && m_data[moduleId].is_object() && m_data[moduleId].contains(key);
}
