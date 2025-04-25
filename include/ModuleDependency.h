#pragma once

#include <string>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>

/**
 * Manages dependencies for screen modules, such as scripts and config files
 */
class ModuleDependency {
public:
    // Singleton access
    static ModuleDependency& getInstance();

    // Delete copy and assignment to ensure singleton
    ModuleDependency(const ModuleDependency&) = delete;
    ModuleDependency& operator=(const ModuleDependency&) = delete;

    // Load dependencies from JSON configuration
    bool loadDependencies(const nlohmann::json& config);

    // Get a dependency path for a module
    std::string getDependencyPath(const std::string& moduleId, const std::string& dependencyKey);

    // Check if a module has a specific dependency
    bool hasDependency(const std::string& moduleId, const std::string& dependencyKey);

    // Get all dependencies for a module
    const std::map<std::string, std::string>& getModuleDependencies(const std::string& moduleId);

    // Check if all dependencies for a module are satisfied
    bool checkDependencies(const std::string& moduleId);
    
    // Special flag to skip dependency checks for menus
    bool shouldSkipDependencyCheck(const std::string& moduleId);
private:
    // Private constructor for singleton
    ModuleDependency();
    
    // Module dependencies storage
    // moduleId -> (dependencyKey -> dependencyPath)
    std::map<std::string, std::map<std::string, std::string>> m_dependencies;
    
    // Empty map for when a module has no dependencies
    std::map<std::string, std::string> m_emptyMap;
};
