#include "MenuScreenModule.h"
#include "Config.h"
#include "Logger.h"
#include "DeviceInterfaces.h"
#include "ModuleDependency.h"
#include <iostream>
#include <unistd.h>

MenuScreenModule::MenuScreenModule(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input,
                                const std::string& id, const std::string& title)
    : ScreenModule(display, input), m_id(id), m_title(title)
{
    m_menu = std::make_shared<Menu>(display);
}

MenuScreenModule::~MenuScreenModule() {
    // Clean up any resources
    if (m_menu) {
        m_menu->clear();
    }
}

void MenuScreenModule::enter() {
    Logger::debug("Entering menu screen: " + m_id);
    
    // Clear the display
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 5);
    
    // set the menu title to the module title
    m_menu->setTitle(m_title);

    // Build the submenu based on registered items
    buildSubmenu();
    
    // Render the menu
    m_menu->render();
    
    // Reset exit flag
    m_exitToParent = false;
}

void MenuScreenModule::update() {
    // Nothing to update continuously in a menu screen
}

void MenuScreenModule::exit() {
    Logger::debug("Exiting menu screen: " + m_id);
    
    // Clear the display
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 5);
}

bool MenuScreenModule::handleInput() {
    // Check if exit to parent is requested
    if (m_exitToParent) {
        return false; // Exit this screen and return to parent
    }
    
    // Process input
    if (m_input->waitForEvents(100) > 0) {
        m_input->processEvents(
            [this](int direction) {
                // Handle rotary encoder rotation - pass to menu
                m_menu->handleRotation(direction);
            },
            [this]() {
                // Handle button press - activate selected menu item
                m_menu->handleButtonPress();
            }
        );
    }
    
    return true; // Continue running unless exit flag is set
}

void MenuScreenModule::addSubmenuItem(const std::string& moduleId, const std::string& title) {
    // Add a new submenu item
    SubmenuItem item;
    item.moduleId = moduleId;
    item.title = title;
    m_submenuItems.push_back(item);
    
    Logger::debug("Added submenu item '" + title + "' with id '" + moduleId + "' to menu " + m_id);
}

void MenuScreenModule::setModuleRegistry(const std::map<std::string, std::shared_ptr<ScreenModule>>* registry) {
    m_moduleRegistry = registry;
}

void MenuScreenModule::buildSubmenu() {
    // Clear any existing menu items
    m_menu->clear();
    
    // Check if we have a registry and items
    if (!m_moduleRegistry || m_submenuItems.empty()) {
        Logger::warning("Menu has no items or module registry not set");
        
        // Add a back option if we have a parent menu
        if (m_parentMenu) {
            m_menu->addItem(std::make_shared<ActionMenuItem>("Back", [this]() {
                m_exitToParent = true;
            }));
        }
        return;
    }
    
    // Add each submenu item
    for (const auto& item : m_submenuItems) {
        // Check if this is a Back item
        if (item.moduleId == "back") {
            m_menu->addItem(std::make_shared<ActionMenuItem>(item.title, [this]() {
                m_exitToParent = true;
            }));
            continue;
        }
        
	// Check if this is the special invert_display action
        if (item.moduleId == "invert_display") {
            m_menu->addItem(std::make_shared<ActionMenuItem>(item.title, [this]() {
                // Invert the display
                m_display->setInverted(!m_display->isInverted());
            }));
            continue;
        }

        // Otherwise, create an action item that launches the corresponding module
        m_menu->addItem(std::make_shared<ActionMenuItem>(item.title, [this, moduleId = item.moduleId]() {
            // Execute the module
            executeSubmenuAction(moduleId);
        }));
    }
}

void MenuScreenModule::executeSubmenuAction(const std::string& moduleId) {
    // Check if we have a module registry
    if (!m_moduleRegistry) {
        Logger::error("No module registry available");
        return;
    }

    // Special handling for back action
    if (moduleId == "back") {
        m_exitToParent = true;
        return;
    }

    // Special handling for invert_display action
    if (moduleId == "invert_display") {
        m_display->setInverted(!m_display->isInverted());
        return;
    }

    // Look up the module in the registry
    auto it = m_moduleRegistry->find(moduleId);
    if (it == m_moduleRegistry->end()) {
        Logger::error("Module not found in registry: " + moduleId);
        return;
    }

    // Get the module
    auto module = it->second;
    if (!module) {
        Logger::error("Invalid module pointer for: " + moduleId);
        return;
    }

    // Check if this is a menu module
    auto menuModule = std::dynamic_pointer_cast<MenuScreenModule>(module);
    bool isMenuModule = (menuModule != nullptr);
    
    // Check dependencies for regular (non-menu) modules
    if (!isMenuModule) {
        auto& dependencies = ModuleDependency::getInstance();
        if (!dependencies.shouldSkipDependencyCheck(moduleId) && !dependencies.checkDependencies(moduleId)) {
            Logger::warning("Dependencies not satisfied for module: " + moduleId);
            
            // Show a message on display
            m_display->clear();
            usleep(Config::DISPLAY_CMD_DELAY * 5);
            m_display->drawText(0, 0, "Dependency Error");
            m_display->drawText(0, 10, "Module unavailable:");
            m_display->drawText(0, 20, moduleId);
            usleep(Config::DISPLAY_CMD_DELAY * 2000); // Show for 2 seconds
            
            // Re-render the menu
            m_display->clear();
            usleep(Config::DISPLAY_CMD_DELAY * 5);
            m_menu->render();
            return;
        }
    }

    // If this is a MenuScreenModule, set its parent to this
    if (isMenuModule) {
        menuModule->setParentMenu(this);
    }

    // Execute the module
    Logger::debug("Executing submenu module: " + moduleId);

    // Clear the display before launching the module
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 5);

    // Run the module
    module->run();

    // Clear the display before returning to menu
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 5);

    // Re-render our menu
    m_menu->render();
}
