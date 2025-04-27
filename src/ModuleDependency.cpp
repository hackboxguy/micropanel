#include "ModuleDependency.h"
#include "Logger.h"
#include <unistd.h>
// Static instance for singleton
ModuleDependency& ModuleDependency::getInstance() {
    static ModuleDependency instance;
    return instance;
}

ModuleDependency::ModuleDependency() {
    // Initialize with empty dependencies
}

bool ModuleDependency::loadDependencies(const nlohmann::json& config) {
    try {
        // Check for modules array
        if (!config.contains("modules") || !config["modules"].is_array()) {
            Logger::warning("No modules array found in configuration");
            return false;
        }

        // Clear existing dependencies
        m_dependencies.clear();
        
        // Process modules
        for (const auto& module : config["modules"]) {
            // Check for required fields
            if (!module.contains("id") || !module["id"].is_string()) {
                Logger::warning("Skipping module with missing or invalid id");
                continue;
            }
            
            // Get module ID
            std::string moduleId = module["id"].get<std::string>();
            
            // Check for dependencies
            if (module.contains("depends") && module["depends"].is_object()) {
                // Process dependencies
                for (auto it = module["depends"].begin(); it != module["depends"].end(); ++it) {
                    std::string key = it.key();
                    
                    // Only process string dependencies
                    if (it.value().is_string()) {
                        std::string path = it.value().get<std::string>();
                        
                        // Store dependency
                        m_dependencies[moduleId][key] = path;
                        Logger::debug("Registered dependency for " + moduleId + ": " + key + " -> " + path);
                    } else {
                        Logger::warning("Ignoring non-string dependency '" + key + "' for module " + moduleId);
                    }
                }
            }
        }
        
        Logger::info("Module dependencies loaded successfully");
        return true;
    } catch (const std::exception& e) {
        Logger::error("Error loading module dependencies: " + std::string(e.what()));
        return false;
    }
}

std::string ModuleDependency::getDependencyPath(const std::string& moduleId, const std::string& dependencyKey) {
    // Check if module exists
    auto moduleIt = m_dependencies.find(moduleId);
    if (moduleIt == m_dependencies.end()) {
        return "";
    }
    
    // Check if dependency exists
    auto dependencyIt = moduleIt->second.find(dependencyKey);
    if (dependencyIt == moduleIt->second.end()) {
        return "";
    }
    
    // Return the dependency path
    return dependencyIt->second;
}

bool ModuleDependency::hasDependency(const std::string& moduleId, const std::string& dependencyKey) {
    // Check if module exists
    auto moduleIt = m_dependencies.find(moduleId);
    if (moduleIt == m_dependencies.end()) {
        return false;
    }
    
    // Check if dependency exists
    return moduleIt->second.find(dependencyKey) != moduleIt->second.end();
}

const std::map<std::string, std::string>& ModuleDependency::getModuleDependencies(const std::string& moduleId) {
    // Check if module exists
    auto moduleIt = m_dependencies.find(moduleId);
    if (moduleIt == m_dependencies.end()) {
        return m_emptyMap;
    }
    
    // Return all dependencies for the module
    return moduleIt->second;
}
/*
bool ModuleDependency::checkDependencies(const std::string& moduleId) {
    // Always allow menu modules to pass dependency checks
    if (shouldSkipDependencyCheck(moduleId)) {
        return true;
    }

    // If the module has no dependencies, they're satisfied by default
    auto moduleIt = m_dependencies.find(moduleId);
    if (moduleIt == m_dependencies.end()) {
        return true;
    }

    // For now, we'll just check if the dependency paths exist
    // A more sophisticated check might be needed based on your requirements
    for (const auto& dep : moduleIt->second) {
        std::string path = dep.second;

        // Check if file exists
        if (access(path.c_str(), F_OK) != 0) {
            Logger::warning("Dependency not satisfied: " + path + " for module " + moduleId);
            return true;//false;TODO
        }
    }

    return true;
}
*/
bool ModuleDependency::checkDependencies(const std::string& moduleId) {
    // Always allow menu modules to pass dependency checks
    if (shouldSkipDependencyCheck(moduleId)) {
        return true;
    }

    // If the module has no dependencies, they're satisfied by default
    auto moduleIt = m_dependencies.find(moduleId);
    if (moduleIt == m_dependencies.end()) {
        return true;
    }

    // For each dependency, check if it exists
    for (const auto& dep : moduleIt->second) {
        std::string key = dep.first;
        std::string path = dep.second;

        // Skip URL dependencies (http:// or https://)
        if (path.substr(0, 7) == "http://" || path.substr(0, 8) == "https://") {
            Logger::debug("Skipping URL dependency check for: " + path);
            continue;
        }

        // Check if file exists
        if (access(path.c_str(), F_OK) != 0) {
            Logger::warning("Dependency not satisfied: " + path + " for module " + moduleId);
            return true;  // Changed to true as per your code comment
        }
    }

    return true;
}

bool ModuleDependency::shouldSkipDependencyCheck(const std::string& moduleId) {
    // Skip dependency checks for menu modules
    return moduleId.find("_menu") != std::string::npos;
}
