#include "MenuScreenModule.h"
#include "Config.h"
#include "Logger.h"
#include "DeviceInterfaces.h"
#include "ModuleDependency.h"
#include "ScreenModules.h"
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
    // If this is the top level menu, always clear the exit to main menu flag
    if (m_isTopLevelMenu) {
        m_exitToMainMenu = false;
    }
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
        //return false; // Exit this screen and return to parent
        // If we're exiting to the main menu, tell the parent to also exit
        if (m_exitToMainMenu && m_parentMenu) {
            // Propagate the flag to the parent menu
            m_parentMenu->m_exitToMainMenu = true;
            m_parentMenu->m_exitToParent = true;
        }
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
                m_exitToMainMenu = false;
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
                m_exitToMainMenu = false;
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
    // Add a "Main Menu" option if we're in a nested menu (not the top level)
    if (m_parentMenu && !m_isTopLevelMenu) {  // Only add if we have a parent and aren't the top level
        m_menu->addItem(std::make_shared<ActionMenuItem>("Main Menu", [this]() {
            navigateToMainMenu();
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
        m_exitToMainMenu = false;
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

    // Check if this is a GenericListScreen
    auto genericListScreen = std::dynamic_pointer_cast<GenericListScreen>(module);

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
            m_display->drawText(0, 40, "Press any key...");

            if (m_useGPIOMode) {
                // In GPIO mode, input comes via MicroPanel's GPIO event loop,
                // not m_input — just show the error briefly and return
                usleep(2000000); // 2 seconds
            } else {
                // Drain any pending input events (from the button press that got us here)
                if (m_input) {
                    while (m_input->waitForEvents(50) > 0) {
                        m_input->processEvents([](int){}, [](){});
                    }
                }

                // Now wait for a fresh key press (no timeout - wait indefinitely)
                if (m_input) {
                    while (m_input->waitForEvents(500) <= 0) {
                        // Keep waiting until user presses something
                    }
                    // Consume the event so it doesn't propagate
                    m_input->processEvents([](int){}, [](){});
                }
            }

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
        menuModule->clearMainMenuFlag();
    }
    // If this is a GenericListScreen, set its callback to this
    if (genericListScreen) {
        genericListScreen->setCallback(this);
    }

    // If this is a NetInfoScreen, set its callback to this
    auto netInfoScreen = std::dynamic_pointer_cast<NetInfoScreen>(module);
    if (netInfoScreen) {
        netInfoScreen->setCallback(this);
    }

    // Execute the module
    Logger::debug("Executing submenu module: " + moduleId);

    // Clear the display before launching the module
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 5);

    // Run the module
    //module->run();
    // Run the module (with GPIO support if enabled)
    if (m_useGPIOMode && m_gpioHandler) {
        Logger::debug("MenuScreenModule: Using GPIO handler for module");
        m_gpioHandler(module);
    } else {
        Logger::debug("MenuScreenModule: Using traditional run() for module");
        module->run();
    }

    // Clear the display before returning to menu
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 5);

    // Re-render our menu
    m_menu->render();
}
void MenuScreenModule::navigateToMainMenu() {
    Logger::debug("Navigating to main menu from: " + m_id);

    // Set our exit flags to trigger a return to parent menu
    m_exitToParent = true;
    m_exitToMainMenu = true;
}

void MenuScreenModule::onScreenAction(const std::string& screenId,
                                    const std::string& action,
                                    const std::string& value)
{
    Logger::debug("Menu received callback from " + screenId +
                ": Action=" + action + ", Value=" + value);

    // Store the value for later use
    std::string key = screenId + "." + action;
    m_callbackValues[key] = value;

    // You can add specific actions here, for example:
    if (action == "selection_changed") {
        // Handle selection change
    } else if (action == "item_activated") {
        // Handle item activation
    } else if (action == "exit_to_main_menu") {
        // Handle request to navigate to main menu from child screen
        Logger::debug("Child screen requested exit to main menu: " + screenId);
        navigateToMainMenu();
    } else if (action == "launch_module") {
        // Handle module launching request from GenericListScreen
        Logger::debug("Module launch requested: " + value);
        handleModuleLaunch(screenId, value);
    }
}

void MenuScreenModule::handleModuleLaunch(const std::string& sourceModuleId, const std::string& value) {
    Logger::debug("MenuScreenModule::handleModuleLaunch called from " + sourceModuleId + " with value: " + value);

    // Parse the value: "textbox:eth0" -> moduleType="textbox", parameter="eth0"
    size_t colonPos = value.find(':');
    if (colonPos == std::string::npos) {
        Logger::warning("Invalid module launch value format: " + value);
        return;
    }

    std::string moduleType = value.substr(0, colonPos);
    std::string parameter = value.substr(colonPos + 1);

    Logger::debug("Module launch request - Type: " + moduleType + ", Parameter: " + parameter);

    // For now, we'll handle textbox modules
    if (moduleType == "textbox") {
        // Create a new TextBoxScreen with runtime parameters
        auto textboxModule = std::make_shared<TextBoxScreen>(m_display, m_input);

        // Set up dynamic module ID
        std::string dynamicId = parameter + "_dynamic_stats";
        textboxModule->setId(dynamicId);

        // Set runtime parameters for substitution
        std::map<std::string, std::string> runtimeParams;
        runtimeParams["INTERFACE"] = parameter;
        textboxModule->setRuntimeParameters(runtimeParams);

        // Add dependencies programmatically
        auto& dependencies = ModuleDependency::getInstance();
        // Get resolved script path from the dynamic_network_stats template module
        std::string templateScriptPath = dependencies.getDependencyPath("dynamic_network_stats", "script_path");
        std::string scriptPath;
        if (!templateScriptPath.empty()) {
            // Use the resolved path from the template
            scriptPath = templateScriptPath;
        } else {
            // Fallback if template doesn't exist
            scriptPath = "./scripts/network-data-info.sh --interface=$INTERFACE";
        }
        dependencies.addDependency(dynamicId, "script_path", scriptPath);
        dependencies.addDependency(dynamicId, "refresh_sec", "2.0");
        dependencies.addDependency(dynamicId, "display_title", "$INTERFACE Stats");

        Logger::debug("Launching dynamic TextBoxScreen for interface: " + parameter);

        // If we have a GPIO handler, use it to run the module
        if (m_gpioHandler && m_useGPIOMode) {
            m_gpioHandler(textboxModule);
        } else {
            // Fallback: run the module directly
            textboxModule->run();
        }
    } else {
        Logger::warning("Unsupported module type for launch: " + moduleType);
    }
}

void MenuScreenModule::handleGPIORotation(int direction) {
    if (m_menu) {
        m_menu->handleRotation(direction);
    }
}

bool MenuScreenModule::handleGPIOButtonPress() {
    Logger::debug("MenuScreenModule::handleGPIOButtonPress() called");

    if (m_menu) {
        Logger::debug("Calling m_menu->handleButtonPress()");
        m_menu->handleButtonPress();

        Logger::debug("After menu button press - exitToParent: " + std::to_string(m_exitToParent) +
                      ", exitToMainMenu: " + std::to_string(m_exitToMainMenu));

        // Check for exit conditions (same as the original handleInput logic)
        if (m_exitToParent) {
            // If we're exiting to the main menu, tell the parent to also exit
            if (m_exitToMainMenu && m_parentMenu) {
                // Propagate the flag to the parent menu
                m_parentMenu->m_exitToMainMenu = true;
                m_parentMenu->m_exitToParent = true;
            }
            Logger::debug("MenuScreenModule should exit!");
            return false; // Exit the module
        }

        return true; // Continue running
    }
    return false; // Exit if no menu
}
void MenuScreenModule::setGPIOHandler(std::function<void(std::shared_ptr<ScreenModule>)> gpioHandler) {
    m_gpioHandler = gpioHandler;
}
