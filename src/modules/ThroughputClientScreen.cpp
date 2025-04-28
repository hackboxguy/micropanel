#include "ScreenModules.h"
#include "MenuSystem.h"
#include "DeviceInterfaces.h"
#include "IPSelector.h"
#include "Logger.h"
#include "Config.h"
#include "ModuleDependency.h"
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <string>
#include <memory>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <csignal>
#include <sys/wait.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <regex>
#include <set>

ThroughputClientScreen::ThroughputClientScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input)
    : ScreenModule(display, input),
      m_state(ThroughputClientState::MENU_STATE_START),
      m_submenuSelection(0),
      m_editingIp(false),
      m_shouldExit(false),
      m_statusChanged(false),
      m_serverIp("192.168.001.001"),
      m_serverPort(5201),
      m_protocol("TCP"),
      m_duration(10),
      m_bandwidth(0),
      m_parallel(1),
      m_testInProgress(false),
      m_testPid(-1),
      m_testResult(-1),
      m_bandwidth_result(0.0),
      m_jitter_result(0.0),
      m_loss_result(0.0),
      m_retransmits_result(0),
      m_discoveryInProgress(false),
      m_discoveryPid(-1)
{
    // Initialize menu options
    m_protocolOptions = {"TCP", "UDP"};
    m_durationOptions = {10, 20, 30, 40, 50, 60};
    m_bandwidthOptions = {0, 10, 20, 50, 100, 500};
    m_parallelOptions = {1, 4, 8, 16, 32};

    // Load settings from config
    refreshSettings();

    // Create IP Selector with callback
    auto callback = [this](const std::string& ip) {
        m_serverIp = ip;
        Logger::debug("ThroughputClientScreen: Server IP changed to: " + ip);
    };

    // Create redraw callback to update menu
    auto redrawCallback = [this]() {
        if (m_state == ThroughputClientState::SUBMENU_STATE_SERVER_IP) {
            renderServerIPSubmenu(false);
        }
    };

    m_ipSelector = std::make_unique<IPSelector>(m_serverIp, 16, callback, redrawCallback);
}

ThroughputClientScreen::~ThroughputClientScreen() {
    // Terminate any ongoing processes
    if (m_testInProgress && m_testPid > 0) {
        kill(m_testPid, SIGTERM);
        waitpid(m_testPid, nullptr, 0);
    }

    if (m_discoveryInProgress && m_discoveryPid > 0) {
        kill(m_discoveryPid, SIGTERM);
        waitpid(m_discoveryPid, nullptr, 0);
    }
}

void ThroughputClientScreen::refreshSettings() {
    auto& dependencies = ModuleDependency::getInstance();

    // Try to get iperf3 path
    std::string iperf3Path = dependencies.getDependencyPath("throughputclient", "iperf3_path");

    // Try to get port
    std::string portStr = dependencies.getDependencyPath("throughputclient", "default_port");
    if (!portStr.empty()) {
        try {
            int port = std::stoi(portStr);
            if (port > 0 && port < 65536) {
                m_serverPort = port;
                Logger::debug("ThroughputClientScreen: Using configured port: " + std::to_string(m_serverPort));
            }
        } catch (...) {
            Logger::warning("ThroughputClientScreen: Invalid port value in config, using default 5201");
        }
    }

    // Try to get protocol
    std::string protocol = dependencies.getDependencyPath("throughputclient", "default_protocol");
    if (!protocol.empty()) {
        std::transform(protocol.begin(), protocol.end(), protocol.begin(), ::toupper);
        if (protocol == "TCP" || protocol == "UDP") {
            m_protocol = protocol;
            Logger::debug("ThroughputClientScreen: Using configured protocol: " + m_protocol);
        }
    }

    // Try to get duration
    std::string durationStr = dependencies.getDependencyPath("throughputclient", "default_duration");
    if (!durationStr.empty()) {
        try {
            int duration = std::stoi(durationStr);
            if (duration > 0) {
                m_duration = duration;
                Logger::debug("ThroughputClientScreen: Using configured duration: " + std::to_string(m_duration));
            }
        } catch (...) {
            Logger::warning("ThroughputClientScreen: Invalid duration value in config, using default 10s");
        }
    }

    // Try to get bandwidth
    std::string bandwidthStr = dependencies.getDependencyPath("throughputclient", "default_bandwidth");
    if (!bandwidthStr.empty()) {
        try {
            int bandwidth = std::stoi(bandwidthStr);
            if (bandwidth >= 0) {
                m_bandwidth = bandwidth;
                Logger::debug("ThroughputClientScreen: Using configured bandwidth: " + std::to_string(m_bandwidth) + " Mbps");
            }
        } catch (...) {
            Logger::warning("ThroughputClientScreen: Invalid bandwidth value in config, using default 0 (Auto)");
        }
    }

    // Try to get parallel
    std::string parallelStr = dependencies.getDependencyPath("throughputclient", "default_parallel");
    if (!parallelStr.empty()) {
        try {
            int parallel = std::stoi(parallelStr);
            if (parallel > 0) {
                m_parallel = parallel;
                Logger::debug("ThroughputClientScreen: Using configured parallel: " + std::to_string(m_parallel));
            }
        } catch (...) {
            Logger::warning("ThroughputClientScreen: Invalid parallel value in config, using default 1");
        }
    }

    // Try to get server IP
    std::string serverIP = dependencies.getDependencyPath("throughputclient", "default_server_ip");
    if (!serverIP.empty()) {
        // Validate IP format (simple check)
        if (serverIP.find('.') != std::string::npos) {
            m_serverIp = serverIP;
            if (m_ipSelector) {
                m_ipSelector->setIp(m_serverIp);
            }
            Logger::debug("ThroughputClientScreen: Using configured server IP: " + m_serverIp);
        }
    }
}

void ThroughputClientScreen::enter() {
    Logger::debug("ThroughputClientScreen: Entered");

    // Reset state
    m_state = ThroughputClientState::MENU_STATE_START;
    m_submenuSelection = 0;
    m_editingIp = false;
    m_shouldExit = false;
    m_testInProgress = false;
    m_testPid = -1;
    m_testResult = -1;
    m_discoveryInProgress = false;
    m_discoveryPid = -1;
    m_statusMessage.clear();
    m_statusChanged = true;

    // Reset result values
    m_bandwidth_result = 0.0;
    m_jitter_result = 0.0;
    m_loss_result = 0.0;
    m_retransmits_result = 0;

    // Clear discovered servers
    m_discoveredServers.clear();
    m_discoveredServerNames.clear();

    // Refresh settings in case they've been loaded after constructor
    refreshSettings();

    // Reset IP selector
    if (m_ipSelector) {
        m_ipSelector->reset();
    }

    // Full redraw
    renderMainMenu(true);
}

void ThroughputClientScreen::exit() {
    Logger::debug("ThroughputClientScreen: Exiting");

    // Terminate any ongoing test or discovery
    if (m_testInProgress && m_testPid > 0) {
        kill(m_testPid, SIGTERM);
        waitpid(m_testPid, nullptr, 0);
        m_testInProgress = false;
        m_testPid = -1;
    }

    if (m_discoveryInProgress && m_discoveryPid > 0) {
        kill(m_discoveryPid, SIGTERM);
        waitpid(m_discoveryPid, nullptr, 0);
        m_discoveryInProgress = false;
        m_discoveryPid = -1;
    }

    // Clean up temporary files if they exist
    unlink("/tmp/micropanel_iperf_result.txt");
    unlink("/tmp/micropanel_avahi_result.txt");

    // Clear display
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 3);
}

// Helper methods

std::string ThroughputClientScreen::getIperf3Path() const {
    auto& dependencies = ModuleDependency::getInstance();

    // First check client-specific iperf3 path, then fall back to server config
    std::string path = dependencies.getDependencyPath("throughputclient", "iperf3_path");
    if (path.empty()) {
        path = dependencies.getDependencyPath("throughputserver", "iperf3_path");
    }
    if (path.empty()) {
        return "/usr/bin/iperf3";
    }
    return path;
}

bool ThroughputClientScreen::isIperf3Available() const {
    std::string iperf3Path = getIperf3Path();
    return (access(iperf3Path.c_str(), X_OK) == 0);
}

bool ThroughputClientScreen::isAvahiAvailable() const {
    return (system("which avahi-browse > /dev/null 2>&1") == 0);
}

std::string ThroughputClientScreen::getBandwidthString(int value) const {
    if (value <= 0) {
        return "Auto";
    }
    return std::to_string(value) + " Mbps";
}

std::string ThroughputClientScreen::formatBandwidth(double value) const {
    std::ostringstream oss;
    if (value < 1.0) {
        oss << value * 1000.0 << " Kbps";
    } else if (value < 1000.0) {
        oss.precision(1);
        oss << std::fixed << value << " Mbps";
    } else {
        oss.precision(2);
        oss << std::fixed << (value / 1000.0) << " Gbps";
    }
    return oss.str();
}
// Menu rendering methods

void ThroughputClientScreen::update() {
    // Check test status if a test is in progress
    if (m_testInProgress) {
        bool wasInProgress = m_testInProgress;
        checkTestStatus();

        // Only update status if test state changed or still in progress
        if (wasInProgress != m_testInProgress || m_testInProgress) {
            m_statusChanged = true;
        }
    //}

    // Skip the rest if we're displaying results and waiting for input
    //if (m_state == ThroughputClientState::MENU_STATE_RESULTS && m_waitingForButtonPress) {
    //    return;
	if (m_state == ThroughputClientState::MENU_STATE_TESTING ||
		(m_state == ThroughputClientState::MENU_STATE_RESULTS && m_waitingForButtonPress)) {
		return;
        }
    }

    // Check discovery status if in progress
    if (m_discoveryInProgress) {
        bool wasInProgress = m_discoveryInProgress;
        checkDiscoveryStatus();

        // Only update if discovery state changed or still in progress
        if (wasInProgress != m_discoveryInProgress || m_discoveryInProgress) {
            m_statusChanged = true;

            // If discovery just completed, render the server selection menu
            if (wasInProgress && !m_discoveryInProgress) {
                renderAutoDiscoverScreen(true);
            }
        }
    }

    // Only update the status line when needed
    if (m_statusChanged) {
        updateStatusLine();
        m_statusChanged = false;
    }
}

void ThroughputClientScreen::renderMainMenu(bool fullRedraw) {
    // Define all menu states in order
    const std::vector<ThroughputClientState> menuStates = {
        ThroughputClientState::MENU_STATE_START,
        ThroughputClientState::MENU_STATE_PROTOCOL,
        ThroughputClientState::MENU_STATE_DURATION,
        ThroughputClientState::MENU_STATE_BANDWIDTH,
        ThroughputClientState::MENU_STATE_PARALLEL,
        ThroughputClientState::MENU_STATE_SERVER_IP,
        ThroughputClientState::MENU_STATE_BACK
    };

    if (m_state == ThroughputClientState::MENU_STATE_TESTING ||
        (m_state == ThroughputClientState::MENU_STATE_RESULTS && m_waitingForButtonPress)) {
        return;
    }

    // Find the index of the currently selected item
    int selectedIndex = 0;
    for (size_t i = 0; i < menuStates.size(); i++) {
        if (menuStates[i] == m_state) {
            selectedIndex = i;
            break;
        }
    }

    // Determine if we need to show the last 6 items
    bool showLast6Items = (selectedIndex >= 5); // Show last 6 when on item 5 or greater

    // Keep track of whether we've changed pages
    static bool wasShowingLast6Items = false;
    bool pageChanged = (wasShowingLast6Items != showLast6Items);
    wasShowingLast6Items = showLast6Items;

    // Only do a full menu redraw when necessary
    if (fullRedraw || pageChanged) {
        // Clear the screen or menu area
        if (fullRedraw) {
            m_display->clear();
            usleep(Config::DISPLAY_CMD_DELAY * 3);

            // Draw header with status
            std::string headerText = m_testInProgress ? "Client(Running)" : "Client(Ready)";
            m_display->drawText(0, 0, headerText);
            usleep(Config::DISPLAY_CMD_DELAY);

            // Draw separator
            m_display->drawText(0, 8, "----------------");
            usleep(Config::DISPLAY_CMD_DELAY);
        } else if (pageChanged) {
            // Only clear the menu area, not the header
            for (int i = 0; i < 6; i++) {
                int yPos = 16 + (i * 8);
                m_display->drawText(0, yPos, "                ");
                usleep(Config::DISPLAY_CMD_DELAY);
            }
        }

        // Determine the range of menu items to display
        int startIndex = showLast6Items ? 1 : 0; // Skip first item when showing last 6
        int endIndex = showLast6Items ? 7 : 6;   // Show items 1-7 or 0-6

        // Draw the visible menu items
        int yPos = 16; // Starting Y position after header and separator

        for (int i = startIndex; i < endIndex; i++) {
            ThroughputClientState itemState = menuStates[i];
            std::string itemText;
            bool isSelected = (m_state == itemState);

            // Generate text for each menu item
            switch (itemState) {
                case ThroughputClientState::MENU_STATE_START:
                    itemText = isSelected ? ">Start Test" : " Start Test";
                    break;

                case ThroughputClientState::MENU_STATE_PROTOCOL:
                    itemText = isSelected ?
                            ">Proto: " + m_protocol :
                            " Proto: " + m_protocol;
                    break;

                case ThroughputClientState::MENU_STATE_DURATION:
                    itemText = isSelected ?
                            ">Duration: " + std::to_string(m_duration) + "s" :
                            " Duration: " + std::to_string(m_duration) + "s";
                    break;

                case ThroughputClientState::MENU_STATE_BANDWIDTH:
                    itemText = isSelected ?
                            ">BW: " + getBandwidthString(m_bandwidth) :
                            " BW: " + getBandwidthString(m_bandwidth);
                    break;

                case ThroughputClientState::MENU_STATE_PARALLEL:
                    itemText = isSelected ?
                            ">Parallel: " + std::to_string(m_parallel) :
                            " Parallel: " + std::to_string(m_parallel);
                    break;

                case ThroughputClientState::MENU_STATE_SERVER_IP:
                    itemText = isSelected ?
                            ">" + normalizeIp(m_serverIp) :
                            " " + normalizeIp(m_serverIp);
                    break;

                case ThroughputClientState::MENU_STATE_BACK:
                    itemText = isSelected ? ">Back" : " Back";
                    break;

                default:
                    continue; // Skip submenu states
            }
            m_display->drawText(0, yPos, itemText);
            usleep(Config::DISPLAY_CMD_DELAY);
            yPos += 8; // 8 pixel spacing
        }
    } else {
        // Just update the selection markers
        int startIndex = showLast6Items ? 1 : 0;

        // First, clear all selection markers in the visible area
        for (int i = 0; i < 6; i++) {
            m_display->drawText(0, 16 + (i * 8), " ");
            usleep(Config::DISPLAY_CMD_DELAY);
        }

        // Then draw only the current selection marker
        // Find which visible position corresponds to current selection
        int visiblePos = -1;
        for (size_t i = 0; i < menuStates.size(); i++) {
            if (menuStates[i] == m_state) {
                visiblePos = static_cast<int>(i) - startIndex;
                break;
            }
        }

        // Draw the current selection marker if it's visible
        if (visiblePos >= 0 && visiblePos < 6) {
            m_display->drawText(0, 16 + (visiblePos * 8), ">");
            usleep(Config::DISPLAY_CMD_DELAY);
        }
    }

    // Update status line if needed
    if (fullRedraw || m_statusChanged) {
        updateStatusLine();
    }
}

void ThroughputClientScreen::renderProtocolSubmenu(bool fullRedraw) {
    if (fullRedraw) {
        // Clear the screen
        m_display->clear();
        usleep(Config::DISPLAY_CMD_DELAY * 3);

        // Draw header
        m_display->drawText(0, 0, "   Protocol");
        usleep(Config::DISPLAY_CMD_DELAY);

        // Draw separator
        m_display->drawText(0, 8, "----------------");
        usleep(Config::DISPLAY_CMD_DELAY);
    }

    // Draw protocol options
    int yPos = 16;

    for (size_t i = 0; i < m_protocolOptions.size(); i++) {
        std::string optionText = (m_submenuSelection == static_cast<int>(i)) ?
                                ">" + m_protocolOptions[i] :
                                " " + m_protocolOptions[i];
        m_display->drawText(0, yPos, optionText);
        usleep(Config::DISPLAY_CMD_DELAY);
        yPos += 10;
    }

    // Draw Back option
    std::string backText = (m_submenuSelection == static_cast<int>(m_protocolOptions.size())) ?
                         ">Back" :
                         " Back";
    m_display->drawText(0, yPos, backText);
    usleep(Config::DISPLAY_CMD_DELAY);
}
void ThroughputClientScreen::renderDurationSubmenu(bool fullRedraw) {
    if (fullRedraw) {
        // Clear the screen
        m_display->clear();
        usleep(Config::DISPLAY_CMD_DELAY * 3);
        // Draw header
        m_display->drawText(0, 0, "   Duration");
        usleep(Config::DISPLAY_CMD_DELAY);
        // Draw separator
        m_display->drawText(0, 8, "----------------");
        usleep(Config::DISPLAY_CMD_DELAY);

        // Clear menu area
        for (int i = 0; i < 6; i++) {
            m_display->drawText(0, 16 + (i * 8), "                ");
            usleep(Config::DISPLAY_CMD_DELAY);
        }
    }

    // Calculate how many items can fit on screen
    const int MAX_VISIBLE_ITEMS = 6;

    // Define total items and visible items
    int totalItems = static_cast<int>(m_durationOptions.size()) + 1; // +1 for Back

    // Calculate scroll position to keep selected item visible
    int scrollOffset = 0;
    if (m_submenuSelection >= MAX_VISIBLE_ITEMS) {
        scrollOffset = m_submenuSelection - MAX_VISIBLE_ITEMS + 1;
    }

    // Limit scroll offset to valid range
    int maxScroll = totalItems - MAX_VISIBLE_ITEMS;
    if (maxScroll < 0) maxScroll = 0;
    if (scrollOffset > maxScroll) scrollOffset = maxScroll;

    // If not full redraw, just clear all selection markers
    if (!fullRedraw) {
        for (int i = 0; i < MAX_VISIBLE_ITEMS; i++) {
            m_display->drawText(0, 16 + (i * 8), " ");
            usleep(Config::DISPLAY_CMD_DELAY);
        }
    }

    // Draw visible menu items with proper text
    for (int i = 0; i < MAX_VISIBLE_ITEMS && (i + scrollOffset) < totalItems; i++) {
        int itemIndex = i + scrollOffset;
        std::string itemText;

        // Format the menu item text
        if (itemIndex < static_cast<int>(m_durationOptions.size())) {
            // This is a duration option
            itemText = std::to_string(m_durationOptions[itemIndex]) + " sec";
        } else {
            // This is the Back option
            itemText = "Back   ";
        }

        // Add selection marker if needed
        if (itemIndex == m_submenuSelection) {
            itemText = ">" + itemText;
        } else {
            itemText = " " + itemText;
        }

        // Draw the item
        m_display->drawText(0, 16 + (i * 8), itemText);
        usleep(Config::DISPLAY_CMD_DELAY);
    }

    // Add scroll indicators if needed
    if (totalItems > MAX_VISIBLE_ITEMS) {
        // Up arrow for items above
        if (scrollOffset > 0) {
            m_display->drawText(122, 16, "^");
            usleep(Config::DISPLAY_CMD_DELAY);
        } else {
            m_display->drawText(122, 16, " ");
            usleep(Config::DISPLAY_CMD_DELAY);
        }

        // Down arrow for items below
        if (scrollOffset + MAX_VISIBLE_ITEMS < totalItems) {
            m_display->drawText(122, 16 + ((MAX_VISIBLE_ITEMS - 1) * 8), "v");
            usleep(Config::DISPLAY_CMD_DELAY);
        } else {
            m_display->drawText(122, 16 + ((MAX_VISIBLE_ITEMS - 1) * 8), " ");
            usleep(Config::DISPLAY_CMD_DELAY);
        }
    }
}

void ThroughputClientScreen::renderBandwidthSubmenu(bool fullRedraw) {
    if (fullRedraw) {
        // Clear the screen
        m_display->clear();
        usleep(Config::DISPLAY_CMD_DELAY * 3);
        // Draw header
        m_display->drawText(0, 0, "   Bandwidth");
        usleep(Config::DISPLAY_CMD_DELAY);
        // Draw separator
        m_display->drawText(0, 8, "----------------");
        usleep(Config::DISPLAY_CMD_DELAY);

        // Clear menu area
        for (int i = 0; i < 6; i++) {
            m_display->drawText(0, 16 + (i * 8), "                ");
            usleep(Config::DISPLAY_CMD_DELAY);
        }
    }

    // Calculate how many items can fit on screen
    const int MAX_VISIBLE_ITEMS = 6;

    // Define total items and visible items
    int totalItems = static_cast<int>(m_bandwidthOptions.size()) + 1; // +1 for Back

    // Calculate scroll position to keep selected item visible
    int scrollOffset = 0;
    if (m_submenuSelection >= MAX_VISIBLE_ITEMS) {
        scrollOffset = m_submenuSelection - MAX_VISIBLE_ITEMS + 1;
    }

    // Limit scroll offset to valid range
    int maxScroll = totalItems - MAX_VISIBLE_ITEMS;
    if (maxScroll < 0) maxScroll = 0;
    if (scrollOffset > maxScroll) scrollOffset = maxScroll;

    // If not full redraw, just clear all selection markers
    if (!fullRedraw) {
        for (int i = 0; i < MAX_VISIBLE_ITEMS; i++) {
            m_display->drawText(0, 16 + (i * 8), " ");
            usleep(Config::DISPLAY_CMD_DELAY);
        }
    }

    // Draw visible menu items with proper text
    for (int i = 0; i < MAX_VISIBLE_ITEMS && (i + scrollOffset) < totalItems; i++) {
        int itemIndex = i + scrollOffset;
        std::string itemText;

        // Format the menu item text
        if (itemIndex < static_cast<int>(m_bandwidthOptions.size())) {
            // This is a bandwidth option
            if (m_bandwidthOptions[itemIndex] == 0) {
                itemText = "Auto (0)";
            } else {
                itemText = std::to_string(m_bandwidthOptions[itemIndex]) + " Mbps";
            }
        } else {
            // This is the Back option
            itemText = "Back    ";
        }

        // Add selection marker if needed
        if (itemIndex == m_submenuSelection) {
            itemText = ">" + itemText;
        } else {
            itemText = " " + itemText;
        }

        // Draw the item
        m_display->drawText(0, 16 + (i * 8), itemText);
        usleep(Config::DISPLAY_CMD_DELAY);
    }

    // Add scroll indicators if needed
    if (totalItems > MAX_VISIBLE_ITEMS) {
        // Up arrow for items above
        if (scrollOffset > 0) {
            m_display->drawText(122, 16, "^");
            usleep(Config::DISPLAY_CMD_DELAY);
        } else {
            m_display->drawText(122, 16, " ");
            usleep(Config::DISPLAY_CMD_DELAY);
        }

        // Down arrow for items below
        if (scrollOffset + MAX_VISIBLE_ITEMS < totalItems) {
            m_display->drawText(122, 16 + ((MAX_VISIBLE_ITEMS - 1) * 8), "v");
            usleep(Config::DISPLAY_CMD_DELAY);
        } else {
            m_display->drawText(122, 16 + ((MAX_VISIBLE_ITEMS - 1) * 8), " ");
            usleep(Config::DISPLAY_CMD_DELAY);
        }
    }
}

void ThroughputClientScreen::renderParallelSubmenu(bool fullRedraw) {
    if (fullRedraw) {
        // Clear the screen
        m_display->clear();
        usleep(Config::DISPLAY_CMD_DELAY * 3);

        // Draw header
        m_display->drawText(0, 0, "   Parallel");
        usleep(Config::DISPLAY_CMD_DELAY);

        // Draw separator
        m_display->drawText(0, 8, "----------------");
        usleep(Config::DISPLAY_CMD_DELAY);
    }

    // Draw parallel options
    int yPos = 16;

    for (size_t i = 0; i < m_parallelOptions.size(); i++) {
        std::string optionText = (m_submenuSelection == static_cast<int>(i)) ?
                                ">" + std::to_string(m_parallelOptions[i]) :
                                " " + std::to_string(m_parallelOptions[i]);
        m_display->drawText(0, yPos, optionText);
        usleep(Config::DISPLAY_CMD_DELAY);
        yPos += 8;
    }

    // Draw Back option
    std::string backText = (m_submenuSelection == static_cast<int>(m_parallelOptions.size())) ?
                         ">Back" :
                         " Back";
    m_display->drawText(0, yPos, backText);
    usleep(Config::DISPLAY_CMD_DELAY);
}
void ThroughputClientScreen::renderServerIPSubmenu(bool forceRedraw) {
    if (forceRedraw) {
        // Clear the screen
        m_display->clear();
        usleep(Config::DISPLAY_CMD_DELAY * 3);

        // Draw header
        m_display->drawText(0, 0, "   Server IP");
        usleep(Config::DISPLAY_CMD_DELAY);

        // Draw separator
        m_display->drawText(0, 8, "----------------");
        usleep(Config::DISPLAY_CMD_DELAY);
    }

    // Prepare draw function for IP selector
    auto drawFunc = [this](int x, int y, const std::string& text) {
        m_display->drawText(x, y, text);
        usleep(Config::DISPLAY_CMD_DELAY);
    };

    // Draw IP address with appropriate selection
    bool ipSelected = (m_submenuSelection == 0);
    if (m_editingIp) {
        // If actively editing IP, pass true to show cursor
        m_ipSelector->draw(true, drawFunc);
    } else {
        // Otherwise, show selection marker based on submenu selection
        m_ipSelector->draw(ipSelected, drawFunc);
        //std::string prefix = (ipSelected ? ">" : " ");
        //m_display->drawText(0, 16, prefix + "IP: " + m_serverIp);
    }
    
    // Draw Auto-Discover option with selection marker
    std::string autoDiscoverLine = (m_submenuSelection == 1 ? ">Auto-Discover" : " Auto-Discover");
    m_display->drawText(0, 32, autoDiscoverLine);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Draw Back option with selection marker
    std::string backLine = (m_submenuSelection == 2 ? ">Back" : " Back");
    m_display->drawText(0, 40, backLine);
    usleep(Config::DISPLAY_CMD_DELAY);
}

void ThroughputClientScreen::renderAutoDiscoverScreen(bool fullRedraw) {
    if (fullRedraw) {
        // Clear the screen
        m_display->clear();
        usleep(Config::DISPLAY_CMD_DELAY * 3);

        if (m_discoveryInProgress) {
            // Draw header
            m_display->drawText(0, 0, "   Discovering");
            usleep(Config::DISPLAY_CMD_DELAY);

            // Draw separator
            m_display->drawText(0, 8, "----------------");
            usleep(Config::DISPLAY_CMD_DELAY);

            // Draw scanning message
            m_display->drawText(0, 16, "Scanning...");
            usleep(Config::DISPLAY_CMD_DELAY);
        } else if (!m_discoveredServers.empty()) {
            // Discovery completed and servers found

            // Draw header
            m_display->drawText(0, 0, "   Select Server");
            usleep(Config::DISPLAY_CMD_DELAY);

            // Draw separator
            m_display->drawText(0, 8, "----------------");
            usleep(Config::DISPLAY_CMD_DELAY);

            // Draw discovered servers (up to 5)
            int yPos = 16;
            int numToShow = std::min(static_cast<int>(m_discoveredServers.size()), 5);

            for (int i = 0; i < numToShow; i++) {
                std::string serverText = (m_submenuSelection == i) ?
                                      ">" + m_discoveredServers[i].first :
                                      " " + m_discoveredServers[i].first;
                m_display->drawText(0, yPos, serverText);
                usleep(Config::DISPLAY_CMD_DELAY);
                yPos += 10;
            }

            // Draw Back option
            std::string backText = (m_submenuSelection == numToShow) ? ">Back" : " Back";
            m_display->drawText(0, yPos, backText);
            usleep(Config::DISPLAY_CMD_DELAY);
        } else {
            // No servers found

            // Draw header
            m_display->drawText(0, 0, "   No Servers");
            usleep(Config::DISPLAY_CMD_DELAY);

            // Draw separator
            m_display->drawText(0, 8, "----------------");
            usleep(Config::DISPLAY_CMD_DELAY);

            // Draw message
            m_display->drawText(0, 16, "No iperf3 servers");
            m_display->drawText(0, 26, "found on network");
            usleep(Config::DISPLAY_CMD_DELAY);

            // Draw Back option
            std::string backText = ">Back";
            m_display->drawText(0, 46, backText);
            usleep(Config::DISPLAY_CMD_DELAY);
        }
    } else if (!m_discoveryInProgress) {
        // Just update selection markers for discovered servers
        if (!m_discoveredServers.empty()) {
            int numToShow = std::min(static_cast<int>(m_discoveredServers.size()), 5);
            int yPos = 16;

            for (int i = 0; i < numToShow; i++) {
                // First erase the character at position 0 (the selection marker)
                m_display->drawText(0, yPos, " ");
                // Draw selection marker if this item is selected
                if (m_submenuSelection == i) {
                    m_display->drawText(0, yPos, ">");
                }
                usleep(Config::DISPLAY_CMD_DELAY);
                yPos += 10;
            }

            // Update Back selection
            m_display->drawText(0, yPos, (m_submenuSelection == numToShow) ? ">" : " ");
            usleep(Config::DISPLAY_CMD_DELAY);
        }
    }
}

void ThroughputClientScreen::updateStatusLine() {
    std::string statusText;
    int yPos = 76; // Position for status line
    //int yPos = 56; // Position for status line

    // Clear status line
    m_display->drawText(0, yPos, "                ");
    usleep(Config::DISPLAY_CMD_DELAY);

    // Show different status based on current state
    if (m_testInProgress) {
        // Show test progress
        static int dots = 0;
        statusText = "Testing" + std::string(dots, '.');
        dots = (dots + 1) % 4;
    } else if (m_testResult != -1) {
        // Show last test result
        if (m_protocol == "TCP") {
            statusText = formatBandwidth(m_bandwidth_result);
        } else {
            // For UDP, show jitter and packet loss
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%.1f%% loss", m_loss_result);
            statusText = buffer;
        }
    }

    // Draw the status text
    if (!statusText.empty()) {
        m_display->drawText(0, yPos, statusText);
        usleep(Config::DISPLAY_CMD_DELAY);
    }
}

// Input handling
bool ThroughputClientScreen::handleInput() {
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

        // Process button press and rotation
        bool redrawNeeded = false;
        //(void)redrawNeeded;

        // Handle button press based on current state
        if (buttonPressed) {
            switch (m_state) {
                case ThroughputClientState::MENU_STATE_START:
                    // Start test
                    if (!m_testInProgress) {
                        startTest();
                        renderMainMenu(true);
                    }
                    break;

                case ThroughputClientState::MENU_STATE_PROTOCOL:
                    // Enter protocol submenu
                    m_state = ThroughputClientState::SUBMENU_STATE_PROTOCOL;
                    m_submenuSelection = 0;
                    // Find current protocol in options
                    for (size_t i = 0; i < m_protocolOptions.size(); i++) {
                        if (m_protocolOptions[i] == m_protocol) {
                            m_submenuSelection = i;
                            break;
                        }
                    }
                    renderProtocolSubmenu(true);
                    break;

                case ThroughputClientState::MENU_STATE_DURATION:
                    // Enter duration submenu
                    m_state = ThroughputClientState::SUBMENU_STATE_DURATION;
                    m_submenuSelection = 0;
                    // Find current duration in options
                    for (size_t i = 0; i < m_durationOptions.size(); i++) {
                        if (m_durationOptions[i] == m_duration) {
                            m_submenuSelection = i;
                            break;
                        }
                    }
                    renderDurationSubmenu(true);
                    break;

                case ThroughputClientState::MENU_STATE_BANDWIDTH:
                    // Enter bandwidth submenu
                    m_state = ThroughputClientState::SUBMENU_STATE_BANDWIDTH;
                    m_submenuSelection = 0;
                    // Find current bandwidth in options
                    for (size_t i = 0; i < m_bandwidthOptions.size(); i++) {
                        if (m_bandwidthOptions[i] == m_bandwidth) {
                            m_submenuSelection = i;
                            break;
                        }
                    }
                    renderBandwidthSubmenu(true);
                    break;

                case ThroughputClientState::MENU_STATE_PARALLEL:
                    // Enter parallel submenu
                    m_state = ThroughputClientState::SUBMENU_STATE_PARALLEL;
                    m_submenuSelection = 0;
                    // Find current parallel in options
                    for (size_t i = 0; i < m_parallelOptions.size(); i++) {
                        if (m_parallelOptions[i] == m_parallel) {
                            m_submenuSelection = i;
                            break;
                        }
                    }
                    renderParallelSubmenu(true);
                    break;

                case ThroughputClientState::MENU_STATE_SERVER_IP:
                    // Enter server IP submenu
                    m_state = ThroughputClientState::SUBMENU_STATE_SERVER_IP;
                    m_submenuSelection = 0;
                    m_editingIp = false;
                    renderServerIPSubmenu(true);
                    break;

                case ThroughputClientState::MENU_STATE_BACK:
                    // Exit screen
                    m_shouldExit = true;
                    break;

                // Handle submenu states
                case ThroughputClientState::SUBMENU_STATE_PROTOCOL:
                    if (m_submenuSelection < static_cast<int>(m_protocolOptions.size())) {
                        // Select protocol
                        m_protocol = m_protocolOptions[m_submenuSelection];
                        m_state = ThroughputClientState::MENU_STATE_PROTOCOL;
                        renderMainMenu(true);
                    } else {
                        // Back option selected
                        m_state = ThroughputClientState::MENU_STATE_PROTOCOL;
                        renderMainMenu(true);
                    }
                    break;

                case ThroughputClientState::SUBMENU_STATE_DURATION:
                    if (m_submenuSelection < static_cast<int>(m_durationOptions.size())) {
                        // Select duration
                        m_duration = m_durationOptions[m_submenuSelection];
                        m_state = ThroughputClientState::MENU_STATE_DURATION;
                        renderMainMenu(true);
                    } else {
                        // Back option selected
                        m_state = ThroughputClientState::MENU_STATE_DURATION;
                        renderMainMenu(true);
                    }
                    break;

                case ThroughputClientState::SUBMENU_STATE_BANDWIDTH:
                    if (m_submenuSelection < static_cast<int>(m_bandwidthOptions.size())) {
                        // Select bandwidth
                        m_bandwidth = m_bandwidthOptions[m_submenuSelection];
                        m_state = ThroughputClientState::MENU_STATE_BANDWIDTH;
                        renderMainMenu(true);
                    } else {
                        // Back option selected
                        m_state = ThroughputClientState::MENU_STATE_BANDWIDTH;
                        renderMainMenu(true);
                    }
                    break;

                case ThroughputClientState::SUBMENU_STATE_PARALLEL:
                    if (m_submenuSelection < static_cast<int>(m_parallelOptions.size())) {
                        // Select parallel
                        m_parallel = m_parallelOptions[m_submenuSelection];
                        m_state = ThroughputClientState::MENU_STATE_PARALLEL;
                        renderMainMenu(true);
                    } else {
                        // Back option selected
                        m_state = ThroughputClientState::MENU_STATE_PARALLEL;
                        renderMainMenu(true);
                    }
                    break;

                case ThroughputClientState::SUBMENU_STATE_SERVER_IP:
                    if (m_editingIp) {
                        // Handle IP editor button press
                        if (m_ipSelector->handleButton()) {
                            redrawNeeded = true;
                        } else {
                            // IP editing completed
                            m_editingIp = false;
                            m_serverIp = m_ipSelector->getIp();
                            renderServerIPSubmenu(false);
                        }
                    } else if (m_submenuSelection == 0) {
                        // Start editing IP
                        m_editingIp = true;
                        m_ipSelector->setIp(m_serverIp);
                        //m_ipSelector->startEditing();
                        renderServerIPSubmenu(false);
                        redrawNeeded = true;
                    } else if (m_submenuSelection == 1) {
                        // Auto-discover
                        if (isAvahiAvailable()) {
                            m_state = ThroughputClientState::SUBMENU_STATE_AUTO_DISCOVER;
                            m_submenuSelection = 0;
                            startDiscovery();
                            renderAutoDiscoverScreen(true);
                        } else {
                            m_statusMessage = "Avahi not available";
                            m_statusChanged = true;
                        }
                    } else {
                        // Back option selected
                        m_state = ThroughputClientState::MENU_STATE_SERVER_IP;
                        renderMainMenu(true);
                    }
                    break;

                case ThroughputClientState::SUBMENU_STATE_AUTO_DISCOVER:
                    if (!m_discoveryInProgress) {
                        if (!m_discoveredServers.empty()) {
                            if (m_submenuSelection < static_cast<int>(m_discoveredServers.size())) {
                                // Select server
                                selectServer(m_submenuSelection);
                                m_state = ThroughputClientState::MENU_STATE_SERVER_IP;
                                renderMainMenu(true);
                            } else {
                                // Back option selected
                                m_state = ThroughputClientState::SUBMENU_STATE_SERVER_IP;
                                m_submenuSelection = 0;
                                renderServerIPSubmenu(true);
                            }
                        } else {
                            // No servers found, just go back
                            m_state = ThroughputClientState::SUBMENU_STATE_SERVER_IP;
                            m_submenuSelection = 0;
                            renderServerIPSubmenu(true);
                        }
                    }
                    break;
		case ThroughputClientState::MENU_STATE_RESULTS:
		    if (buttonPressed) {
		        // Return to main menu when any button is pressed
		        m_waitingForButtonPress = false;
			m_state = ThroughputClientState::MENU_STATE_START;
		        Logger::debug("ThroughputClientScreen: Button pressed on results screen, returning to main menu");
			renderMainMenu(true);
		    }
		    break;
		case ThroughputClientState::MENU_STATE_TESTING:
		    // Either ignore button presses during testing
		    // or allow cancellation with a confirmation prompt
		    if (buttonPressed && !m_testCancellationPrompt) {
		        m_testCancellationPrompt = true;
		        // Show confirmation prompt
		        m_display->drawText(0, 56, "Cancel test? Press again");
		    } else if (buttonPressed && m_testCancellationPrompt) {
		        // Cancel the test
		        if (m_testPid > 0) {
		            kill(m_testPid, SIGTERM);
		            waitpid(m_testPid, NULL, 0);
		            m_testPid = -1;
		        }
		        m_testInProgress = false;
		        m_testCancellationPrompt = false;
		        m_state = ThroughputClientState::MENU_STATE_START;
		        renderMainMenu(true);
		    }
		    break;
	    }
        }

        // Handle rotation based on current state
        if (rotationDirection != 0) {
            bool handled = false;

            // Special case for IP selector
            if (m_state == ThroughputClientState::SUBMENU_STATE_SERVER_IP && m_editingIp) {
                handled = m_ipSelector->handleRotation(rotationDirection);
                redrawNeeded = handled;
                if (!handled && rotationDirection > 0) {
        	    // Exit IP editing mode
	            m_editingIp = false;
        	    // Move selection to the next item
	            m_submenuSelection = 1;  // Auto-Discover
	            // Redraw the menu with new selection
	            renderServerIPSubmenu(false);
	            redrawNeeded = true;
	            handled = true;
	        } 
	    }
		if (m_state == ThroughputClientState::MENU_STATE_TESTING ||
		(m_state == ThroughputClientState::MENU_STATE_RESULTS && m_waitingForButtonPress)) {
		// Ignore rotation when testing or viewing results
	        handled = true;
		}

            // If not handled by IP selector, handle menu navigation
            if (!handled) {
                if (m_state == ThroughputClientState::MENU_STATE_START ||
                    m_state == ThroughputClientState::MENU_STATE_PROTOCOL ||
                    m_state == ThroughputClientState::MENU_STATE_DURATION ||
                    m_state == ThroughputClientState::MENU_STATE_BANDWIDTH ||
                    m_state == ThroughputClientState::MENU_STATE_PARALLEL ||
                    m_state == ThroughputClientState::MENU_STATE_SERVER_IP ||
                    m_state == ThroughputClientState::MENU_STATE_BACK) {

                    // Main menu navigation
                    if (rotationDirection < 0) {
                        // Up
                        switch (m_state) {
                            case ThroughputClientState::MENU_STATE_START:
                                m_state = ThroughputClientState::MENU_STATE_BACK;
                                break;
                            case ThroughputClientState::MENU_STATE_PROTOCOL:
                                m_state = ThroughputClientState::MENU_STATE_START;
                                break;
                            case ThroughputClientState::MENU_STATE_DURATION:
                                m_state = ThroughputClientState::MENU_STATE_PROTOCOL;
                                break;
                            case ThroughputClientState::MENU_STATE_BANDWIDTH:
                                m_state = ThroughputClientState::MENU_STATE_DURATION;
                                break;
                            case ThroughputClientState::MENU_STATE_PARALLEL:
                                m_state = ThroughputClientState::MENU_STATE_BANDWIDTH;
                                break;
                            case ThroughputClientState::MENU_STATE_SERVER_IP:
                                m_state = ThroughputClientState::MENU_STATE_PARALLEL;
                                break;
                            case ThroughputClientState::MENU_STATE_BACK:
                                m_state = ThroughputClientState::MENU_STATE_SERVER_IP;
                                break;
                            default:
                                break;
                        }
                    } else {
                        // Down
                        switch (m_state) {
                            case ThroughputClientState::MENU_STATE_START:
                                m_state = ThroughputClientState::MENU_STATE_PROTOCOL;
                                break;
                            case ThroughputClientState::MENU_STATE_PROTOCOL:
                                m_state = ThroughputClientState::MENU_STATE_DURATION;
                                break;
                            case ThroughputClientState::MENU_STATE_DURATION:
                                m_state = ThroughputClientState::MENU_STATE_BANDWIDTH;
                                break;
                            case ThroughputClientState::MENU_STATE_BANDWIDTH:
                                m_state = ThroughputClientState::MENU_STATE_PARALLEL;
                                break;
                            case ThroughputClientState::MENU_STATE_PARALLEL:
                                m_state = ThroughputClientState::MENU_STATE_SERVER_IP;
                                break;
                            case ThroughputClientState::MENU_STATE_SERVER_IP:
                                m_state = ThroughputClientState::MENU_STATE_BACK;
                                break;
                            case ThroughputClientState::MENU_STATE_BACK:
                                m_state = ThroughputClientState::MENU_STATE_START;
                                break;
                            default:
                                break;
                        }
                    }
                    renderMainMenu(false);
                    redrawNeeded = true;
                } else if (m_state == ThroughputClientState::SUBMENU_STATE_PROTOCOL) {
                    // Protocol submenu navigation
                    int numOptions = static_cast<int>(m_protocolOptions.size() + 1); // +1 for Back
                    m_submenuSelection = (m_submenuSelection + numOptions + rotationDirection) % numOptions;
                    renderProtocolSubmenu(false);
                    redrawNeeded = true;
                } else if (m_state == ThroughputClientState::SUBMENU_STATE_DURATION) {
                    // Duration submenu navigation
                    int numOptions = static_cast<int>(m_durationOptions.size() + 1); // +1 for Back

		    //m_submenuSelection = (m_submenuSelection + numOptions + rotationDirection) % numOptions;
		    if (rotationDirection < 0) {
                    // Move up by 1
                         m_submenuSelection = (m_submenuSelection - 1 + numOptions) % numOptions;
                    } else {
                    // Move down by 1
                     m_submenuSelection = (m_submenuSelection + 1) % numOptions;
		    }
		    renderDurationSubmenu(false);
                    redrawNeeded = true;
                } else if (m_state == ThroughputClientState::SUBMENU_STATE_BANDWIDTH) {
                    // Bandwidth submenu navigation
                    int numOptions = static_cast<int>(m_bandwidthOptions.size() + 1); // +1 for Back
                    //m_submenuSelection = (m_submenuSelection + numOptions + rotationDirection) % numOptions;
                    if(rotationDirection < 0) m_submenuSelection = (m_submenuSelection - 1 + numOptions) % numOptions;
		    else m_submenuSelection = (m_submenuSelection + 1) % numOptions;
		    renderBandwidthSubmenu(false);
                    redrawNeeded = true;
                } else if (m_state == ThroughputClientState::SUBMENU_STATE_PARALLEL) {
                    // Parallel submenu navigation
                    int numOptions = static_cast<int>(m_parallelOptions.size() + 1); // +1 for Back
                    //m_submenuSelection = (m_submenuSelection + numOptions + rotationDirection) % numOptions;
                    if(rotationDirection < 0) m_submenuSelection = (m_submenuSelection - 1 + numOptions) % numOptions;
		    else m_submenuSelection = (m_submenuSelection + 1) % numOptions;
                    renderParallelSubmenu(false);
                    redrawNeeded = true;
                } else if (m_state == ThroughputClientState::SUBMENU_STATE_SERVER_IP && !m_editingIp) {
                    // Server IP submenu navigation
                    int numOptions = 3; // IP, Auto-Discover, Back
                    m_submenuSelection = (m_submenuSelection + numOptions + rotationDirection) % numOptions;
                    renderServerIPSubmenu(false);
                    redrawNeeded = true;
                } else if (m_state == ThroughputClientState::SUBMENU_STATE_AUTO_DISCOVER && !m_discoveryInProgress) {
                    // Auto-discover results navigation
                    int numOptions = !m_discoveredServers.empty() ?
                                   static_cast<int>(m_discoveredServers.size() + 1) : 1; // +1 for Back
                    m_submenuSelection = (m_submenuSelection + numOptions + rotationDirection) % numOptions;
                    renderAutoDiscoverScreen(false);
                    redrawNeeded = true;
                }
            }
        }
    }

    // Return false if we should exit
    return !m_shouldExit;
}

// Process execution methods
void ThroughputClientScreen::startTest() {
    // Normalize the IP address (remove leading zeros)
    std::string normalizedIp = normalizeIp(m_serverIp);
    m_serverIp = normalizedIp;
    Logger::debug("ThroughputClientScreen: Using normalized IP: " + normalizedIp);

    // In startTest() method, add detailed command logging
    std::string cmdLine = getIperf3Path() + " -c " + m_serverIp + " -p " + std::to_string(m_serverPort) +
                    " -t " + std::to_string(m_duration) + " -J";
    if (m_protocol == "UDP") cmdLine += " -u";
    if (m_bandwidth > 0) cmdLine += " -b " + std::to_string(m_bandwidth) + "m";
    if (m_parallel > 1) cmdLine += " -P " + std::to_string(m_parallel);
    Logger::debug("ThroughputClientScreen: Executing: " + cmdLine);

    if (m_testInProgress) return;

    // Check if iperf3 is available
    if (!isIperf3Available()) {
        Logger::error("ThroughputClientScreen: iperf3 not found");
        m_statusMessage = "iperf3 not found";
        m_statusChanged = true;
        return;
    }

    // Reset test state
    m_testResult = -1;
    m_bandwidth_result = 0.0;
    m_jitter_result = 0.0;
    m_loss_result = 0.0;
    m_retransmits_result = 0;
    m_testInProgress = true;
    m_statusChanged = true;

    // Change state to testing and show testing screen
    m_state = ThroughputClientState::MENU_STATE_TESTING;
    renderTestingScreen();

    Logger::debug("ThroughputClientScreen: Starting iperf3 test to " + m_serverIp);

    // Fork to run iperf3 client
    pid_t child_pid = fork();
    if (child_pid == 0) {
        // Child process - run iperf3 client
        // Prepare command arguments
        std::vector<std::string> args;
        args.push_back("iperf3");
        args.push_back("-c");
        args.push_back(m_serverIp);
        args.push_back("-p");
        args.push_back(std::to_string(m_serverPort));
        args.push_back("-t");
        args.push_back(std::to_string(m_duration));
        args.push_back("-J"); // JSON output
        // Add protocol flag if UDP
        if (m_protocol == "UDP") {
            args.push_back("-u");
        }
        // Add bandwidth flag if not Auto
        if (m_bandwidth > 0) {
            args.push_back("-b");
            args.push_back(std::to_string(m_bandwidth) + "m");
        }
        // Add parallel flag if not 1
        if (m_parallel > 1) {
            args.push_back("-P");
            args.push_back(std::to_string(m_parallel));
        }
        // Create C-style arguments array
        std::vector<char*> c_args;
        for (const auto& arg : args) {
            c_args.push_back(const_cast<char*>(arg.c_str()));
        }
        c_args.push_back(nullptr);
        // Redirect stdout to temporary file to capture JSON output
        int outFile = open("/tmp/micropanel_iperf_result.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (outFile != -1) {
            dup2(outFile, STDOUT_FILENO);
            close(outFile);
        }
        // Execute iperf3 with the appropriate arguments
        execvp(getIperf3Path().c_str(), c_args.data());
        // If execvp returns, it failed
        Logger::error("ThroughputClientScreen: Failed to execute iperf3");
        std::exit(1);
    } else if (child_pid > 0) {
        // Parent process
        m_testPid = child_pid;
        Logger::info("ThroughputClientScreen: Started iperf3 client with PID " + std::to_string(child_pid));
    } else {
        // Fork failed
        Logger::error("ThroughputClientScreen: Failed to fork for iperf3 client");
        m_testInProgress = false;
        m_statusMessage = "Failed to start test";
        m_statusChanged = true;
        // Return to main menu if test failed to start
        m_state = ThroughputClientState::MENU_STATE_START;
        renderMainMenu(true);
    }
}

void ThroughputClientScreen::checkTestStatus() {
    if (!m_testInProgress) return;

    // Check if the test process has completed
    int status;
    pid_t result = waitpid(m_testPid, &status, WNOHANG);

    if (result == m_testPid) {
        // Test process has completed
        m_testInProgress = false;
        m_testPid = -1;

        // Determine test result
        if (WIFEXITED(status)) {
            m_testResult = WEXITSTATUS(status);
            Logger::debug("ThroughputClientScreen: iperf3 test completed with status " +
                         std::to_string(m_testResult));

            // Parse results from the temporary file if test succeeded
            if (m_testResult == 0) {
                parseTestResults();
                Logger::info("ThroughputClientScreen: Test results - Bandwidth: " +
                            std::to_string(m_bandwidth_result) + " Mbps");

                // Switch to results screen - ONLY change state and render
                m_state = ThroughputClientState::MENU_STATE_RESULTS;
                m_waitingForButtonPress = true;  // Add this flag to your class
                showResultsScreen();  // Renamed to better reflect what it does
                Logger::debug("ThroughputClientScreen: Waiting for button press on results screen");

                // IMPORTANT: Do NOT call renderMainMenu or anything else here
            } else {
                Logger::warning("ThroughputClientScreen: iperf3 client exited with error code " +
                               std::to_string(m_testResult));
                m_statusMessage = "Test failed";
                m_statusChanged = true;
                m_state = ThroughputClientState::MENU_STATE_START;
                renderMainMenu(true);
            }
        } else {
            m_testResult = 1; // Error
            Logger::warning("ThroughputClientScreen: iperf3 client terminated abnormally");
            m_statusMessage = "Test terminated";
            m_statusChanged = true;
            m_state = ThroughputClientState::MENU_STATE_START;
            renderMainMenu(true);
        }
    }
}
void ThroughputClientScreen::parseTestResults() {
    // Read the JSON output file
    std::ifstream file("/tmp/micropanel_iperf_result.txt");
    if (!file.is_open()) {
        Logger::error("ThroughputClientScreen: Failed to open test results file");
        return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string output = buffer.str();
    file.close();

    // Clean up the file
    unlink("/tmp/micropanel_iperf_result.txt");

    // Parse the JSON structure to extract result data
    try {
        // For TCP tests, look for bits_per_second in the end->sum_sent section
        // Based on the actual JSON structure
        size_t sumSentPos = output.find("\"sum_sent\":");
        if (sumSentPos != std::string::npos) {
            size_t bpsPos = output.find("\"bits_per_second\":", sumSentPos);
            if (bpsPos != std::string::npos) {
                size_t valueStart = bpsPos + 18; // Length of "\"bits_per_second\":"
                size_t valueEnd = output.find(",", valueStart);
                if (valueEnd != std::string::npos) {
                    std::string bpsStr = output.substr(valueStart, valueEnd - valueStart);
                    try {
                        // Convert from bits/sec to Mbits/sec
                        double bps = std::stod(bpsStr);
                        m_bandwidth_result = bps / 1000000.0;
                        Logger::debug("ThroughputClientScreen: Parsed bandwidth: " +
                                     std::to_string(m_bandwidth_result) + " Mbps");
                    } catch (const std::exception& e) {
                        Logger::error("ThroughputClientScreen: Failed to parse bandwidth: " +
                                     std::string(e.what()));
                    }
                }
            }

            // Look for retransmits (TCP only)
            if (m_protocol == "TCP") {
                size_t retransPos = output.find("\"retransmits\":", sumSentPos);
                if (retransPos != std::string::npos) {
                    size_t valueStart = retransPos + 14; // Length of "\"retransmits\":"
                    size_t valueEnd = output.find(",", valueStart);
                    if (valueEnd != std::string::npos) {
                        std::string retransStr = output.substr(valueStart, valueEnd - valueStart);
                        try {
                            m_retransmits_result = std::stoi(retransStr);
                            Logger::debug("ThroughputClientScreen: Parsed retransmits: " +
                                         std::to_string(m_retransmits_result));
                        } catch (const std::exception& e) {
                            Logger::error("ThroughputClientScreen: Failed to parse retransmits: " +
                                         std::string(e.what()));
                        }
                    }
                }
            }
        }

        // For UDP tests, look for jitter and lost packets
	if (m_protocol == "UDP") {
		UDPTestResult udpResult = parseUDPTestResults(output);
		if (udpResult.valid) {
			// Store the results in class members
			m_bandwidth_result = udpResult.bandwidth_mbps;
			m_jitter_result = udpResult.jitter_ms;
			m_loss_result = udpResult.lost_percent;  // Match the variable used in showResultsAndWait

			// Log the results
		        Logger::info("ThroughputClientScreen: UDP Test results - "
				"Bandwidth: " + std::to_string(m_bandwidth_result) + " Mbps, "
				"Jitter: " + std::to_string(m_jitter_result) + " ms, "
				"Loss: " + std::to_string(m_loss_result) + "%, "
				"Lost packets: " + std::to_string(udpResult.lost_packets) + " / " +
				std::to_string(udpResult.total_packets));
		} else {
			Logger::warning("ThroughputClientScreen: Failed to parse UDP test results");
		}
	}

    } catch (const std::exception& e) {
        Logger::error("ThroughputClientScreen: Exception parsing results: " + std::string(e.what()));
    }

    Logger::info("ThroughputClientScreen: Test results - Bandwidth: " +
                std::to_string(m_bandwidth_result) + " Mbps" +
                (m_protocol == "TCP" ? ", Retransmits: " + std::to_string(m_retransmits_result) : ""));
}

void ThroughputClientScreen::startDiscovery() {
    if (m_discoveryInProgress) return;

    // Check if avahi is available
    if (!isAvahiAvailable()) {
        Logger::error("ThroughputClientScreen: avahi-browse not found");
        m_statusMessage = "Avahi not available";
        m_statusChanged = true;
        return;
    }

    // Reset discovery state
    m_discoveredServers.clear();
    m_discoveredServerNames.clear();
    m_discoveryInProgress = true;
    m_statusChanged = true;

    Logger::debug("ThroughputClientScreen: Starting Avahi discovery");

    // Fork to run avahi-browse
    pid_t child_pid = fork();

    if (child_pid == 0) {
        // Child process - run avahi-browse

        // Redirect stdout to temporary file to capture output
        int outFile = open("/tmp/micropanel_avahi_result.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (outFile != -1) {
            dup2(outFile, STDOUT_FILENO);
            close(outFile);
        }

        // Execute avahi-browse to search for iperf3 services
        // Add -t to terminate after resolving and ensure -p for parsable output
        execl("/usr/bin/avahi-browse", "avahi-browse", "-p", "-t", "-r", "_iperf3._tcp", NULL);

        // If execl returns, it failed
        Logger::error("ThroughputClientScreen: Failed to execute avahi-browse");
        std::exit(1);
    } else if (child_pid > 0) {
        // Parent process
        m_discoveryPid = child_pid;
        Logger::info("ThroughputClientScreen: Started avahi-browse with PID " + std::to_string(child_pid));
    } else {
        // Fork failed
        Logger::error("ThroughputClientScreen: Failed to fork for avahi-browse");
        m_discoveryInProgress = false;
        m_statusMessage = "Discovery failed";
        m_statusChanged = true;
    }
}

void ThroughputClientScreen::checkDiscoveryStatus() {
    if (!m_discoveryInProgress) return;

    // Check if the discovery process has completed
    int status;
    pid_t result = waitpid(m_discoveryPid, &status, WNOHANG);

    if (result == m_discoveryPid) {
        // Discovery process has completed
        m_discoveryInProgress = false;
        m_discoveryPid = -1;

        // Determine discovery result
        if (WIFEXITED(status)) {
            int exitStatus = WEXITSTATUS(status);
            Logger::debug("ThroughputClientScreen: avahi-browse completed with status " +
                         std::to_string(exitStatus));

            // Parse results from the temporary file
            parseDiscoveryResults();

            if (m_discoveredServers.empty()) {
                Logger::warning("ThroughputClientScreen: No iperf3 servers found");
                m_statusMessage = "No servers found";
                m_statusChanged = true;
            } else {
                Logger::info("ThroughputClientScreen: Found " +
                            std::to_string(m_discoveredServers.size()) + " iperf3 servers");
            }
        } else {
            Logger::warning("ThroughputClientScreen: avahi-browse terminated abnormally");
            m_statusMessage = "Discovery failed";
            m_statusChanged = true;
        }

        // Update display with discovery results
        renderAutoDiscoverScreen(true);
    }
}
void ThroughputClientScreen::parseDiscoveryResults() {
    std::ifstream file("/tmp/micropanel_avahi_result.txt");
    if (!file.is_open()) {
        Logger::error("ThroughputClientScreen: Failed to open discovery results file");
        return;
    }

    // Debug: Log the entire file content
    std::string allContent((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    Logger::debug("ThroughputClientScreen: Discovery file content:\n" + allContent);

    // Reset file position to beginning
    file.clear();
    file.seekg(0);

    // Track IPv4 addresses we've seen to avoid duplicates
    std::set<std::string> seenIPs;

    // First, find all IPv4 entries
    std::string line;
    std::vector<std::string> ipv4Lines;

    while (std::getline(file, line)) {
        // Look specifically for IPv4 lines (avoid IPv6)
        if (line.empty()) continue;

        if (line.find(";IPv4;") != std::string::npos) {
            ipv4Lines.push_back(line);
        }
    }

    // Now process each IPv4 line
    for (const auto& ipv4Line : ipv4Lines) {
        std::vector<std::string> fields;
        std::string field;
        std::istringstream lineStream(ipv4Line);

        while (std::getline(lineStream, field, ';')) {
            fields.push_back(field);
        }

        // Must have at least basic fields
        if (fields.size() < 4) continue;

        // Extract service name
        std::string serviceName = fields[3];
        // Unescape special characters
        serviceName = std::regex_replace(serviceName, std::regex("\\\\032"), " ");
        //serviceName = std::regex_replace(serviceName, std::regex("\\\\\."), ".");
	serviceName = std::regex_replace(serviceName, std::regex("\\\\."), ".");
        // Try to extract IP from service name (common format)
        std::string ipAddress;
        size_t lastSpace = serviceName.find_last_of(' ');
        if (lastSpace != std::string::npos) {
            std::string possibleIP = serviceName.substr(lastSpace + 1);
            // Simple IPv4 validation (contains dots, no colons)
            if (possibleIP.find('.') != std::string::npos &&
                possibleIP.find(':') == std::string::npos) {
                ipAddress = possibleIP;
            }
        }

        // If we couldn't extract from service name, look for IP in resolved entries
        if (ipAddress.empty() && fields[0] == "=") {
            // In resolved entries, IP should be in field 7
            if (fields.size() >= 8) {
                std::string possibleIP = fields[7];
                if (possibleIP.find('.') != std::string::npos &&
                    possibleIP.find(':') == std::string::npos) {
                    ipAddress = possibleIP;
                }
            }
        }

        // Skip if no valid IP found or already processed this IP
        if (ipAddress.empty() || seenIPs.find(ipAddress) != seenIPs.end()) {
            continue;
        }

        // Try to extract port number
        int port = 5201; // Default iperf3 port
        if (fields[0] == "=" && fields.size() >= 9) {
            try {
                port = std::stoi(fields[8]);
            } catch (...) {
                Logger::warning("ThroughputClientScreen: Failed to parse port, using default");
            }
        }

        // Record this IP
        seenIPs.insert(ipAddress);

        // Add to discovered servers
        m_discoveredServers.push_back(std::make_pair(ipAddress, port));
        m_discoveredServerNames.push_back(serviceName);

        Logger::debug("ThroughputClientScreen: Discovered server - " +
                      ipAddress + ":" + std::to_string(port) + " (" + serviceName + ")");
    }

    file.close();

    // Clean up temporary file
    unlink("/tmp/micropanel_avahi_result.txt");
}
void ThroughputClientScreen::selectServer(int index) {
    if (index >= 0 && index < static_cast<int>(m_discoveredServers.size())) {
        // Get selected server
        m_serverIp = m_discoveredServers[index].first;
        m_serverPort = m_discoveredServers[index].second;

        // Update IP selector
        if (m_ipSelector) {
            m_ipSelector->setIp(m_serverIp);
        }

        Logger::info("ThroughputClientScreen: Selected server: " +
                    m_serverIp + ":" + std::to_string(m_serverPort));
    }
}

std::string ThroughputClientScreen::normalizeIp(const std::string& ip) {
    std::string normalized;
    std::istringstream iss(ip);
    std::string octet;

    while (std::getline(iss, octet, '.')) {
        // Remove leading zeros and convert back to integer
        int value = std::stoi(octet);

        // Add to normalized string with dots
        if (!normalized.empty()) {
            normalized += ".";
        }
        normalized += std::to_string(value);
    }

    return normalized;
}

void ThroughputClientScreen::showResultsAndWait(){//int durationMs) {
    // Draw the results screen
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 3);

    // Draw header
    m_display->drawText(0, 0, "   Test Results");
    usleep(Config::DISPLAY_CMD_DELAY);

    // Draw separator
    m_display->drawText(0, 8, "----------------");
    usleep(Config::DISPLAY_CMD_DELAY);

    // Draw protocol
    m_display->drawText(0, 16, "Protocol: " + m_protocol);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Draw bandwidth result
    std::string bwStr = formatBandwidth(m_bandwidth_result);
    m_display->drawText(0, 24, "Speed: " + bwStr);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Draw additional results based on protocol
    if (m_protocol == "TCP") {
        m_display->drawText(0, 32, "Retrans: " + std::to_string(m_retransmits_result));
    } else { // UDP
        m_display->drawText(0, 32, "Loss: " + std::to_string(m_loss_result) + "%");
        m_display->drawText(0, 40, "Jitter: " + std::to_string(m_jitter_result) + "ms");
    }
    usleep(Config::DISPLAY_CMD_DELAY);

    // Draw message
    m_display->drawText(0, 56, "Please wait...");
    usleep(Config::DISPLAY_CMD_DELAY);

    // Wait for specified duration
    //Logger::debug("ThroughputClientScreen: Showing results for " + std::to_string(durationMs) + "ms");

    // Sleep for the requested duration
    //usleep(durationMs * 1000);

    Logger::debug("ThroughputClientScreen: Done showing results");
}

UDPTestResult ThroughputClientScreen::parseUDPTestResults(const std::string& output) {
    UDPTestResult result;

    try {
        // Look for the UDP metrics in the end->sum section
        size_t endPos = output.find("\"end\":");
        if (endPos == std::string::npos) {
            Logger::warning("ThroughputClientScreen: Cannot find end section in UDP results");
            return result;
        }

        size_t sumPos = output.find("\"sum\":", endPos);
        if (sumPos == std::string::npos) {
            Logger::warning("ThroughputClientScreen: Cannot find sum section in UDP results");
            return result;
        }

        // Extract bandwidth (bits_per_second)
        size_t bpsPos = output.find("\"bits_per_second\":", sumPos);
        if (bpsPos != std::string::npos) {
            size_t valueStart = bpsPos + 18; // Length of "\"bits_per_second\":"
            size_t valueEnd = output.find(",", valueStart);
            if (valueEnd != std::string::npos) {
                std::string bpsStr = output.substr(valueStart, valueEnd - valueStart);
                // Convert from bits/sec to Mbits/sec
                double bps = std::stod(bpsStr);
                result.bandwidth_mbps = bps / 1000000.0;
                Logger::debug("ThroughputClientScreen: Parsed UDP bandwidth: " +
                             std::to_string(result.bandwidth_mbps) + " Mbps");
            }
        }

        // Extract jitter
        size_t jitterPos = output.find("\"jitter_ms\":", sumPos);
        if (jitterPos != std::string::npos) {
            size_t valueStart = jitterPos + 12; // Length of "\"jitter_ms\":"
            size_t valueEnd = output.find(",", valueStart);
            if (valueEnd != std::string::npos) {
                std::string jitterStr = output.substr(valueStart, valueEnd - valueStart);
                result.jitter_ms = std::stod(jitterStr);
                Logger::debug("ThroughputClientScreen: Parsed jitter: " +
                             std::to_string(result.jitter_ms) + " ms");
            }
        }

        // Extract lost packets
        size_t lostPacketsPos = output.find("\"lost_packets\":", sumPos);
        if (lostPacketsPos != std::string::npos) {
            size_t valueStart = lostPacketsPos + 15; // Length of "\"lost_packets\":"
            size_t valueEnd = output.find(",", valueStart);
            if (valueEnd != std::string::npos) {
                std::string lostPacketsStr = output.substr(valueStart, valueEnd - valueStart);
                result.lost_packets = std::stoi(lostPacketsStr);
                Logger::debug("ThroughputClientScreen: Parsed lost packets: " +
                             std::to_string(result.lost_packets));
            }
        }

        // Extract lost percent
        size_t lostPercentPos = output.find("\"lost_percent\":", sumPos);
        if (lostPercentPos != std::string::npos) {
            size_t valueStart = lostPercentPos + 15; // Length of "\"lost_percent\":"
            size_t valueEnd = output.find(",", valueStart);
            if (valueEnd != std::string::npos) {
                std::string lostPercentStr = output.substr(valueStart, valueEnd - valueStart);
                result.lost_percent = std::stod(lostPercentStr);
                Logger::debug("ThroughputClientScreen: Parsed packet loss: " +
                             std::to_string(result.lost_percent) + "%");
            }
        }

        // Extract total packets
        size_t packetsPos = output.find("\"packets\":", sumPos);
        if (packetsPos != std::string::npos) {
            size_t valueStart = packetsPos + 10; // Length of "\"packets\":"
            size_t valueEnd = output.find(",", valueStart);
            if (valueEnd != std::string::npos) {
                std::string packetsStr = output.substr(valueStart, valueEnd - valueStart);
                result.total_packets = std::stoi(packetsStr);
                Logger::debug("ThroughputClientScreen: Parsed total packets: " +
                             std::to_string(result.total_packets));
            }
        }

        // Mark as valid if we parsed the essential data
        result.valid = (result.bandwidth_mbps > 0);

    } catch (const std::exception& e) {
        Logger::error("ThroughputClientScreen: Exception parsing UDP results: " + std::string(e.what()));
    }

    return result;
}
void ThroughputClientScreen::showResultsScreen() {
    // Draw the results screen
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 3);

    // Draw header
    m_display->drawText(0, 0, "   Test Results");
    usleep(Config::DISPLAY_CMD_DELAY);

    // Draw separator
    m_display->drawText(0, 8, "----------------");
    usleep(Config::DISPLAY_CMD_DELAY);

    // Draw protocol
    m_display->drawText(0, 16, "Protocol: " + m_protocol);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Draw bandwidth result
    std::string bwStr = formatBandwidth(m_bandwidth_result);
    m_display->drawText(0, 24, "Speed: " + bwStr);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Draw additional results based on protocol
    if (m_protocol == "TCP") {
        m_display->drawText(0, 32, "Retrans: " + std::to_string(m_retransmits_result));
    } else { // UDP
        m_display->drawText(0, 32, "Loss: " + std::to_string(m_loss_result) + "%");
        m_display->drawText(0, 40, "Jitter: " + std::to_string(m_jitter_result) + " ms");
    }
    usleep(Config::DISPLAY_CMD_DELAY);

    // Draw press button message
    m_display->drawText(0, 56, "Enter to continu");
    usleep(Config::DISPLAY_CMD_DELAY);
}
void ThroughputClientScreen::renderTestingScreen() {
    // Clear the screen
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 3);

    // Draw header
    m_display->drawText(0, 0, "    Testing");
    usleep(Config::DISPLAY_CMD_DELAY);

    // Draw separator
    m_display->drawText(0, 8, "----------------");
    usleep(Config::DISPLAY_CMD_DELAY);

    // Show test parameters
    m_display->drawText(0, 16, "Protocol: " + m_protocol);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Show server info
    m_display->drawText(0, 24, "Server: " + m_serverIp);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Show duration
    m_display->drawText(0, 32, "Duration: " + std::to_string(m_duration) + " sec");
    usleep(Config::DISPLAY_CMD_DELAY);

    // Show other parameters
    if (m_protocol == "UDP" && m_bandwidth > 0) {
        m_display->drawText(0, 40, "Rate: " + std::to_string(m_bandwidth) + " Mbps");
        usleep(Config::DISPLAY_CMD_DELAY);
    } else if (m_protocol == "UDP") {
        m_display->drawText(0, 40, "Rate: Auto");
        usleep(Config::DISPLAY_CMD_DELAY);
    }

    if (m_parallel > 1) {
        m_display->drawText(0, 48, "Streams: " + std::to_string(m_parallel));
        usleep(Config::DISPLAY_CMD_DELAY);
    }

    // Show progress message
    m_display->drawText(0, 56, "Please wait...");
    usleep(Config::DISPLAY_CMD_DELAY);

    Logger::debug("ThroughputClientScreen: Showing testing screen");
}
