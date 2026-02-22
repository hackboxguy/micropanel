#include "MicroPanel.h"
#include "Config.h"
#include "DeviceInterfaces.h"
#include "MultiInputDevice.h"
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
std::atomic<MicroPanel*> MicroPanel::s_instance{nullptr};
extern std::atomic<bool> g_signalReceived;

// Signal handler function
void MicroPanel::signalHandler(int signal)
{
   (void)signal;
   MicroPanel* inst = s_instance.load();
   if (inst) {
        inst->m_running.store(false);
    }
   g_signalReceived.store(true);
}

MicroPanel::MicroPanel(int argc, char* argv[])
{
    // Set static instance for signal handler
    s_instance.store(this);
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
                if (std::string(optarg) == "gpio") {
                    m_config.useGPIOMode = true;
                    Logger::info("GPIO multi-device mode enabled");
                }
                // Note: Don't disable auto-detect here - let -a flag control it for hybrid mode
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
                std::cout << "              Use 'gpio' for GPIO button auto-detection\n";
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

    // If auto-detect is enabled, try device detection with optional fallback
    if (m_config.autoDetect) {
        std::pair<std::string, std::string> devices;

        // Check if fallback devices were provided via command line
        bool hasFallbackDevices = !m_config.inputDevice.empty() && !m_config.serialDevice.empty();

        if (hasFallbackDevices) {
            // Hybrid mode: Try USB first, fallback to provided devices
            std::cout << "Hybrid detection mode: USB HID priority with GPIO/I2C fallback..." << std::endl;
            devices = m_deviceManager->detectDevicesWithFallback(m_config.inputDevice, m_config.serialDevice);
        } else {
            // Pure auto-detect mode: Wait for USB HID device
            std::cout << "Waiting for HMI device to be connected..." << std::endl;

            // First check if the device is already connected
            devices = m_deviceManager->detectDevices();
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

            if (devices.first.empty() || devices.second.empty()) {
                std::cerr << "Failed to auto-detect devices" << std::endl;
                return false;
            }
        }

        // Update config with detected/fallback devices
        m_config.inputDevice = devices.first;
        m_config.serialDevice = devices.second;

        // IMPORTANT: If we detected a real USB input device (not GPIO fallback), disable GPIO mode
        if (devices.first != "gpio" && devices.first.find("/dev/input/event") == 0) {
            m_config.useGPIOMode = false;
            std::cout << "USB input device detected, disabling GPIO mode" << std::endl;
        }

        std::cout << "Using input device: " << m_config.inputDevice << std::endl;
        std::cout << "Using serial device: " << m_config.serialDevice << std::endl;
    }


    // Initialize devices
    //m_displayDevice = std::make_shared<DisplayDevice>(m_config.serialDevice);
    //m_inputDevice = std::make_shared<InputDevice>(m_config.inputDevice);
    // Open devices
    //if (!m_inputDevice->open()) {
    //    std::cerr << "Failed to open input device: " << m_config.inputDevice << std::endl;
    //    return false;
    //}

    // Initialize devices
    //m_displayDevice = std::make_shared<DisplayDevice>(m_config.serialDevice);
    m_baseDisplayDevice = createDisplayDevice(m_config.serialDevice);

    // NEW: Handle GPIO mode vs traditional mode
    // Use GPIO only if explicitly requested AND no USB device was detected
    bool shouldUseGPIO = false;

    if (m_config.useGPIOMode) {
        // GPIO mode was requested, but check if we detected a USB device instead
        if (m_config.inputDevice == "gpio" || m_config.inputDevice.find("/dev/input/event") != 0) {
            // No USB device detected, use GPIO
            shouldUseGPIO = true;
        } else {
            // USB device detected, ignore GPIO request and use USB
            shouldUseGPIO = false;
            std::cout << "USB device detected, overriding GPIO mode request" << std::endl;
        }
    }

    Logger::info("Input mode decision: shouldUseGPIO=" + std::string(shouldUseGPIO ? "true" : "false") +
                 ", inputDevice='" + m_config.inputDevice + "', useGPIOMode=" + std::string(m_config.useGPIOMode ? "true" : "false"));

    if (shouldUseGPIO) {
    std::cout << "Initializing GPIO multi-device mode..." << std::endl;

    // Create MultiInputDevice for GPIO buttons
    m_multiInputDevice = std::make_unique<MultiInputDevice>();
    if (!m_multiInputDevice->open()) {
        std::cerr << "Failed to open GPIO input devices. Make sure GPIO buttons are configured." << std::endl;
        std::cerr << "Check: dmesg | grep button" << std::endl;
        return false;
    }

    // Create dummy InputDevice for modules that expect it
    //m_inputDevice = std::make_shared<InputDevice>("/dev/null");
    // Create a properly working InputDevice for modules
    // Use the first available input device or event0 as fallback
    std::string fallbackDevice = "/dev/input/event0";
    if (!m_config.inputDevice.empty() && m_config.inputDevice != "gpio") {
        fallbackDevice = m_config.inputDevice;
    }
    m_inputDevice = std::make_shared<InputDevice>(fallbackDevice);
    if (!m_inputDevice->open()) {
        std::cout << "Warning: Failed to open fallback input device for modules" << std::endl;
    }
    } else {
    // Traditional mode
    m_inputDevice = std::make_shared<InputDevice>(m_config.inputDevice);
    if (!m_inputDevice->open()) {
        std::cerr << "Failed to open input device: " << m_config.inputDevice << std::endl;
        return false;
    }
    }


    if (!m_baseDisplayDevice->open()) {
        std::cerr << "Failed to open display device: " << m_config.serialDevice << std::endl;
        if (m_inputDevice)
            m_inputDevice->close();
        return false;
    }

    // Create display wrapper
    //m_display = std::make_shared<Display>(m_displayDevice);
    m_display = std::make_shared<Display>(m_baseDisplayDevice);

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
                Logger::debug("Using persistent data file from config: " + m_config.persistentDataFile);

                // Initialize persistent storage with the new path
                initPersistentStorage();
            }
        }

        // Initial startup delay to make sure device is fully initialized
        usleep(Config::STARTUP_DELAY);
        Logger::debug("Initializing display...");

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

            // Get the module type if specified
            std::string moduleType = module.contains("type") ? module["type"].get<std::string>() : "";

            // Check if this is a menu type module
            bool isMenu = moduleType == "menu";

            // Check if this is a special action type module
            bool isAction = moduleType == "action";
            // Check if this is a GenericList type module
            bool isGenericList = moduleType == "GenericList";
            // Check if this is a textbox type module
            bool isTextBox = moduleType == "textbox";

            // Always create menu modules, regardless of enabled status
            if (isMenu) {
                Logger::debug("Creating menu module: " + id);
                auto menuModule = std::make_shared<MenuScreenModule>(m_display, m_inputDevice, id, title);

                // Add to module registry
                m_modules[id] = menuModule;

                // CRITICAL: Set GPIO handler for ALL menu modules, not just enabled ones
                configureMenuModuleGPIO(menuModule);

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
            // Handle GenericList modules
            else if (isGenericList) {
                Logger::debug("Creating GenericList module: " + id);
                // Create a new GenericListScreen instance for this module
                auto genericListModule = std::make_shared<GenericListScreen>(m_display, m_inputDevice);
                genericListModule->setId(id);
                genericListModule->setConfig(module);
                // Add to module registry
                m_modules[id] = genericListModule;
                // Add to main menu only if enabled
                if (enabled) {
                    registerModuleInMenu(id, title);
                    Logger::debug("Added GenericList module to main menu: " + id);
                }
            }
            // Handle textbox modules
            else if (isTextBox) {
                Logger::debug("Creating textbox module: " + id);
                // Create a new TextBoxScreen instance for this module
                auto textboxModule = std::make_shared<TextBoxScreen>(m_display, m_inputDevice);
                textboxModule->setId(id);
                // Add to module registry
                m_modules[id] = textboxModule;
                // Add to main menu only if enabled
                if (enabled) {
                    registerModuleInMenu(id, title);
                    Logger::debug("Added textbox module to main menu: " + id);
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

        // Mark top-level menu modules
        for (const auto& modulePair : m_modules) {
        auto menuModule = std::dynamic_pointer_cast<MenuScreenModule>(modulePair.second);
        if (menuModule) {
        // Check if this is a top-level menu (enabled and in the main menu)
        for (const auto& module : config["modules"]) {
            if (module.contains("id") && module["id"].get<std::string>() == modulePair.first &&
                module.contains("enabled") && module["enabled"].get<bool>()) {
                menuModule->setAsTopLevelMenu(true);
                Logger::debug("Marked " + modulePair.first + " as top-level menu");
                break;
            }
          }
        }
        }

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
    m_modules["textbox"] = std::make_shared<TextBoxScreen>(m_display, m_inputDevice);
    m_modules["internet"] = std::make_shared<InternetTestScreen>(m_display, m_inputDevice);
    m_modules["wifi"] = std::make_shared<WiFiSettingsScreen>(m_display, m_inputDevice);
    m_modules["ping"] = std::make_shared<IPPingScreen>(m_display, m_inputDevice);
    m_modules["netinfo"] = std::make_shared<NetInfoScreen>(m_display, m_inputDevice);
    m_modules["netsettings"] = std::make_shared<NetSettingsScreen>(m_display, m_inputDevice);
    m_modules["speedtest"] = std::make_shared<SpeedTestScreen>(m_display, m_inputDevice);
    //m_modules["throughputtest"] = std::make_shared<ThroughputTestScreen>(m_display, m_inputDevice);
    m_modules["throughputserver"] = std::make_shared<ThroughputServerScreen>(m_display, m_inputDevice);
    m_modules["throughputclient"] = std::make_shared<ThroughputClientScreen>(m_display, m_inputDevice);
    Logger::debug("Module initialization complete - " + std::to_string(m_modules.size()) + " modules available");
}

void MicroPanel::registerModuleInMenu(const std::string& moduleName, const std::string& menuTitle) {
    m_mainMenu->addItem(std::make_shared<ActionMenuItem>(menuTitle, [this, moduleName]() {
        Logger::debug("Executing action for module: " + moduleName);
        auto module = std::dynamic_pointer_cast<ScreenModule>(m_modules[moduleName]);
        if (module) {
            // Clear main menu flag if this is a menu module
            auto menuModule = std::dynamic_pointer_cast<MenuScreenModule>(module);
            if (menuModule) {
                menuModule->clearMainMenuFlag();

                // Configure menu module for GPIO mode
                configureMenuModuleGPIO(menuModule);
            }

            // Use GPIO input for ALL module execution in GPIO mode
            if (m_config.useGPIOMode) {
                runModuleWithGPIOInput(module);
            } else {
                module->run();
            }

            // Explicitly redraw the main menu when returning
            m_display->clear();
            usleep(Config::DISPLAY_CMD_DELAY * 5);
            m_mainMenu->render();
        } else {
            Logger::error("Failed to execute module: " + moduleName);
        }
    }));
}

void MicroPanel::configureMenuModuleGPIO(std::shared_ptr<MenuScreenModule> menuModule) {
    if (m_config.useGPIOMode) {
        menuModule->setUseGPIOMode(true);
        menuModule->setGPIOHandler([this](std::shared_ptr<ScreenModule> submodule) {
            this->runModuleWithGPIOInput(submodule);
        });
    }
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
    //m_mainMenu->addItem(std::make_shared<ActionMenuItem>("Exit", [this]() {
    //    m_running = false;
    //}));

    // Initially render the menu
    m_mainMenu->render();
}

void MicroPanel::run()
{
    // Check if we're using I2C mode
    bool isI2CMode = (m_config.serialDevice.find("/dev/i2c-") == 0);

    // Only start disconnection monitor for USB/serial devices, not I2C
    if (!isI2CMode) {
        std::cout << "Starting USB device disconnection monitor" << std::endl;
        m_deviceManager->startDisconnectionMonitor();
    } else {
        Logger::debug("I2C mode detected - skipping USB disconnection monitoring");
    }

    // Set running flag
    m_running = true;

    // Main event loop
    struct timeval lastBufferFlush = {0, 0};
    struct timeval now;

    while (m_running) {
        // Check if device was disconnected - ONLY for non-I2C devices
        if (!isI2CMode && (m_deviceManager->isDeviceDisconnected() ||
            (m_baseDisplayDevice && m_baseDisplayDevice->isDisconnected()))) {
            std::cout << "USB device disconnection detected!" << std::endl;

            if (m_config.autoDetect) {
                // For auto-detect mode, try to reconnect
                std::cout << "Attempting to reconnect..." << std::endl;

                // Clean up current devices
                if (m_inputDevice) {
                    m_inputDevice->close();
                }

                if (m_baseDisplayDevice) {
                    m_baseDisplayDevice->close();
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
                        m_baseDisplayDevice = createDisplayDevice(m_config.serialDevice);

                        if (m_inputDevice->open() && m_baseDisplayDevice->open()) {
                            std::cout << "Successfully opened reconnected devices" << std::endl;

                            // Update display and redraw menu
                            m_display = std::make_shared<Display>(m_baseDisplayDevice);
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

        // Process input (handle GPIO vs traditional mode)
        if (m_config.useGPIOMode) {
            // Use MultiInputDevice for GPIO mode
            if (m_multiInputDevice && m_multiInputDevice->waitForEvents(100) > 0) {
                m_multiInputDevice->processEvents(
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
        } else {
            // Use traditional InputDevice
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

        // Flush command buffer at regular intervals - only for devices that support buffering
        if (timeSinceFlush > Config::CMD_BUFFER_FLUSH_INTERVAL) {
            // Only flush buffer for serial devices, I2C doesn't use buffering
            if (m_baseDisplayDevice && !isI2CMode) {
                m_baseDisplayDevice->flushBuffer();
            }
            lastBufferFlush = now;
        }

        // Short delay to reduce CPU usage
        usleep(Config::MAIN_LOOP_DELAY);
    }
}

void MicroPanel::shutdown()
{
    // Make shutdown idempotent - only run once
    if (m_shutdownDone) {
        return;
    }
    m_shutdownDone = true;

    // Stop disconnection monitor - only if it was started
    if (m_deviceManager) {
        bool isI2CMode = (m_config.serialDevice.find("/dev/i2c-") == 0);
        if (!isI2CMode) {
            m_deviceManager->stopDisconnectionMonitor();
        }
    }

    // Display shutdown message
    if (m_display && m_baseDisplayDevice && m_baseDisplayDevice->isOpen()) {
        m_display->clear();
        usleep(Config::DISPLAY_CMD_DELAY);
        m_display->drawText(0, 0, "Rebooting.....");
        usleep(Config::DISPLAY_CMD_DELAY * 10);
    }

    // Close devices
    if (m_inputDevice) {
        m_inputDevice->close();
    }

    if (m_baseDisplayDevice) {
        m_baseDisplayDevice->close();
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


void MicroPanel::runModuleWithGPIOInput(std::shared_ptr<ScreenModule> module) {
    Logger::debug("Running module with GPIO input...");

    // Check what type of module this is
    auto menuModule = std::dynamic_pointer_cast<MenuScreenModule>(module);
    auto brightnessModule = std::dynamic_pointer_cast<BrightnessScreen>(module);
    auto netInfoModule = std::dynamic_pointer_cast<NetInfoScreen>(module);
    auto pingModule = std::dynamic_pointer_cast<IPPingScreen>(module);
    auto netSettingsModule = std::dynamic_pointer_cast<NetSettingsScreen>(module);
    auto wifiSettingsModule = std::dynamic_pointer_cast<WiFiSettingsScreen>(module);
    auto textboxModule = std::dynamic_pointer_cast<TextBoxScreen>(module);

    if (menuModule) {
        Logger::debug("Module type: MenuScreenModule (ID: " + menuModule->getModuleId() + ")");
    } else if (brightnessModule) {
        Logger::debug("Module type: BrightnessScreen");
    } else if (netInfoModule) {
        Logger::debug("Module type: NetInfoScreen");
    } else if (pingModule) {
        Logger::debug("Module type: IPPingScreen");
    } else if (netSettingsModule) {
        Logger::debug("Module type: NetSettingsScreen");
    } else if (wifiSettingsModule) {
        Logger::debug("Module type: WiFiSettingsScreen");
    } else if (textboxModule) {
        Logger::debug("Module type: TextBoxScreen (ID: " + textboxModule->getModuleId() + ")");
    } else {
        Logger::debug("Module type: Generic ScreenModule");
    }

    // Enter the module
    Logger::debug("Entering module...");
    module->enter();
    Logger::debug("Module entered successfully");

    bool moduleRunning = true;
    int loopCount = 0;

    while (moduleRunning && m_running) {
        loopCount++;
        if (loopCount % 100 == 0) {  // Log every 100 loops to avoid spam
            Logger::debug("Module loop: " + std::to_string(loopCount));
        }

        // Process GPIO input
        if (m_multiInputDevice && m_multiInputDevice->waitForEvents(100) > 0) {
            m_multiInputDevice->processEvents(
                [&](int direction) {
                    Logger::debug("GPIO rotation in module: " + std::to_string(direction));
                    simulateRotationForModule(module, direction);
                },
                [&]() {
                    Logger::debug("GPIO button press in module");

                    // Handle button press differently for different module types
                    if (menuModule) {
                        // For menu modules, simulate button press to trigger menu selection
                        Logger::debug("Processing menu button press");
                        simulateButtonPressForModule(module, moduleRunning);
                    } else if (netInfoModule) {
                        // For NetInfoScreen, use its GPIO button handler
                        Logger::debug("Processing NetInfoScreen button press");
                        if (!netInfoModule->handleGPIOButtonPress()) {
                            moduleRunning = false;
                        }
                    } else if (pingModule) {
                        Logger::debug("Processing IPPingScreen button press");
                        if (!pingModule->handleGPIOButtonPress()) {
                            moduleRunning = false;
                        }
                    } else if (netSettingsModule) {
                        Logger::debug("Processing NetSettingsScreen button press");
                        if (!netSettingsModule->handleGPIOButtonPress()) {
                            moduleRunning = false;
                        }
                    } else if (wifiSettingsModule) {
                        Logger::debug("Processing WiFiSettingsScreen button press");
                        if (!wifiSettingsModule->handleGPIOButtonPress()) {
                            moduleRunning = false;
                        }
                    } else if (textboxModule) {
                        Logger::debug("Processing TextBoxScreen button press");
                        if (!textboxModule->handleGPIOButtonPress()) {
                            moduleRunning = false;
                        }
                    } else {
                        // Check for ThroughputServerScreen
                        auto throughputServerModule = std::dynamic_pointer_cast<ThroughputServerScreen>(module);
                        if (throughputServerModule) {
                            Logger::debug("Processing ThroughputServerScreen button press");
                            if (!throughputServerModule->handleGPIOButtonPress()) {
                                moduleRunning = false;
                            }
                        }
                        // Check for ThroughputClientScreen
                        else {
                            auto throughputClientModule = std::dynamic_pointer_cast<ThroughputClientScreen>(module);
                            if (throughputClientModule) {
                                Logger::debug("Processing ThroughputClientScreen button press");
                                if (!throughputClientModule->handleGPIOButtonPress()) {
                                    moduleRunning = false;
                                }
                            }
                            // For non-menu modules, check if it's GenericListScreen
                            else {
                                auto genericListModule = std::dynamic_pointer_cast<GenericListScreen>(module);
                                if (genericListModule) {
                                    Logger::debug("Processing GenericListScreen button press");
                                    if (!genericListModule->handleGPIOButtonPress()) {
                                        moduleRunning = false;
                                    }
                                } else {
                                    // For other non-menu modules, button press exits
                                    Logger::debug("Exiting non-menu module");
                                    moduleRunning = false;
                                }
                            }
                        }
                    }
                }
            );
        }

        // Update the module (this is important for display updates)
        try {
            module->update();
        } catch (const std::exception& e) {
            std::cout << "Exception in module update: " << e.what() << std::endl;
            moduleRunning = false;
        }

        // For TextBoxScreen, also call handleInput() to enable periodic refresh
        if (textboxModule) {
            try {
                if (!textboxModule->handleInput()) {
                    moduleRunning = false;
                }
            } catch (const std::exception& e) {
                std::cout << "Exception in TextBoxScreen handleInput: " << e.what() << std::endl;
                moduleRunning = false;
            }
        }

        // Small delay
        usleep(Config::MAIN_LOOP_DELAY);
    }

    // Exit the module
    Logger::debug("Exiting module...");
    module->exit();
    Logger::debug("Module exited successfully");
}

// NEW: Helper method to simulate rotation for different module types
void MicroPanel::simulateRotationForModule(std::shared_ptr<ScreenModule> module, int direction) {
    // Check for IPPingScreen
    auto pingModule = std::dynamic_pointer_cast<IPPingScreen>(module);
    if (pingModule) {
       Logger::debug("SUCCESS: IPPingScreen - calling handleGPIORotation");
       pingModule->handleGPIORotation(direction);
       return;
    }

    // Check for NetInfoScreen
    auto netInfoModule = std::dynamic_pointer_cast<NetInfoScreen>(module);
    if (netInfoModule) {
       Logger::debug("SUCCESS: NetInfoScreen - calling handleGPIORotation");
       netInfoModule->handleGPIORotation(direction);
       return;
    }

    // Handle MenuScreenModule - this covers ALL menu-type modules!
    auto menuModule = std::dynamic_pointer_cast<MenuScreenModule>(module);
    if (menuModule) {
        Logger::debug("Navigating menu: " + std::to_string(direction));
        menuModule->handleGPIORotation(direction);  // This will actually work now!
        return;
    }

    // For BrightnessScreen - adjust brightness directly
    auto brightnessModule = std::dynamic_pointer_cast<BrightnessScreen>(module);
    if (brightnessModule) {
        Logger::debug("Adjusting brightness: " + std::to_string(direction));
        int currentBrightness = m_display->getBrightness();

        if (direction < 0) {
            // Decrease brightness (UP button)
            currentBrightness -= 10;
            if (currentBrightness < 0) currentBrightness = 0;
        } else {
            // Increase brightness (DOWN button)
            currentBrightness += 10;
            if (currentBrightness > 255) currentBrightness = 255;
        }

        // Calculate percentage (0-255 -> 0-100%)
        int percentage = (currentBrightness * 100) / 255;

        // Clear the previous text area first
        m_display->drawText(50, 20, "    ");
        usleep(Config::DISPLAY_CMD_DELAY);

        // Format and draw new value
        char text[16];
        snprintf(text, sizeof(text), "%d%%", percentage);
        m_display->drawText(50, 20, text);
        usleep(Config::DISPLAY_CMD_DELAY);

        // Update progress bar
        m_display->drawProgressBar(10, 30, 108, 15, percentage);

        // Set the actual display brightness
        m_display->setBrightness(currentBrightness);

        return;
    }

    // Add support for ThroughputServerScreen
    auto throughputServerModule = std::dynamic_pointer_cast<ThroughputServerScreen>(module);
    if (throughputServerModule) {
        Logger::debug("SUCCESS: ThroughputServerScreen - calling handleGPIORotation");
        throughputServerModule->handleGPIORotation(direction);
        return;
    }

    // Add support for ThroughputClientScreen
    auto throughputClientModule = std::dynamic_pointer_cast<ThroughputClientScreen>(module);
    if (throughputClientModule) {
        Logger::debug("SUCCESS: ThroughputClientScreen - calling handleGPIORotation");
        throughputClientModule->handleGPIORotation(direction);
        return;
    }

    // Add support for NetSettingsScreen
    auto netSettingsModule = std::dynamic_pointer_cast<NetSettingsScreen>(module);
    if (netSettingsModule) {
        Logger::debug("SUCCESS: NetSettingsScreen - calling handleGPIORotation");
        netSettingsModule->handleGPIORotation(direction);
        return;
    }

    // Add support for WiFiSettingsScreen
    auto wifiSettingsModule = std::dynamic_pointer_cast<WiFiSettingsScreen>(module);
    if (wifiSettingsModule) {
        Logger::debug("SUCCESS: WiFiSettingsScreen - calling handleGPIORotation");
        wifiSettingsModule->handleGPIORotation(direction);
        return;
    }

    // Add support for GenericListScreen modules (insert before the default case)
    auto genericListModule = std::dynamic_pointer_cast<GenericListScreen>(module);
    if (genericListModule) {
       Logger::debug("Navigating GenericListScreen: " + std::to_string(direction));
       genericListModule->handleGPIORotation(direction);
       return;
    }
}
void MicroPanel::simulateButtonPressForModule(std::shared_ptr<ScreenModule> module, bool& moduleRunning) {
    // Check for NetInfoScreen first
    auto netInfoModule = std::dynamic_pointer_cast<NetInfoScreen>(module);
    if (netInfoModule) {
        std::cout << "NetInfoScreen button press - handling selection" << std::endl;
        if (!netInfoModule->handleGPIOButtonPress()) {
            moduleRunning = false; // Exit if method returns false
        }
        return;
    }

    auto menuModule = std::dynamic_pointer_cast<MenuScreenModule>(module);
    if (menuModule) {
        Logger::debug("Simulating menu button press - selecting menu item");
        if (!menuModule->handleGPIOButtonPress()) {
            moduleRunning = false; // Exit if menu returns false
        }
        return;
    }

    auto genericListModule = std::dynamic_pointer_cast<GenericListScreen>(module);
    if (genericListModule) {
        std::cout << "GenericListScreen button press - selecting item" << std::endl;
        if (!genericListModule->handleGPIOButtonPress()) {
            // If the method returns false, exit the module
            moduleRunning = false;
        }
        return;
    }

    // For other module types, button press typically exits
    std::cout << "Button press for non-menu module" << std::endl;
    moduleRunning = false;
}
std::shared_ptr<BaseDisplayDevice> MicroPanel::createDisplayDevice(const std::string& devicePath) {
    std::cout << "Creating display device for: " << devicePath << std::endl;

    // Detect device type based on path
    if (devicePath.find("/dev/i2c-") == 0) {
        //std::cout << "I2C display detected but not yet implemented: " << devicePath << std::endl;
        //std::cout << "Falling back to serial mode for now" << std::endl;
        //return std::make_shared<DisplayDevice>("/dev/ttyACM0"); // Fallback

        // I2C device path detected
        std::cout << "Detected I2C display device: " << devicePath << std::endl;
        return std::make_shared<I2CDisplayDevice>(devicePath);
    }
    else if (devicePath.find("/dev/ttyACM") == 0 || devicePath.find("/dev/ttyUSB") == 0) {
        // Serial device path detected
        std::cout << "Detected serial display device: " << devicePath << std::endl;
        return std::make_shared<DisplayDevice>(devicePath);
    }
    else {
        // Default to serial for backward compatibility
        std::cout << "Unknown device type, defaulting to serial: " << devicePath << std::endl;
        return std::make_shared<DisplayDevice>(devicePath);
    }
}
