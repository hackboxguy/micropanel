#include "MicroPanel.h"
#include "Config.h"
#include "DeviceInterfaces.h"
#include "MenuSystem.h"
#include "ScreenModules.h"
#include "MenuScreenModule.h"
#include "PersistentStorage.h"
#include "ModuleDependency.h"
#include "Logger.h"
#include <iostream>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <fstream>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// Static instance for signal handler
MicroPanel* MicroPanel::s_instance = nullptr;
extern std::atomic<bool> g_signalReceived;

// Signal handler function
void MicroPanel::signalHandler(int signal)
{
   (void)signal;
   if (s_instance) {
        s_instance->m_running = false;
    }
   g_signalReceived.store(true);
   Logger::debug("Signal received, initiating shutdown...");
}

MicroPanel::MicroPanel(int argc, char* argv[])
{
    // Set static instance for signal handler
    s_instance = this;
    m_running=true; 
    setupSignalHandlers(); 
    // Default configuration
    m_config.inputDevice = Config::DEFAULT_INPUT_DEVICE;
    m_config.serialDevice = Config::DEFAULT_SERIAL_DEVICE;
    m_config.verboseMode = false;
    m_config.autoDetect = false;
    m_config.powerSaveEnabled = false;
    
    // Parse command line arguments
    parseCommandLine(argc, argv);
}

MicroPanel::~MicroPanel()
{
    shutdown();
    s_instance = nullptr;
}

void MicroPanel::parseCommandLine(int argc, char* argv[])
{
    // Default configuration - now with auto-detect enabled by default
    m_config.autoDetect = true;  // Enable auto-detection by default

    int opt;
    while ((opt = getopt(argc, argv, "i:s:c:vahp")) != -1) {
        switch (opt) {
            case 'i':
                m_config.inputDevice = optarg;
                m_config.autoDetect = false;  // Disable auto-detect when specific device is provided
                break;
            case 's':
                m_config.serialDevice = optarg;
                break;
            case 'c':
                m_config.configFile = optarg;
                // Default persistent data file location based on config file
                if (m_config.persistentDataFile.empty()) {
                    std::string configPath = optarg;
                    size_t lastDot = configPath.find_last_of('.');
                    if (lastDot != std::string::npos) {
                        m_config.persistentDataFile = configPath.substr(0, lastDot) + "_data.json";
                    } else {
                        m_config.persistentDataFile = configPath + "_data.json";
                    }
                }
                Logger::info("Using configuration file: " + std::string(optarg));
                Logger::info("Using persistent data file: " + m_config.persistentDataFile);
                break;
            case 'v':
                m_config.verboseMode = true;
                Logger::setVerbose(true);
                Logger::debug("Verbose mode enabled");
                break;
            case 'a':
                m_config.autoDetect = true;
                Logger::info("Auto-detection mode enabled");
                break;
            case 'p':
                m_config.powerSaveEnabled = true;
                Logger::info("Power save mode enabled (timeout: " +
                          std::to_string(Config::POWER_SAVE_TIMEOUT_SEC) + " seconds)");
                break;
            case 'h':
                std::cout << "OLED Menu Control Daemon v" << Config::VERSION << std::endl;
                std::cout << "Usage: " << argv[0] << " [OPTIONS]\n\n";
                std::cout << "Options:\n";
                std::cout << "  -i DEVICE   Specify input device (default: auto-detect)\n";
                std::cout << "  -s DEVICE   Specify serial device for display (default: auto-detect)\n";
                std::cout << "  -c FILE     Specify JSON configuration file for screen modules\n";
                std::cout << "  -a          Auto-detect HMI device (enabled by default)\n";
                std::cout << "  -p          Enable power save mode (display turns off after "
                        << Config::POWER_SAVE_TIMEOUT_SEC << " seconds of inactivity)\n";
                std::cout << "  -v          Enable verbose debug output\n";
                std::cout << "  -h          Display this help message\n\n";
                std::cout << "Example:\n";
                std::cout << "  " << argv[0] << " -i /dev/input/event11 -s /dev/ttyACM0 -c /etc/screens.json -v\n\n";
                std::cout << "Controls:\n";
                std::cout << "  - Rotate encoder left/right to navigate menu\n";
                std::cout << "  - Press encoder button to select menu item\n";
                std::cout << "  - Press Ctrl+C to exit program\n";
                exit(EXIT_SUCCESS);
                break;
            default:
                std::cerr << "Try '" << argv[0] << " -h' for more information." << std::endl;
                exit(EXIT_FAILURE);
        }
    }

    Logger::debug("Auto-detection: " + std::string(m_config.autoDetect ? "ENABLED" : "DISABLED"));
}

void MicroPanel::setupSignalHandlers()
{
    // Set up signal handlers for clean exit
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
}

bool MicroPanel::initialize()
{
    // Set up signal handlers
    setupSignalHandlers();

    // Initialize device manager
    m_deviceManager = std::make_shared<DeviceManager>();

    // If auto-detect is enabled, wait for device to be connected
    if (m_config.autoDetect) {
        std::cout << "Waiting for HMI device to be connected..." << std::endl;

        // First check if the device is already connected
        auto devices = m_deviceManager->detectDevices();
        if (devices.first.empty() || devices.second.empty()) {
            // Device not connected, wait for it
            std::cout << "HMI device not found. Waiting for connection..." << std::endl;

            if (!m_deviceManager->monitorDeviceUntilConnected(m_running)) {
                std::cerr << "Gave up waiting for device" << std::endl;
                return false;
            }

            // Try again after device is connected
            devices = m_deviceManager->detectDevices();
        }

        if (!devices.first.empty() && !devices.second.empty()) {
            m_config.inputDevice = devices.first;
            m_config.serialDevice = devices.second;
            std::cout << "Auto-detected input device: " << m_config.inputDevice << std::endl;
            std::cout << "Auto-detected serial device: " << m_config.serialDevice << std::endl;
        } else {
            std::cerr << "Failed to auto-detect devices" << std::endl;
            return false;
        }
    }

    // Initialize devices
    m_displayDevice = std::make_shared<DisplayDevice>(m_config.serialDevice);
    m_inputDevice = std::make_shared<InputDevice>(m_config.inputDevice);

    // Open devices
    if (!m_inputDevice->open()) {
        std::cerr << "Failed to open input device: " << m_config.inputDevice << std::endl;
        return false;
    }

    if (!m_displayDevice->open()) {
        std::cerr << "Failed to open display device: " << m_config.serialDevice << std::endl;
        m_inputDevice->close();
        return false;
    }

    // Create display wrapper
    m_display = std::make_shared<Display>(m_displayDevice);

    // Configure power save if enabled
    if (m_config.powerSaveEnabled) {
        m_display->enablePowerSave(true);
    }

    // Initialize main menu
    m_mainMenu = std::make_shared<Menu>(m_display);

    // Initialize modules
    initializeModules();
    
    // Initialize persistent storage if config file is provided
    if (!m_config.configFile.empty()) {
        if (!initPersistentStorage()) {
            Logger::warning("Failed to initialize persistent storage");
            // Continue anyway, persistent storage will be unavailable
        }
        
        // Load module dependencies
        if (!loadModuleDependencies()) {
            Logger::warning("Failed to load module dependencies");
            // Continue anyway, dependencies will be unavailable
        }
    }
    
    // Set up menu based on config file or default setup
    if (!m_config.configFile.empty()) {
        if (!loadConfigFromJson()) {
            // If JSON config fails, fall back to default setup
            Logger::warning("Failed to load config from JSON, using default setup");
            setupMenu();
        }
    } else {
        // Use default menu setup
        setupMenu();
    }

    return true;
}
bool MicroPanel::loadConfigFromJson() {
    try {
        Logger::debug("Loading configuration from: " + m_config.configFile);

        // Open the file
        std::ifstream configFile(m_config.configFile);
        if (!configFile.is_open()) {
            Logger::error("Could not open config file: " + m_config.configFile);
            return false;
        }

        // Parse JSON
        json config = json::parse(configFile);

        // Check for persistent_data section
        if (config.contains("persistent_data") && config["persistent_data"].is_object()) {
            if (config["persistent_data"].contains("file_path") &&
                config["persistent_data"]["file_path"].is_string()) {
                // Override default persistent data file path
                m_config.persistentDataFile = config["persistent_data"]["file_path"].get<std::string>();
                Logger::info("Using persistent data file from config: " + m_config.persistentDataFile);

                // Initialize persistent storage with the new path
                initPersistentStorage();
            }
        }

        // Initial startup delay to make sure device is fully initialized
        usleep(Config::STARTUP_DELAY);
        std::cout << "Initializing display..." << std::endl;

        // Clear the display
        m_display->clear();
        usleep(Config::DISPLAY_CMD_DELAY * 15);

        // Draw startup message
        m_display->drawText(0, 0, "Menu System");
        usleep(Config::DISPLAY_CMD_DELAY * 10);

        m_display->drawText(0, 10, "Loading Config...");
        usleep(Config::DISPLAY_CMD_DELAY * 10);

        // Clear before showing menu
        m_display->clear();
        usleep(Config::DISPLAY_CMD_DELAY * 15);

        // Check if "modules" field exists and is an array
        if (!config.contains("modules") || !config["modules"].is_array()) {
            Logger::error("Config file doesn't contain valid 'modules' array");
            return false;
        }

        Logger::debug("Starting menu configuration processing");
        Logger::debug("Found " + std::to_string(config["modules"].size()) + " modules in config");

        // First pass: Create all menu modules
        for (const auto& module : config["modules"]) {
            // Check for required fields
            if (!module.contains("id") || !module.contains("title")) {
                Logger::warning("Skipping module with missing required field");
                continue;
            }

            // Get module properties
            std::string id = module["id"].get<std::string>();
            std::string title = module["title"].get<std::string>();
            bool enabled = module.contains("enabled") ? module["enabled"].get<bool>() : false;

            // Check if this is a menu type module
            bool isMenu = module.contains("type") && module["type"].get<std::string>() == "menu";

            // Check if this is a special action type module
            bool isAction = module.contains("type") && module["type"].get<std::string>() == "action";

            // Always create menu modules, regardless of enabled status
            if (isMenu) {
                Logger::debug("Creating menu module: " + id);
                auto menuModule = std::make_shared<MenuScreenModule>(m_display, m_inputDevice, id, title);

                // Add to module registry
                m_modules[id] = menuModule;

                // Add to main menu only if enabled
                if (enabled) {
                    registerModuleInMenu(id, title);
                    Logger::debug("Added menu module to main menu: " + id);
                }
            }
            // Handle action modules
            else if (isAction) {
                if (id == "invert_display" && enabled) {
                    m_mainMenu->addItem(std::make_shared<ActionMenuItem>(title, [this]() {
                        m_display->setInverted(!m_display->isInverted());
                    }));
                    Logger::debug("Added invert display action to main menu: " + title);
                }
            }
            // For regular modules, only add to main menu if enabled
            else if (enabled && m_modules.find(id) != m_modules.end()) {
                // Only add to menu if dependencies are satisfied (for non-menu modules)
                auto& dependencies = ModuleDependency::getInstance();
                if (dependencies.shouldSkipDependencyCheck(id) || dependencies.checkDependencies(id)) {
                    registerModuleInMenu(id, title);
                    Logger::debug("Registered module: " + id + " with title: " + title);
                } else {
                    Logger::warning("Module dependencies not satisfied: " + id);
                }
            }
        }

        // Second pass: Configure menu hierarchies
        for (const auto& module : config["modules"]) {
            // Check if this module has an ID and is a menu type
            if (module.contains("id") &&
                module.contains("type") &&
                module["type"].get<std::string>() == "menu") {

                std::string menuId = module["id"].get<std::string>();

                // Check if the menu module exists in our registry
                auto it = m_modules.find(menuId);
                if (it == m_modules.end() || !std::dynamic_pointer_cast<MenuScreenModule>(it->second)) {
                    Logger::warning("Menu module not found: " + menuId);
                    continue;
                }

                // Get the menu module
                auto menuModule = std::dynamic_pointer_cast<MenuScreenModule>(it->second);

                // Set the module registry so the menu can look up modules
                menuModule->setModuleRegistry(&m_modules);

                // Check if this menu has submenus
                if (module.contains("submenus") && module["submenus"].is_array()) {
                    // Add each submenu item
                    for (const auto& submenu : module["submenus"]) {
                        // Check for required fields
                        if (!submenu.contains("id") || !submenu.contains("title")) {
                            Logger::warning("Skipping submenu with missing required field");
                            continue;
                        }

                        // Get submenu properties
                        std::string submenuId = submenu["id"].get<std::string>();
                        std::string submenuTitle = submenu["title"].get<std::string>();

                        // Add to the menu without checking dependencies
                        menuModule->addSubmenuItem(submenuId, submenuTitle);
                        Logger::debug("Added submenu item " + submenuId + " to menu " + menuId);
                    }
                }
            }
        }

        // Special case for Invert Display option if it's in the options section
        if (config.contains("options") && config["options"].is_object()) {
            auto options = config["options"];
            if (options.contains("invert_display") && options["invert_display"].is_object()) {
                auto invertOpt = options["invert_display"];
                if (invertOpt.contains("enabled") && invertOpt["enabled"].get<bool>() &&
                    invertOpt.contains("title") && invertOpt["title"].is_string()) {
                    // Add the invert display option with custom title
                    std::string title = invertOpt["title"].get<std::string>();
                    m_mainMenu->addItem(std::make_shared<ActionMenuItem>(title, [this]() {
                        m_display->setInverted(!m_display->isInverted());
                    }));
                    Logger::debug("Added invert display option: " + title);
                }
            }
        }

        // Add Exit option at the end
        m_mainMenu->addItem(std::make_shared<ActionMenuItem>("Exit", [this]() {
            m_running = false;
        }));

        // Debug the menu state
        Logger::debug("Menu setup complete, about to render");

        // Force a display test
        m_display->clear();
        usleep(Config::DISPLAY_CMD_DELAY * 5);
        m_display->drawText(0, 20, "TESTING DISPLAY");
        usleep(Config::DISPLAY_CMD_DELAY * 20);

        // Initially render the menu
        m_mainMenu->render();
        Logger::debug("Menu render called");

        return true;
    } catch (const std::exception& e) {
        Logger::error("Error parsing JSON config: " + std::string(e.what()));
        return false;
    }
}
void MicroPanel::initializeModules()
{
    // Clear any existing modules
    m_modules.clear();

    // Create screen modules
    m_modules["hello"] = std::make_shared<HelloWorldScreen>(m_display, m_inputDevice);
    m_modules["counter"] = std::make_shared<CounterScreen>(m_display, m_inputDevice);
    m_modules["brightness"] = std::make_shared<BrightnessScreen>(m_display, m_inputDevice);
    m_modules["network"] = std::make_shared<NetworkInfoScreen>(m_display, m_inputDevice);
    m_modules["system"] = std::make_shared<SystemStatsScreen>(m_display, m_inputDevice);
    m_modules["internet"] = std::make_shared<InternetTestScreen>(m_display, m_inputDevice);
    m_modules["wifi"] = std::make_shared<WiFiSettingsScreen>(m_display, m_inputDevice);
    m_modules["ping"] = std::make_shared<IPPingScreen>(m_display, m_inputDevice);
    m_modules["netinfo"] = std::make_shared<NetInfoScreen>(m_display, m_inputDevice);
    m_modules["netsettings"] = std::make_shared<NetSettingsScreen>(m_display, m_inputDevice);
    //std::cout << "Module initialization complete" << std::endl;
    Logger::debug("Module initialization complete - " + std::to_string(m_modules.size()) + " modules available");
}

// New helper method to register a module in the menu
void MicroPanel::registerModuleInMenu(const std::string& moduleName, const std::string& menuTitle) {
    m_mainMenu->addItem(std::make_shared<ActionMenuItem>(menuTitle, [this, moduleName]() {
        std::cout << "Executing action for module: " << moduleName << std::endl;
        auto module = std::dynamic_pointer_cast<ScreenModule>(m_modules[moduleName]);
        if (module) {
            module->run();
            // Explicitly redraw the main menu when returning
            m_display->clear();
            usleep(Config::DISPLAY_CMD_DELAY * 5);
            m_mainMenu->render();
        } else {
            Logger::error("Failed to execute module: " + moduleName);
        }
    }));
}

void MicroPanel::setupMenu()
{
    // Initial startup delay to make sure device is fully initialized
    usleep(Config::STARTUP_DELAY);
    std::cout << "Initializing display..." << std::endl;

    // Clear the display
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 15);

    // Draw startup message
    m_display->drawText(0, 0, "Menu System");
    usleep(Config::DISPLAY_CMD_DELAY * 10);

    m_display->drawText(0, 10, "Initializing...");
    usleep(Config::DISPLAY_CMD_DELAY * 10);

    // Clear before showing menu
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 15);

    registerModuleInMenu("brightness", "Brightness");
    registerModuleInMenu("network", "Net Settings");
    registerModuleInMenu("system", "System Stats");
    registerModuleInMenu("internet", "Test Internet");
    registerModuleInMenu("wifi", "WiFi Settings");
    registerModuleInMenu("ping", "IP Ping");
    registerModuleInMenu("netinfo", "Net Info");
    registerModuleInMenu("netsettings", "Net Settings");

    // Add Exit option at the end
    m_mainMenu->addItem(std::make_shared<ActionMenuItem>("Exit", [this]() {
        m_running = false;
    }));

    // Initially render the menu
    m_mainMenu->render();
}

void MicroPanel::run()
{
    // Start disconnection monitor
    m_deviceManager->startDisconnectionMonitor();

    // Set running flag
    m_running = true;

    // Main event loop
    struct timeval lastBufferFlush = {0, 0};
    struct timeval now;

    while (m_running) {
        // Check if device was disconnected
        if (m_deviceManager->isDeviceDisconnected() ||
            m_display->isDisconnected()) {
            std::cout << "Device disconnection detected!" << std::endl;

            if (m_config.autoDetect) {
                // For auto-detect mode, try to reconnect
                std::cout << "Attempting to reconnect..." << std::endl;

                // Clean up current devices
                if (m_inputDevice) {
                    m_inputDevice->close();
                }

                if (m_displayDevice) {
                    m_displayDevice->close();
                }

                // Stop the disconnection monitor while we wait for reconnection
                m_deviceManager->stopDisconnectionMonitor();

                // Wait for device to reconnect with a timeout
                bool reconnected = m_deviceManager->monitorDeviceUntilConnected(m_running);

                if (reconnected) {
                    std::cout << "Successfully reconnected to device!" << std::endl;

                    // Get new device paths
                    auto devices = m_deviceManager->detectDevices();
                    if (!devices.first.empty() && !devices.second.empty()) {
                        // Update device paths
                        m_config.inputDevice = devices.first;
                        m_config.serialDevice = devices.second;

                        // Create and open new devices
                        m_inputDevice = std::make_shared<InputDevice>(m_config.inputDevice);
                        m_displayDevice = std::make_shared<DisplayDevice>(m_config.serialDevice);

                        if (m_inputDevice->open() && m_displayDevice->open()) {
                            std::cout << "Successfully opened reconnected devices" << std::endl;

                            // Update display and redraw menu
                            m_display = std::make_shared<Display>(m_displayDevice);
                            if (m_config.powerSaveEnabled) {
                                m_display->enablePowerSave(true);
                            }
                            // Reinitialize modules with new device handles
                            initializeModules();

                            // Reinitialize menu
                            m_mainMenu = std::make_shared<Menu>(m_display);
                            setupMenu();

                            // Restart the disconnection monitor
                            m_deviceManager->startDisconnectionMonitor();

                            // Continue with the main loop
                            continue;
                        } else {
                            std::cerr << "Failed to open reconnected devices" << std::endl;
                        }
                    } else {
                        std::cerr << "Failed to get device paths after reconnection" << std::endl;
                    }
                } else {
                    std::cerr << "Failed to reconnect to device" << std::endl;
                }
            }

            // Exit the loop if reconnection failed or is not enabled
            break;
        }

        // Process input
        if (m_inputDevice->waitForEvents(100) > 0) {
            m_inputDevice->processEvents(
                [this](int direction) {
                    // Handle rotation
                    m_mainMenu->handleRotation(direction);
                },
                [this]() {
                    // Handle button press
                    m_mainMenu->handleButtonPress();
                }
            );
        }

        // Check for power save timeout
        if (m_config.powerSaveEnabled) {
            m_display->checkPowerSaveTimeout();
        }

        // Update timestamp for periodic operations
        gettimeofday(&now, nullptr);

        // Calculate time since last buffer flush
        long timeSinceFlush = (now.tv_sec - lastBufferFlush.tv_sec) * 1000 +
                             (now.tv_usec - lastBufferFlush.tv_usec) / 1000;

        // Flush command buffer at regular intervals
        if (timeSinceFlush > Config::CMD_BUFFER_FLUSH_INTERVAL) {
            m_displayDevice->flushBuffer();
            lastBufferFlush = now;
        }

        // Short delay to reduce CPU usage
        usleep(Config::MAIN_LOOP_DELAY);
    }
}
void MicroPanel::shutdown()
{
    // Stop disconnection monitor
    m_deviceManager->stopDisconnectionMonitor();
    
    // Display shutdown message
    if (m_display && m_displayDevice->isOpen()) {
        m_display->clear();
        usleep(Config::DISPLAY_CMD_DELAY);
        //m_display->drawText(0, 0, "Daemon stopped");
        m_display->drawText(0, 0, "Rebooting.....");
        usleep(Config::DISPLAY_CMD_DELAY * 10);
    }
    
    // Close devices
    if (m_inputDevice) {
        m_inputDevice->close();
    }
    
    if (m_displayDevice) {
        m_displayDevice->close();
    }
    
    // Clear module registry
    m_modules.clear();
    
    // Clear menu
    if (m_mainMenu) {
        m_mainMenu->clear();
    }
    
    std::cout << "MicroPanel shutdown complete" << std::endl;
}

// Main entry point
int main(int argc, char* argv[])
{
    MicroPanel app(argc, argv);
    
    if (!app.initialize()) {
        return EXIT_FAILURE;
    }
    
    app.run();
    app.shutdown();
    
    return EXIT_SUCCESS;
}


// Initialize persistent storage
bool MicroPanel::initPersistentStorage() {
    if (m_config.persistentDataFile.empty()) {
        Logger::warning("No persistent data file specified");
        return false;
    }
    
    auto& storage = PersistentStorage::getInstance();
    return storage.initialize(m_config.persistentDataFile);
}

// Load module dependencies from JSON configuration
bool MicroPanel::loadModuleDependencies() {
    try {
        // Open the file
        std::ifstream configFile(m_config.configFile);
        if (!configFile.is_open()) {
            Logger::error("Could not open config file: " + m_config.configFile);
            return false;
        }
        
        // Parse JSON
        json config = json::parse(configFile);
        
        // Load dependencies
        auto& dependencies = ModuleDependency::getInstance();
        return dependencies.loadDependencies(config);
    } catch (const std::exception& e) {
        Logger::error("Error loading module dependencies: " + std::string(e.what()));
        return false;
    }
}
