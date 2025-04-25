#pragma once

#include <string>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>

/**
 * Centralized persistent storage manager for screen modules
 * Handles reading/writing JSON data for all modules
 */
class PersistentStorage {
public:
    // Singleton access
    static PersistentStorage& getInstance();

    // Delete copy and assignment to ensure singleton
    PersistentStorage(const PersistentStorage&) = delete;
    PersistentStorage& operator=(const PersistentStorage&) = delete;

    // Initialize with storage file path
    bool initialize(const std::string& filePath = "");

    // Set and get string values
    bool setValue(const std::string& moduleId, const std::string& key, const std::string& value);
    std::string getValue(const std::string& moduleId, const std::string& key, const std::string& defaultValue = "");

    // Set and get integer values
    bool setValue(const std::string& moduleId, const std::string& key, int value);
    int getValue(const std::string& moduleId, const std::string& key, int defaultValue = 0);

    // Set and get boolean values
    bool setValue(const std::string& moduleId, const std::string& key, bool value);
    bool getValue(const std::string& moduleId, const std::string& key, bool defaultValue = false);

    // Set and get floating point values
    bool setValue(const std::string& moduleId, const std::string& key, double value);
    double getValue(const std::string& moduleId, const std::string& key, double defaultValue = 0.0);

    // Check if a value exists
    bool hasValue(const std::string& moduleId, const std::string& key);

    // Save data to file (can be called explicitly, but also called by setValue methods)
    bool saveToFile();

    // Get the current storage file path
    std::string getStorageFilePath() const { return m_storageFilePath; }

    // Check if storage is available
    bool isAvailable() const { return m_initialized; }

private:
    // Private constructor for singleton
    PersistentStorage();
    ~PersistentStorage();

    // Load data from file
    bool loadFromFile();

    // Internal data storage
    nlohmann::json m_data;
    std::string m_storageFilePath;
    bool m_initialized = false;
    bool m_isDirty = false;
    
    // Thread safety
    std::mutex m_mutex;
    
    // Delay save mechanism
    void scheduleSave();
    void cancelScheduledSave();
    bool m_savePending = false;
};
