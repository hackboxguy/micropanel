#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sys/time.h>

// Forward declarations
class BaseDisplayDevice;
class DisplayDevice;
class InputDevice;
class MultiInputDevice;
class Display;
class DeviceManager;
class Menu;
class MenuItem;
class ScreenModule;

/**
 * Main application class
 */
class MicroPanel {
public:
    MicroPanel(int argc, char* argv[]);
    ~MicroPanel();

    bool initialize();
    void run();
    void shutdown();

private:
    void parseCommandLine(int argc, char* argv[]);
    void setupSignalHandlers();
    void initializeModules();
    void setupMenu();
    bool detectAndOpenDevices();
    void mainEventLoop();

    // For auto-detect mode
    void detectAndRun();
    bool loadConfigFromJson();
    void registerModuleInMenu(const std::string& moduleName, const std::string& menuTitle);
    // New methods for persistence and dependencies
    bool initPersistentStorage();
    bool loadModuleDependencies();
    void runModuleWithGPIOInput(std::shared_ptr<ScreenModule> module);
    void simulateRotationForModule(std::shared_ptr<ScreenModule> module, int direction);
    void simulateButtonPressForModule(std::shared_ptr<ScreenModule> module,bool& moduleRunning);
    void configureMenuModuleGPIO(std::shared_ptr<class MenuScreenModule> menuModule);

    struct {
        std::string inputDevice;
        std::string serialDevice;
        std::string configFile;
        std::string persistentDataFile;  // Path to store persistent data
        bool verboseMode = false;
        bool autoDetect = false;
        bool powerSaveEnabled = false;
        bool useGPIOMode = false;
    } m_config;

    std::shared_ptr<DisplayDevice> m_displayDevice;
    std::shared_ptr<InputDevice> m_inputDevice;
    std::shared_ptr<MultiInputDevice> m_multiInputDevice;
    std::shared_ptr<Display> m_display;
    std::shared_ptr<DeviceManager> m_deviceManager;
    std::shared_ptr<Menu> m_mainMenu;

    // Application state
    std::atomic<bool> m_running{false};

    // Module registry
    std::map<std::string, std::shared_ptr<ScreenModule>> m_modules;

    // Signal handling
    static std::atomic<MicroPanel*> s_instance;
    static void signalHandler(int signal);
    std::shared_ptr<BaseDisplayDevice> createDisplayDevice(const std::string& devicePath);
    std::shared_ptr<BaseDisplayDevice> m_baseDisplayDevice;
};
