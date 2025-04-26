#include "ScreenModules.h"
#include "MenuSystem.h"
#include "DeviceInterfaces.h"
#include "ModuleDependency.h"
#include "Config.h"
#include "Logger.h"
#include "IPSelector.h"
#include <iostream>
#include <unistd.h>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>

// Script path for network settings
//#define NET_SETTINGS_SCRIPT "/usr/bin/dhcp-net-settings.sh"

// Menu states - define which menu is currently active
enum class NetSettingsMenuState {
    MENU_MAIN,      // Main network settings menu
    MENU_MODE,      // Mode selection submenu
    MENU_IP,        // IP address edit submenu
    MENU_GATEWAY,   // Gateway edit submenu
    MENU_NETMASK    // Netmask edit submenu
};

// Main menu items
enum class MainMenuSelection {
    MAIN_MODE,      // Mode selection
    MAIN_IP,        // IP address
    MAIN_GATEWAY,   // Gateway
    MAIN_NETMASK,   // Netmask
    MAIN_APPLY,     // Apply settings
    MAIN_EXIT,      // Exit
    MAIN_ITEM_COUNT // Count of main menu items
};

// Mode selection options
enum class ModeMenuSelection {
    MODE_STATIC,    // Static IP configuration
    MODE_DHCP,      // DHCP configuration
    MODE_BACK,      // Back to main menu
    MODE_ITEM_COUNT // Count of mode menu items
};

// Address menu options
enum class AddrMenuSelection {
    ADDR_IP,        // IP address field
    ADDR_BACK,      // Back to main menu
    ADDR_ITEM_COUNT // Count of address menu items
};

// Network configuration mode
enum class NetworkMode {
    NET_MODE_STATIC, // Static IP
    NET_MODE_DHCP    // DHCP
};

// Implementation class for NetSettingsScreen
class NetSettingsScreen::Impl {
public:
    Impl(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input);
    ~Impl();

    // Interface methods
    void refreshSettings();
    void applyNetworkSettings();
    bool initNetworkSettingsFromScript();
    void networkFieldChanged(const std::string& ip);
    std::string getNetSettingsScriptPath();
    std::string getNetSettingsOsType();
    std::string getNetSettingsInterface();
    // Menu state management
    void switchToMainMenu();
    void switchToModeMenu();
    void switchToIpMenu();
    void switchToGatewayMenu();
    void switchToNetmaskMenu();

    // Button handlers
    void handleMainMenuButton();
    void handleModeMenuButton();
    void handleAddrMenuButton();

    // Drawing methods
    void drawMainMenu(bool fullRedraw);
    void updateMainMenuSelection(int oldSelection, int newSelection);
    void drawModeMenu(bool fullRedraw);
    void updateModeMenuSelection(int oldSelection, int newSelection);
    void drawIpMenu(bool fullRedraw);
    void drawGatewayMenu(bool fullRedraw);
    void drawNetmaskMenu(bool fullRedraw);
    void updateAddrMenuSelection(AddrMenuSelection oldSelection, AddrMenuSelection newSelection, IPSelector* selector);

    // State tracking
    NetSettingsMenuState m_menuState = NetSettingsMenuState::MENU_MAIN;
    int m_mainSelection = static_cast<int>(MainMenuSelection::MAIN_MODE);
    int m_prevMainSelection = static_cast<int>(MainMenuSelection::MAIN_MODE);
    int m_modeSelection = static_cast<int>(ModeMenuSelection::MODE_STATIC);
    int m_prevModeSelection = static_cast<int>(ModeMenuSelection::MODE_STATIC);
    AddrMenuSelection m_addrSelection = AddrMenuSelection::ADDR_IP;
    AddrMenuSelection m_prevAddrSelection = AddrMenuSelection::ADDR_IP;
    NetworkMode m_mode = NetworkMode::NET_MODE_STATIC;
    bool m_settingsChanged = false;
    bool m_settingsApplied = false;
    bool m_redrawNeeded = true;
    bool m_fullRedrawNeeded = true;
    bool m_shouldExit = false;

    // IP selectors
    std::unique_ptr<IPSelector> m_ipSelector;
    std::unique_ptr<IPSelector> m_gatewaySelector;
    std::unique_ptr<IPSelector> m_netmaskSelector;

    // Reference to display and input
    std::shared_ptr<Display> m_display;
    std::shared_ptr<InputDevice> m_input;
};

// Callback function for IP selector changes
static void ip_change_callback(const std::string& ip) {
    // This will be handled in the implementation
}

// Constructor for implementation
NetSettingsScreen::Impl::Impl(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input)
    : m_display(display), m_input(input)
{
    // Create IP selectors with default values
    m_ipSelector = std::make_unique<IPSelector>("192.168.001.001", 16, ip_change_callback);
    m_gatewaySelector = std::make_unique<IPSelector>("192.168.001.001", 16, ip_change_callback);
    m_netmaskSelector = std::make_unique<IPSelector>("255.255.255.000", 16, ip_change_callback);
    
    // Try to initialize from script
    if (!initNetworkSettingsFromScript()) {
        Logger::warning("Failed to get network settings from script, using defaults");
    }
}

NetSettingsScreen::Impl::~Impl() {
    // Reset all IP selectors
    m_ipSelector->reset();
    m_gatewaySelector->reset();
    m_netmaskSelector->reset();
}

bool NetSettingsScreen::Impl::initNetworkSettingsFromScript() {
    // Command to execute the script
    ///usr/bin/dhcp-net-settings.sh --os=debian --interface=eth0
    std::string cmd = getNetSettingsScriptPath();
    std::string ostype = getNetSettingsOsType();
    std::string iface = getNetSettingsInterface();
    cmd+=" --os="+ostype+" --interface="+iface;
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) {
        Logger::error("Failed to run dhcp-net-settings.sh");
        return false;
    }

    char line[128];
    bool hasIp = false;
    bool hasGateway = false;
    bool hasNetmask = false;
    bool foundResult = false;

    // Parse script output
    while (fgets(line, sizeof(line), fp) != nullptr) {
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }

        Logger::debug("Script output: " + std::string(line));

        // Check for result status
        if (strncmp(line, "RESULT:", 7) == 0) {
            foundResult = true;
            if (strstr(line, "ERROR") != nullptr) {
                Logger::error("Error reading network settings");
                pclose(fp);
                return false;
            }
        }
        // Check mode
        else if (strncmp(line, "mode=", 5) == 0) {
            if (strcmp(line + 5, "static") == 0) {
                m_mode = NetworkMode::NET_MODE_STATIC;
            } else if (strcmp(line + 5, "dhcp") == 0) {
                m_mode = NetworkMode::NET_MODE_DHCP;
            }
        }
        // Check IP
        else if (strncmp(line, "ip=", 3) == 0) {
            hasIp = true;
            m_ipSelector->setIp(line + 3);
        }
        // Check gateway
        else if (strncmp(line, "gateway=", 8) == 0) {
            hasGateway = true;
            m_gatewaySelector->setIp(line + 8);
        }
        // Check netmask
        else if (strncmp(line, "netmask=", 8) == 0) {
            hasNetmask = true;
            m_netmaskSelector->setIp(line + 8);
        }
    }

    pclose(fp);

    // Check if we got all needed information
    if (!foundResult) {
        Logger::error("Script didn't return result status");
        return false;
    }

    // If mode is static, we should have all IP settings
    if (m_mode == NetworkMode::NET_MODE_STATIC) {
        if (!hasIp || !hasGateway || !hasNetmask) {
            Logger::warning("Static mode but missing some IP settings");
            // We'll continue anyway with default values
        }
    }

    Logger::debug("Network settings initialized from script: mode=" + 
                  std::string((m_mode == NetworkMode::NET_MODE_STATIC) ? "static" : "dhcp"));
    return true;
}

void NetSettingsScreen::Impl::networkFieldChanged(const std::string& ip) {
    m_settingsChanged = true;
    m_settingsApplied = false;
}

void NetSettingsScreen::Impl::refreshSettings() {
    // Store current IP values before refreshing
    std::string currentIp = m_ipSelector->getIp();
    std::string currentGateway = m_gatewaySelector->getIp();
    std::string currentNetmask = m_netmaskSelector->getIp();

    // Refresh network settings from the script
    Logger::debug("Refreshing network settings from script");
    if (!initNetworkSettingsFromScript()) {
        Logger::warning("Failed to refresh network settings, using current values");
        // If script failed, restore previous values
        m_ipSelector->setIp(currentIp);
        m_gatewaySelector->setIp(currentGateway);
        m_netmaskSelector->setIp(currentNetmask);
    }
}

void NetSettingsScreen::Impl::applyNetworkSettings() {
    char cmd[512];
    char line[128];
    bool success = false;
    std::string scriptPath = getNetSettingsScriptPath(); 
    std::string ostype = getNetSettingsOsType();
    std::string iface  = getNetSettingsInterface();
    FILE* fp = nullptr;

    if (m_mode == NetworkMode::NET_MODE_STATIC) {
        // Get current IP values from selectors
        const std::string& ip = m_ipSelector->getIp();
        const std::string& netmask = m_netmaskSelector->getIp();
        const std::string& gateway = m_gatewaySelector->getIp();

        // Construct command with all parameters
        snprintf(cmd, sizeof(cmd),
                "%s --os=%s --interface=%s --mode=static --ip=%s --gateway=%s --netmask=%s",
                scriptPath.c_str(), ostype.c_str(),iface.c_str(),ip.c_str(), gateway.c_str(), netmask.c_str());

        Logger::debug("Running command: " + std::string(cmd));

        // Execute the command and check result
        fp = popen(cmd, "r");
        if (!fp) {
            Logger::error("Failed to run dhcp-net-settings.sh");
            m_settingsApplied = false;
            return;
        }

        // Parse output to check if successful
        while (fgets(line, sizeof(line), fp) != nullptr) {
            // Remove newline
            size_t len = strlen(line);
            if (len > 0 && line[len-1] == '\n') {
                line[len-1] = '\0';
            }

            Logger::debug("Script output: " + std::string(line));

            // Check for result status
            if (strncmp(line, "RESULT:", 7) == 0) {
                if (strstr(line, "OK") != nullptr) {
                    success = true;
                }
            }
        }

        Logger::debug("Applied static IP settings:");
        Logger::debug("  IP: " + ip);
        Logger::debug("  Netmask: " + netmask);
        Logger::debug("  Gateway: " + gateway);
    }
    else {
        // DHCP mode - simpler command
        snprintf(cmd, sizeof(cmd), "%s --os=%s --interface=%s --mode=dhcp", scriptPath.c_str(),ostype.c_str(),iface.c_str());//NET_SETTINGS_SCRIPT);

        Logger::debug("Running command: " + std::string(cmd));

        // Execute the command and check result
        fp = popen(cmd, "r");
        if (!fp) {
            Logger::error("Failed to run dhcp-net-settings.sh");
            m_settingsApplied = false;
            return;
        }

        // Parse output to check if successful
        while (fgets(line, sizeof(line), fp) != nullptr) {
            // Remove newline
            size_t len = strlen(line);
            if (len > 0 && line[len-1] == '\n') {
                line[len-1] = '\0';
            }

            Logger::debug("Script output: " + std::string(line));

            // Check for result status
            if (strncmp(line, "RESULT:", 7) == 0) {
                if (strstr(line, "OK") != nullptr) {
                    success = true;
                }
            }
        }

        Logger::debug("Applied DHCP configuration");
    }

    if (fp) {
        pclose(fp);
    }

    // Mark settings as applied only if successful
    if (success) {
        m_settingsChanged = false;
        m_settingsApplied = true;
        Logger::debug("Network settings applied successfully");
    } else {
        m_settingsApplied = false;
        Logger::error("Failed to apply network settings");
    }
}

void NetSettingsScreen::Impl::switchToMainMenu() {
    m_menuState = NetSettingsMenuState::MENU_MAIN;
    m_redrawNeeded = true;
    m_fullRedrawNeeded = true;  // Always do full redraw when switching menus
}

void NetSettingsScreen::Impl::switchToModeMenu() {
    m_menuState = NetSettingsMenuState::MENU_MODE;
    // Highlight the current mode initially
    m_modeSelection = (m_mode == NetworkMode::NET_MODE_STATIC) ? 
                       static_cast<int>(ModeMenuSelection::MODE_STATIC) : 
                       static_cast<int>(ModeMenuSelection::MODE_DHCP);
    m_prevModeSelection = m_modeSelection; // Prevent unnecessary marker update
    m_redrawNeeded = true;
    m_fullRedrawNeeded = true;  // Always do full redraw when switching menus
}

void NetSettingsScreen::Impl::switchToIpMenu() {
    m_menuState = NetSettingsMenuState::MENU_IP;
    m_addrSelection = AddrMenuSelection::ADDR_IP;
    m_prevAddrSelection = m_addrSelection; // Prevent unnecessary marker update
    m_redrawNeeded = true;
    m_fullRedrawNeeded = true;  // Always do full redraw when switching menus
}

void NetSettingsScreen::Impl::switchToGatewayMenu() {
    m_menuState = NetSettingsMenuState::MENU_GATEWAY;
    m_addrSelection = AddrMenuSelection::ADDR_IP;
    m_prevAddrSelection = m_addrSelection; // Prevent unnecessary marker update
    m_redrawNeeded = true;
    m_fullRedrawNeeded = true;  // Always do full redraw when switching menus
}

void NetSettingsScreen::Impl::switchToNetmaskMenu() {
    m_menuState = NetSettingsMenuState::MENU_NETMASK;
    m_addrSelection = AddrMenuSelection::ADDR_IP;
    m_prevAddrSelection = m_addrSelection; // Prevent unnecessary marker update
    m_redrawNeeded = true;
    m_fullRedrawNeeded = true;  // Always do full redraw when switching menus
}

void NetSettingsScreen::Impl::handleMainMenuButton() {
    switch (static_cast<MainMenuSelection>(m_mainSelection)) {
        case MainMenuSelection::MAIN_MODE:
            switchToModeMenu();
            break;

        case MainMenuSelection::MAIN_IP:
            // Only allow IP editing in static mode
            if (m_mode == NetworkMode::NET_MODE_STATIC) {
                switchToIpMenu();
            }
            break;

        case MainMenuSelection::MAIN_GATEWAY:
            // Only allow Gateway editing in static mode
            if (m_mode == NetworkMode::NET_MODE_STATIC) {
                switchToGatewayMenu();
            }
            break;

        case MainMenuSelection::MAIN_NETMASK:
            // Only allow Netmask editing in static mode
            if (m_mode == NetworkMode::NET_MODE_STATIC) {
                switchToNetmaskMenu();
            }
            break;

        case MainMenuSelection::MAIN_APPLY:
            applyNetworkSettings();
            m_redrawNeeded = true;
            m_fullRedrawNeeded = true; // Full redraw to show changes
            break;

        case MainMenuSelection::MAIN_EXIT:
            // Exit to main menu
            m_shouldExit = true;
            break;

        default:
            break;
    }
}

void NetSettingsScreen::Impl::handleModeMenuButton() {
    switch (static_cast<ModeMenuSelection>(m_modeSelection)) {
        case ModeMenuSelection::MODE_STATIC:
            m_mode = NetworkMode::NET_MODE_STATIC;
            m_settingsChanged = true;
            m_settingsApplied = false;
            switchToMainMenu();
            break;

        case ModeMenuSelection::MODE_DHCP:
            m_mode = NetworkMode::NET_MODE_DHCP;
            m_settingsChanged = true;
            m_settingsApplied = false;
            switchToMainMenu();
            break;

        case ModeMenuSelection::MODE_BACK:
            switchToMainMenu();
            break;

        default:
            break;
    }
}

void NetSettingsScreen::Impl::handleAddrMenuButton() {
    if (m_addrSelection == AddrMenuSelection::ADDR_BACK) {
        // Go back to main menu
        switchToMainMenu();
    }
    // Otherwise let the IP selector handle it
}

void NetSettingsScreen::Impl::drawMainMenu(bool fullRedraw) {
    if (fullRedraw) {
        // Clear screen
        m_display->clear();
        usleep(Config::DISPLAY_CMD_DELAY * 3);
        
        // Draw header
        m_display->drawText(0, 0, "  Net Settings");
        usleep(Config::DISPLAY_CMD_DELAY);
        
        // Draw separator
        m_display->drawText(0, 8, "----------------");
        usleep(Config::DISPLAY_CMD_DELAY);
    }

    // Draw each menu item with proper formatting
    char buffer[32];

    // Mode line
    const char* modeText = (m_mode == NetworkMode::NET_MODE_STATIC) ? "Static" : "DHCP";
    snprintf(buffer, sizeof(buffer), "%cMode: %s",
             (m_mainSelection == static_cast<int>(MainMenuSelection::MAIN_MODE)) ? '>' : ' ',
             modeText);
    m_display->drawText(0, 16, buffer);
    usleep(Config::DISPLAY_CMD_DELAY);

    // IP line
    snprintf(buffer, sizeof(buffer), "%cIP",
             (m_mainSelection == static_cast<int>(MainMenuSelection::MAIN_IP)) ? '>' : ' ');
    m_display->drawText(0, 24, buffer);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Gateway line
    snprintf(buffer, sizeof(buffer), "%cGateway",
             (m_mainSelection == static_cast<int>(MainMenuSelection::MAIN_GATEWAY)) ? '>' : ' ');
    m_display->drawText(0, 32, buffer);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Netmask line
    snprintf(buffer, sizeof(buffer), "%cNetmask",
             (m_mainSelection == static_cast<int>(MainMenuSelection::MAIN_NETMASK)) ? '>' : ' ');
    m_display->drawText(0, 40, buffer);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Apply line
    snprintf(buffer, sizeof(buffer), "%cApply",
             (m_mainSelection == static_cast<int>(MainMenuSelection::MAIN_APPLY)) ? '>' : ' ');
    m_display->drawText(0, 48, buffer);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Back line
    snprintf(buffer, sizeof(buffer), "%cBack",
             (m_mainSelection == static_cast<int>(MainMenuSelection::MAIN_EXIT)) ? '>' : ' ');
    m_display->drawText(0, 56, buffer);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Update previous selection
    m_prevMainSelection = m_mainSelection;
}

void NetSettingsScreen::Impl::updateMainMenuSelection(int oldSelection, int newSelection) {
    char buffer[32];

    // Clear old selection
    switch (static_cast<MainMenuSelection>(oldSelection)) {
        case MainMenuSelection::MAIN_MODE:
            {
                const char* modeText = (m_mode == NetworkMode::NET_MODE_STATIC) ? "Static" : "DHCP";
                snprintf(buffer, sizeof(buffer), " Mode: %s", modeText);
                m_display->drawText(0, 16, buffer);
            }
            break;
        case MainMenuSelection::MAIN_IP:
            m_display->drawText(0, 24, " IP");
            break;
        case MainMenuSelection::MAIN_GATEWAY:
            m_display->drawText(0, 32, " Gateway");
            break;
        case MainMenuSelection::MAIN_NETMASK:
            m_display->drawText(0, 40, " Netmask");
            break;
        case MainMenuSelection::MAIN_APPLY:
            m_display->drawText(0, 48, " Apply");
            break;
        case MainMenuSelection::MAIN_EXIT:
            m_display->drawText(0, 56, " Back");
            break;
        default:
            break;
    }
    usleep(Config::DISPLAY_CMD_DELAY);

    // Set new selection
    switch (static_cast<MainMenuSelection>(newSelection)) {
        case MainMenuSelection::MAIN_MODE:
            {
                const char* modeText = (m_mode == NetworkMode::NET_MODE_STATIC) ? "Static" : "DHCP";
                snprintf(buffer, sizeof(buffer), ">Mode: %s", modeText);
                m_display->drawText(0, 16, buffer);
            }
            break;
        case MainMenuSelection::MAIN_IP:
            m_display->drawText(0, 24, ">IP");
            break;
        case MainMenuSelection::MAIN_GATEWAY:
            m_display->drawText(0, 32, ">Gateway");
            break;
        case MainMenuSelection::MAIN_NETMASK:
            m_display->drawText(0, 40, ">Netmask");
            break;
        case MainMenuSelection::MAIN_APPLY:
            m_display->drawText(0, 48, ">Apply");
            break;
        case MainMenuSelection::MAIN_EXIT:
            m_display->drawText(0, 56, ">Back");
            break;
        default:
            break;
    }
    usleep(Config::DISPLAY_CMD_DELAY);
}

void NetSettingsScreen::Impl::drawModeMenu(bool fullRedraw) {
    if (fullRedraw) {
        // Clear screen
        m_display->clear();
        usleep(Config::DISPLAY_CMD_DELAY * 3);
        
        // Draw header
        m_display->drawText(0, 0, "Mode");
        usleep(Config::DISPLAY_CMD_DELAY);
        
        // Draw separator
        m_display->drawText(0, 8, "----------------");
        usleep(Config::DISPLAY_CMD_DELAY);
    }

    // Draw each menu item with proper formatting
    char buffer[32];

    // Static mode option
    snprintf(buffer, sizeof(buffer), "%cStatic",
             (m_modeSelection == static_cast<int>(ModeMenuSelection::MODE_STATIC)) ? '>' : ' ');
    m_display->drawText(0, 16, buffer);
    usleep(Config::DISPLAY_CMD_DELAY);

    // DHCP mode option
    snprintf(buffer, sizeof(buffer), "%cDhcp",
             (m_modeSelection == static_cast<int>(ModeMenuSelection::MODE_DHCP)) ? '>' : ' ');
    m_display->drawText(0, 24, buffer);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Back option
    snprintf(buffer, sizeof(buffer), "%cBack",
             (m_modeSelection == static_cast<int>(ModeMenuSelection::MODE_BACK)) ? '>' : ' ');
    m_display->drawText(0, 32, buffer);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Clear remaining lines
    m_display->drawText(0, 40, "                ");
    m_display->drawText(0, 48, "                ");
    m_display->drawText(0, 56, "                ");
    usleep(Config::DISPLAY_CMD_DELAY);

    // Update previous selection
    m_prevModeSelection = m_modeSelection;
}

void NetSettingsScreen::Impl::updateModeMenuSelection(int oldSelection, int newSelection) {
    char buffer[32];

    // Clear old selection
    switch (static_cast<ModeMenuSelection>(oldSelection)) {
        case ModeMenuSelection::MODE_STATIC:
            m_display->drawText(0, 16, " Static");
            break;
        case ModeMenuSelection::MODE_DHCP:
            m_display->drawText(0, 24, " Dhcp");
            break;
        case ModeMenuSelection::MODE_BACK:
            m_display->drawText(0, 32, " Back");
            break;
        default:
            break;
    }
    usleep(Config::DISPLAY_CMD_DELAY);

    // Set new selection
    switch (static_cast<ModeMenuSelection>(newSelection)) {
        case ModeMenuSelection::MODE_STATIC:
            m_display->drawText(0, 16, ">Static");
            break;
        case ModeMenuSelection::MODE_DHCP:
            m_display->drawText(0, 24, ">Dhcp");
            break;
        case ModeMenuSelection::MODE_BACK:
            m_display->drawText(0, 32, ">Back");
            break;
        default:
            break;
    }
    usleep(Config::DISPLAY_CMD_DELAY);
}

void NetSettingsScreen::Impl::drawIpMenu(bool fullRedraw) {
    if (fullRedraw) {
        // Clear screen
        m_display->clear();
        usleep(Config::DISPLAY_CMD_DELAY * 3);
        
        // Draw header
        m_display->drawText(0, 0, "IP");
        usleep(Config::DISPLAY_CMD_DELAY);
        
        // Draw separator
        m_display->drawText(0, 8, "----------------");
        usleep(Config::DISPLAY_CMD_DELAY);
    }

    // Get the IP string once to ensure consistency
    const std::string& ipStr = m_ipSelector->getIp();

    // Draw IP address with proper formatting
    if (m_addrSelection == AddrMenuSelection::ADDR_IP) {
        // Show with selection marker
        m_display->drawText(0, 16, ">");
        // Draw IP selector with highlighting
        m_ipSelector->draw(true, [this](int x, int y, const std::string& text) {
            m_display->drawText(x, y, text);
        });
    } else {
        // Show without selection marker and ensure we clear the entire line first
        m_display->drawText(0, 16, "                ");
        usleep(Config::DISPLAY_CMD_DELAY);

        // Then redraw with precise formatting
        char buffer[32];
        snprintf(buffer, sizeof(buffer), " %s", ipStr.c_str());
        m_display->drawText(0, 16, buffer);

        // Clear the cursor line completely
        m_display->drawText(0, 24, "                ");
    }
    usleep(Config::DISPLAY_CMD_DELAY);

    // Back option
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%cBack",
             (m_addrSelection == AddrMenuSelection::ADDR_BACK) ? '>' : ' ');
    m_display->drawText(0, 32, buffer);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Clear remaining lines
    m_display->drawText(0, 40, "                ");
    m_display->drawText(0, 48, "                ");
    m_display->drawText(0, 56, "                ");
    usleep(Config::DISPLAY_CMD_DELAY);

    // Update previous selection
    m_prevAddrSelection = m_addrSelection;
}
void NetSettingsScreen::Impl::drawGatewayMenu(bool fullRedraw) {
    if (fullRedraw) {
        // Clear screen
        m_display->clear();
        usleep(Config::DISPLAY_CMD_DELAY * 3);
        
        // Draw header
        m_display->drawText(0, 0, "Gateway");
        usleep(Config::DISPLAY_CMD_DELAY);
        
        // Draw separator
        m_display->drawText(0, 8, "----------------");
        usleep(Config::DISPLAY_CMD_DELAY);
    }

    // Get the Gateway string once to ensure consistency
    const std::string& gatewayStr = m_gatewaySelector->getIp();

    // Draw Gateway address with proper formatting
    if (m_addrSelection == AddrMenuSelection::ADDR_IP) {
        // Show with selection marker
        m_display->drawText(0, 16, ">");
        // Draw Gateway selector with highlighting
        m_gatewaySelector->draw(true, [this](int x, int y, const std::string& text) {
            m_display->drawText(x, y, text);
        });
    } else {
        // Show without selection marker and ensure we clear the entire line first
        m_display->drawText(0, 16, "                ");
        usleep(Config::DISPLAY_CMD_DELAY);

        // Then redraw with precise formatting
        char buffer[32];
        snprintf(buffer, sizeof(buffer), " %s", gatewayStr.c_str());
        m_display->drawText(0, 16, buffer);

        // Clear the cursor line completely
        m_display->drawText(0, 24, "                ");
    }
    usleep(Config::DISPLAY_CMD_DELAY);

    // Back option
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%cBack",
             (m_addrSelection == AddrMenuSelection::ADDR_BACK) ? '>' : ' ');
    m_display->drawText(0, 32, buffer);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Clear remaining lines
    m_display->drawText(0, 40, "                ");
    m_display->drawText(0, 48, "                ");
    m_display->drawText(0, 56, "                ");
    usleep(Config::DISPLAY_CMD_DELAY);

    // Update previous selection
    m_prevAddrSelection = m_addrSelection;
}

void NetSettingsScreen::Impl::drawNetmaskMenu(bool fullRedraw) {
    if (fullRedraw) {
        // Clear screen
        m_display->clear();
        usleep(Config::DISPLAY_CMD_DELAY * 3);
        
        // Draw header
        m_display->drawText(0, 0, "Netmask");
        usleep(Config::DISPLAY_CMD_DELAY);
        
        // Draw separator
        m_display->drawText(0, 8, "----------------");
        usleep(Config::DISPLAY_CMD_DELAY);
    }

    // Get the Netmask string once to ensure consistency
    const std::string& netmaskStr = m_netmaskSelector->getIp();

    // Draw Netmask address with proper formatting
    if (m_addrSelection == AddrMenuSelection::ADDR_IP) {
        // Show with selection marker
        m_display->drawText(0, 16, ">");
        // Draw Netmask selector with highlighting
        m_netmaskSelector->draw(true, [this](int x, int y, const std::string& text) {
            m_display->drawText(x, y, text);
        });
    } else {
        // Show without selection marker and ensure we clear the entire line first
        m_display->drawText(0, 16, "                ");
        usleep(Config::DISPLAY_CMD_DELAY);

        // Then redraw with precise formatting
        char buffer[32];
        snprintf(buffer, sizeof(buffer), " %s", netmaskStr.c_str());
        m_display->drawText(0, 16, buffer);

        // Clear the cursor line completely
        m_display->drawText(0, 24, "                ");
    }
    usleep(Config::DISPLAY_CMD_DELAY);

    // Back option
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%cBack",
             (m_addrSelection == AddrMenuSelection::ADDR_BACK) ? '>' : ' ');
    m_display->drawText(0, 32, buffer);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Clear remaining lines
    m_display->drawText(0, 40, "                ");
    m_display->drawText(0, 48, "                ");
    m_display->drawText(0, 56, "                ");
    usleep(Config::DISPLAY_CMD_DELAY);

    // Update previous selection
    m_prevAddrSelection = m_addrSelection;
}

void NetSettingsScreen::Impl::updateAddrMenuSelection(AddrMenuSelection oldSelection, AddrMenuSelection newSelection, IPSelector* selector) {
    char buffer[32];
    const std::string& ipStr = selector->getIp();

    // Clear relevant lines first
    if (oldSelection == AddrMenuSelection::ADDR_IP) {
        // Clear both the IP line and cursor line
        m_display->drawText(0, 16, "                ");
        m_display->drawText(0, 24, "                ");
        usleep(Config::DISPLAY_CMD_DELAY);
    } else if (oldSelection == AddrMenuSelection::ADDR_BACK) {
        // Clear back line
        m_display->drawText(0, 32, "                ");
        usleep(Config::DISPLAY_CMD_DELAY);
    }

    // Handle transition from IP selector to Back
    if (oldSelection == AddrMenuSelection::ADDR_IP && newSelection == AddrMenuSelection::ADDR_BACK) {
        // Redraw IP without highlight after clearing
        snprintf(buffer, sizeof(buffer), " %s", ipStr.c_str());
        m_display->drawText(0, 16, buffer);
        usleep(Config::DISPLAY_CMD_DELAY);

        // Add highlight to Back
        m_display->drawText(0, 32, ">Back");
    }
    // Handle transition from Back to IP selector
    else if (oldSelection == AddrMenuSelection::ADDR_BACK && newSelection == AddrMenuSelection::ADDR_IP) {
        // Add highlight to IP and show IP selector
        m_display->drawText(0, 16, ">");
        selector->draw(true, [this](int x, int y, const std::string& text) {
            m_display->drawText(x, y, text);
        });

        // Redraw Back without highlight
        m_display->drawText(0, 32, " Back");
    }
    usleep(Config::DISPLAY_CMD_DELAY);
}

// NetSettingsScreen public methods implementation

NetSettingsScreen::NetSettingsScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input)
    : ScreenModule(display, input),
      m_pImpl(std::make_unique<Impl>(display, input))
{
    // Constructor - Implementation is in Impl
}

NetSettingsScreen::~NetSettingsScreen() = default;

void NetSettingsScreen::enter() {
    Logger::debug("NetSettingsScreen: Entered");
    
    // Reset state
    m_pImpl->m_menuState = NetSettingsMenuState::MENU_MAIN;
    m_pImpl->m_mainSelection = static_cast<int>(MainMenuSelection::MAIN_MODE);
    m_pImpl->m_prevMainSelection = static_cast<int>(MainMenuSelection::MAIN_MODE);
    m_pImpl->m_modeSelection = static_cast<int>(ModeMenuSelection::MODE_STATIC);
    m_pImpl->m_prevModeSelection = static_cast<int>(ModeMenuSelection::MODE_STATIC);
    m_pImpl->m_addrSelection = AddrMenuSelection::ADDR_IP;
    m_pImpl->m_prevAddrSelection = AddrMenuSelection::ADDR_IP;
    m_pImpl->m_shouldExit = false;
    m_pImpl->m_redrawNeeded = true;
    m_pImpl->m_fullRedrawNeeded = true;
    
    // Refresh settings from script
    m_pImpl->refreshSettings();
    
    // Initial screen draw
    m_pImpl->drawMainMenu(true);
}

void NetSettingsScreen::update() {
    // If redraw is needed, update the display
    if (m_pImpl->m_redrawNeeded) {
        switch (m_pImpl->m_menuState) {
            case NetSettingsMenuState::MENU_MAIN:
                m_pImpl->drawMainMenu(m_pImpl->m_fullRedrawNeeded);
                break;
            case NetSettingsMenuState::MENU_MODE:
                m_pImpl->drawModeMenu(m_pImpl->m_fullRedrawNeeded);
                break;
            case NetSettingsMenuState::MENU_IP:
                m_pImpl->drawIpMenu(m_pImpl->m_fullRedrawNeeded);
                break;
            case NetSettingsMenuState::MENU_GATEWAY:
                m_pImpl->drawGatewayMenu(m_pImpl->m_fullRedrawNeeded);
                break;
            case NetSettingsMenuState::MENU_NETMASK:
                m_pImpl->drawNetmaskMenu(m_pImpl->m_fullRedrawNeeded);
                break;
        }
        m_pImpl->m_redrawNeeded = false;
        m_pImpl->m_fullRedrawNeeded = false;
    }
}

void NetSettingsScreen::exit() {
    Logger::debug("NetSettingsScreen: Exiting");
    
    // Clear display
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 3);
}

bool NetSettingsScreen::handleInput() {
    if (m_input->waitForEvents(100) > 0) {
        bool buttonPressed = false;
        int rotationDirection = 0;
        
        m_input->processEvents(
            [this, &rotationDirection](int direction) {
                // Store rotation direction
                rotationDirection = direction;
                m_display->updateActivityTimestamp();
            },
            [this, &buttonPressed]() {
                buttonPressed = true;
                m_display->updateActivityTimestamp();
            }
        );
        
        // Handle button press
        if (buttonPressed) {
            bool handled = false;
            
            // Let IP selector handle button first if applicable
            if (m_pImpl->m_menuState == NetSettingsMenuState::MENU_IP && 
                m_pImpl->m_addrSelection == AddrMenuSelection::ADDR_IP) {
                handled = m_pImpl->m_ipSelector->handleButton();
                if (handled) {
                    m_pImpl->m_redrawNeeded = true;
                    m_pImpl->m_settingsChanged = true;
                    m_pImpl->m_settingsApplied = false;
                }
            }
            else if (m_pImpl->m_menuState == NetSettingsMenuState::MENU_GATEWAY && 
                     m_pImpl->m_addrSelection == AddrMenuSelection::ADDR_IP) {
                handled = m_pImpl->m_gatewaySelector->handleButton();
                if (handled) {
                    m_pImpl->m_redrawNeeded = true;
                    m_pImpl->m_settingsChanged = true;
                    m_pImpl->m_settingsApplied = false;
                }
            }
            else if (m_pImpl->m_menuState == NetSettingsMenuState::MENU_NETMASK && 
                     m_pImpl->m_addrSelection == AddrMenuSelection::ADDR_IP) {
                handled = m_pImpl->m_netmaskSelector->handleButton();
                if (handled) {
                    m_pImpl->m_redrawNeeded = true;
                    m_pImpl->m_settingsChanged = true;
                    m_pImpl->m_settingsApplied = false;
                }
            }
            
            // If not handled by IP selector, handle menu actions
            if (!handled) {
                switch (m_pImpl->m_menuState) {
                    case NetSettingsMenuState::MENU_MAIN:
                        m_pImpl->handleMainMenuButton();
                        break;
                    case NetSettingsMenuState::MENU_MODE:
                        m_pImpl->handleModeMenuButton();
                        break;
                    case NetSettingsMenuState::MENU_IP:
                    case NetSettingsMenuState::MENU_GATEWAY:
                    case NetSettingsMenuState::MENU_NETMASK:
                        m_pImpl->handleAddrMenuButton();
                        break;
                }
            }
        }
        
        // Handle rotation
        if (rotationDirection != 0) {
            bool handled = false;
            
            // Let IP selector handle rotation first if applicable
            if (m_pImpl->m_menuState == NetSettingsMenuState::MENU_IP && 
                m_pImpl->m_addrSelection == AddrMenuSelection::ADDR_IP) {
                handled = m_pImpl->m_ipSelector->handleRotation(rotationDirection);
                if (handled) {
                    m_pImpl->m_redrawNeeded = true;
                    m_pImpl->m_settingsChanged = true;
                    m_pImpl->m_settingsApplied = false;
                }
            }
            else if (m_pImpl->m_menuState == NetSettingsMenuState::MENU_GATEWAY && 
                     m_pImpl->m_addrSelection == AddrMenuSelection::ADDR_IP) {
                handled = m_pImpl->m_gatewaySelector->handleRotation(rotationDirection);
                if (handled) {
                    m_pImpl->m_redrawNeeded = true;
                    m_pImpl->m_settingsChanged = true;
                    m_pImpl->m_settingsApplied = false;
                }
            }
            else if (m_pImpl->m_menuState == NetSettingsMenuState::MENU_NETMASK && 
                     m_pImpl->m_addrSelection == AddrMenuSelection::ADDR_IP) {
                handled = m_pImpl->m_netmaskSelector->handleRotation(rotationDirection);
                if (handled) {
                    m_pImpl->m_redrawNeeded = true;
                    m_pImpl->m_settingsChanged = true;
                    m_pImpl->m_settingsApplied = false;
                }
            }
            
            // If not handled by IP selector, handle menu navigation
            if (!handled) {
                switch (m_pImpl->m_menuState) {
                    case NetSettingsMenuState::MENU_MAIN:
                        {
                            int oldSelection = m_pImpl->m_mainSelection;
                            int numItems = static_cast<int>(MainMenuSelection::MAIN_ITEM_COUNT);
                            
                            // Navigate menu
                            if (rotationDirection < 0) {
                                // Move up (with wrap)
                                m_pImpl->m_mainSelection = (m_pImpl->m_mainSelection > 0) ? 
                                                        m_pImpl->m_mainSelection - 1 : 
                                                        numItems - 1;
                            } else {
                                // Move down (with wrap)
                                m_pImpl->m_mainSelection = (m_pImpl->m_mainSelection + 1) % numItems;
                            }
                            
                            // Update display if selection changed
                            if (oldSelection != m_pImpl->m_mainSelection) {
                                m_pImpl->updateMainMenuSelection(oldSelection, m_pImpl->m_mainSelection);
                            }
                        }
                        break;
                    
                    case NetSettingsMenuState::MENU_MODE:
                        {
                            int oldSelection = m_pImpl->m_modeSelection;
                            int numItems = static_cast<int>(ModeMenuSelection::MODE_ITEM_COUNT);
                            
                            // Navigate menu
                            if (rotationDirection < 0) {
                                // Move up (with wrap)
                                m_pImpl->m_modeSelection = (m_pImpl->m_modeSelection > 0) ? 
                                                        m_pImpl->m_modeSelection - 1 : 
                                                        numItems - 1;
                            } else {
                                // Move down (with wrap)
                                m_pImpl->m_modeSelection = (m_pImpl->m_modeSelection + 1) % numItems;
                            }
                            
                            // Update display if selection changed
                            if (oldSelection != m_pImpl->m_modeSelection) {
                                m_pImpl->updateModeMenuSelection(oldSelection, m_pImpl->m_modeSelection);
                            }
                        }
                        break;
                    
                    case NetSettingsMenuState::MENU_IP:
                    case NetSettingsMenuState::MENU_GATEWAY:
                    case NetSettingsMenuState::MENU_NETMASK:
                        {
                            AddrMenuSelection oldSelection = m_pImpl->m_addrSelection;
                            
                            // Toggle between IP field and Back button
                            m_pImpl->m_addrSelection = (m_pImpl->m_addrSelection == AddrMenuSelection::ADDR_IP) ?
                                              AddrMenuSelection::ADDR_BACK : AddrMenuSelection::ADDR_IP;
                            
                            // Update display if selection changed
                            if (oldSelection != m_pImpl->m_addrSelection) {
                                IPSelector* selector = nullptr;
                                
                                if (m_pImpl->m_menuState == NetSettingsMenuState::MENU_IP) {
                                    selector = m_pImpl->m_ipSelector.get();
                                } else if (m_pImpl->m_menuState == NetSettingsMenuState::MENU_GATEWAY) {
                                    selector = m_pImpl->m_gatewaySelector.get();
                                } else {
                                    selector = m_pImpl->m_netmaskSelector.get();
                                }
                                
                                m_pImpl->updateAddrMenuSelection(oldSelection, m_pImpl->m_addrSelection, selector);
                            }
                        }
                        break;
                }
            }
        }
    }
    
    // Return false to exit the screen
    return !m_pImpl->m_shouldExit;
}

//check if required dhcp-net-settings.sh is provided through json config file
//else returns default path
std::string NetSettingsScreen::Impl::getNetSettingsScriptPath() {
    const std::string defaultPath = "/usr/bin/dhcp-net-settings.sh";
    
    // Try to get the path from dependencies
    auto& dependencies = ModuleDependency::getInstance();
    std::string scriptPath = dependencies.getDependencyPath("netsettings", "action_script");
    
    if (scriptPath.empty()) {
        Logger::debug("No action_script dependency found for netsettings, using default path");
        return defaultPath;
    }
    
    Logger::debug("Using script path from dependencies: " + scriptPath);
    return scriptPath;
}
std::string NetSettingsScreen::Impl::getNetSettingsOsType() {
    const std::string defaultOs = "debian";

    // Try to get the os_type from dependencies
    auto& dependencies = ModuleDependency::getInstance();
    std::string osType = dependencies.getDependencyPath("netsettings", "os_type");

    if (osType.empty()) {
        Logger::debug("No os_type dependency found for netsettings, using default debian os_type");
        return defaultOs;
    }
    Logger::debug("Using os_type from dependencies: " + osType);
    return osType;
}
std::string NetSettingsScreen::Impl::getNetSettingsInterface() {
    const std::string defaultInterface = "eth0";

    // Try to get the iface_name
    auto& dependencies = ModuleDependency::getInstance();
    std::string interfaceName = dependencies.getDependencyPath("netsettings", "iface_name");

    if(interfaceName.empty()) {
       Logger::debug("No iface_name dependency found for netsettings, using default eth0 interface");
       return defaultInterface;
    }
    Logger::debug("Using iface_name from dependencies: " + interfaceName);
    return interfaceName;
}
