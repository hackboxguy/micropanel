#include "ScreenModules.h"
#include "MenuSystem.h"
#include "DeviceInterfaces.h"
#include "Config.h"
#include "Logger.h"
#include <iostream>
#include <unistd.h>
#include <vector>
#include <algorithm>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <string.h>
#include <dirent.h>

// Structure to hold network interface information
struct InterfaceInfo {
    std::string name;
    bool linkUp;
    std::string ipAddress;
    std::string macAddress;
    std::string netmask;
};

// PIMPL implementation for NetInfoScreen
class NetInfoScreen::Impl {
public:
    Impl(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input)
        : m_display(display), m_input(input) {}

    // Interface list management
    std::vector<InterfaceInfo> m_interfaces;
    bool m_inSubmenu = false;
    int m_selectedInterface = 0;
    int m_previousSelection = -1;
    
    // Scrolling
    int m_scrollOffset = 0;
    // Use enum instead of static constexpr for constants
    enum { MAX_VISIBLE_ITEMS = 6 }; // Max number of items visible on screen
    enum { REFRESH_INTERVAL = 5 };  // Refresh interval in seconds
    
    // Timing for interface refresh
    time_t m_lastRefreshTime = 0;
    
    // State flags
    bool m_shouldExit = false;

    // Reference to display and input devices
    std::shared_ptr<Display> m_display;
    std::shared_ptr<InputDevice> m_input;

    // Network interface methods
    void refreshInterfaceList();
    bool getInterfaceDetails(InterfaceInfo& interface);
    bool checkLinkStatus(const std::string& interfaceName);
    bool getMacAddress(const std::string& interfaceName, std::string& macAddress);
    
    // Drawing methods
    void renderMenu(bool fullRedraw);
    void updateSelection(int oldSelection, int newSelection);
    void renderInterfaceDetails(int interfaceIndex);
};

// Implementation of NetInfoScreen methods

NetInfoScreen::NetInfoScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input)
    : ScreenModule(display, input),
      m_pImpl(std::make_unique<Impl>(display, input))
{
    // Constructor - Nothing additional needed
}

NetInfoScreen::~NetInfoScreen() = default;

void NetInfoScreen::enter()
{
    Logger::debug("NetInfoScreen: Entered");
    
    // Reset state
    m_pImpl->m_inSubmenu = false;
    m_pImpl->m_selectedInterface = 0;
    m_pImpl->m_previousSelection = -1;
    m_pImpl->m_scrollOffset = 0;
    m_pImpl->m_shouldExit = false;
    
    // Get initial interface list
    m_pImpl->refreshInterfaceList();
    
    // Render the main menu
    m_pImpl->renderMenu(true);
}

void NetInfoScreen::update()
{
    // Periodically refresh interface list
    time_t currentTime = time(nullptr);
    if (currentTime - m_pImpl->m_lastRefreshTime >= m_pImpl->REFRESH_INTERVAL) {
        // Save current selection
        std::string selectedName;
        if (!m_pImpl->m_interfaces.empty() && m_pImpl->m_selectedInterface >= 0 && 
            m_pImpl->m_selectedInterface < static_cast<int>(m_pImpl->m_interfaces.size()) - 1) {
            selectedName = m_pImpl->m_interfaces[m_pImpl->m_selectedInterface].name;
        }
        
        // Refresh the interface list
        m_pImpl->refreshInterfaceList();
        
        // Try to restore selection if possible
        if (!selectedName.empty()) {
            for (size_t i = 0; i < m_pImpl->m_interfaces.size() - 1; i++) {
                if (m_pImpl->m_interfaces[i].name == selectedName) {
                    m_pImpl->m_selectedInterface = i;
                    break;
                }
            }
        }
        
        // Update display if we're in the main menu
        if (!m_pImpl->m_inSubmenu) {
            m_pImpl->renderMenu(true);
        } else if (m_pImpl->m_selectedInterface < static_cast<int>(m_pImpl->m_interfaces.size()) - 1) {
            // Update the details screen if we're in submenu
            m_pImpl->renderInterfaceDetails(m_pImpl->m_selectedInterface);
        }
        
        m_pImpl->m_lastRefreshTime = currentTime;
    }
}

void NetInfoScreen::exit()
{
    Logger::debug("NetInfoScreen: Exiting");
    
    // Clear display
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 3);
}

bool NetInfoScreen::handleInput()
{
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
            if (m_pImpl->m_inSubmenu) {
                // In submenu, go back to main menu
                m_pImpl->m_inSubmenu = false;
                m_pImpl->renderMenu(true);
            } else {
                // In main menu
                if (m_pImpl->m_selectedInterface == static_cast<int>(m_pImpl->m_interfaces.size()) - 1) {
                    // "Back" selected - exit
                    m_pImpl->m_shouldExit = true;
                } else {
                    // Interface selected - show details
                    m_pImpl->m_inSubmenu = true;
                    m_pImpl->renderInterfaceDetails(m_pImpl->m_selectedInterface);
                }
            }
        }
        
        // Handle rotation
        if (!m_pImpl->m_inSubmenu && rotationDirection != 0) {
            int oldSelection = m_pImpl->m_selectedInterface;
            
            if (rotationDirection < 0) {
                // Rotate left - move up
                if (m_pImpl->m_selectedInterface > 0) {
                    m_pImpl->m_selectedInterface--;
                }
            } else {
                // Rotate right - move down
                if (m_pImpl->m_selectedInterface < static_cast<int>(m_pImpl->m_interfaces.size()) - 1) {
                    m_pImpl->m_selectedInterface++;
                }
            }
            
            // Update display if selection changed
            if (oldSelection != m_pImpl->m_selectedInterface) {
                m_pImpl->updateSelection(oldSelection, m_pImpl->m_selectedInterface);
            }
        }
    }
    
    // Return false to exit the screen
    return !m_pImpl->m_shouldExit;
}

// Implementation of NetInfoScreen::Impl methods

void NetInfoScreen::Impl::refreshInterfaceList()
{
    // Clear existing interfaces
    m_interfaces.clear();
    
    // Get network interfaces
    struct ifaddrs *ifaddr, *ifa;
    
    if (getifaddrs(&ifaddr) == -1) {
        Logger::error("Failed to get interface addresses");
        
        // Add "Back" option even if we failed
        InterfaceInfo backOption;
        backOption.name = "Back";
        m_interfaces.push_back(backOption);
        
        return;
    }
    
    // Find all interfaces
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr)
            continue;
            
        // Skip loopback interfaces
        if (ifa->ifa_flags & IFF_LOOPBACK)
            continue;
            
        // Check if we already have this interface
        bool found = false;
        for (const auto& iface : m_interfaces) {
            if (iface.name == ifa->ifa_name) {
                found = true;
                break;
            }
        }
        
        if (found)
            continue;
            
        // Create new interface info
        InterfaceInfo iface;
        iface.name = ifa->ifa_name;
        iface.linkUp = checkLinkStatus(iface.name);
        
        // Get MAC and populate initial values
        getMacAddress(iface.name, iface.macAddress);
        iface.ipAddress = "<no ip>";
        iface.netmask = "<no netmask>";
        
        // Try to fill in more details
        getInterfaceDetails(iface);
        
        // Add to list
        m_interfaces.push_back(iface);
    }
    
    freeifaddrs(ifaddr);
    
    // Add "Back" option
    InterfaceInfo backOption;
    backOption.name = "Back";
    m_interfaces.push_back(backOption);
    
    // Set last refresh time
    m_lastRefreshTime = time(nullptr);
    
    Logger::debug("Found " + std::to_string(m_interfaces.size() - 1) + " network interfaces");
}

bool NetInfoScreen::Impl::getInterfaceDetails(InterfaceInfo& interface)
{
    struct ifaddrs *ifaddr, *ifa;
    char addrStr[INET_ADDRSTRLEN];
    
    if (getifaddrs(&ifaddr) == -1) {
        return false;
    }
    
    // Find matching interface
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr || ifa->ifa_name == nullptr)
            continue;
            
        if (strcmp(ifa->ifa_name, interface.name.c_str()) != 0)
            continue;
            
        // Found a match
        if (ifa->ifa_addr->sa_family == AF_INET) {
            // IPv4 address
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            struct sockaddr_in *mask = (struct sockaddr_in *)ifa->ifa_netmask;
            
            // Get IP address
            inet_ntop(AF_INET, &addr->sin_addr, addrStr, sizeof(addrStr));
            interface.ipAddress = addrStr;
            
            // Get netmask
            inet_ntop(AF_INET, &mask->sin_addr, addrStr, sizeof(addrStr));
            interface.netmask = addrStr;
            
            break;
        }
    }
    
    freeifaddrs(ifaddr);
    return true;
}

bool NetInfoScreen::Impl::checkLinkStatus(const std::string& interfaceName)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", interfaceName.c_str());
    
    FILE* fp = fopen(path, "r");
    if (!fp) {
        return false;
    }
    
    char buf[8];
    bool linkUp = false;
    
    if (fgets(buf, sizeof(buf), fp)) {
        // "up" indicates the link is up
        if (strncmp(buf, "up", 2) == 0) {
            linkUp = true;
        }
    }
    
    fclose(fp);
    return linkUp;
}

bool NetInfoScreen::Impl::getMacAddress(const std::string& interfaceName, std::string& macAddress)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }
    
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, interfaceName.c_str(), IFNAMSIZ - 1);
    
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
        close(fd);
        return false;
    }
    
    unsigned char* mac = (unsigned char*)ifr.ifr_hwaddr.sa_data;
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X%02X%02X%02X%02X%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    macAddress = macStr;
    close(fd);
    return true;
}

void NetInfoScreen::Impl::renderMenu(bool fullRedraw)
{
    if (fullRedraw) {
        // Clear the screen
        m_display->clear();
        usleep(Config::DISPLAY_CMD_DELAY * 3);
        
        // Draw header
        m_display->drawText(0, 0, "   Net Info");
        usleep(Config::DISPLAY_CMD_DELAY);
        
        // Draw separator
        m_display->drawText(0, 8, "----------------");
        usleep(Config::DISPLAY_CMD_DELAY);
    }
    
    // Calculate scroll offset to ensure selection is visible
    if (m_selectedInterface < m_scrollOffset) {
        m_scrollOffset = m_selectedInterface;
    } else if (m_selectedInterface >= m_scrollOffset + MAX_VISIBLE_ITEMS) {
        m_scrollOffset = m_selectedInterface - MAX_VISIBLE_ITEMS + 1;
    }
    
    // Ensure scroll offset is within valid range
    if (m_scrollOffset < 0) 
        m_scrollOffset = 0;
    
    int maxOffset = static_cast<int>(m_interfaces.size()) - MAX_VISIBLE_ITEMS;
    if (maxOffset < 0) maxOffset = 0;
    if (m_scrollOffset > maxOffset) 
        m_scrollOffset = maxOffset;
    
    // Calculate number of items to show
    //int itemsToShow = std::min(
    //    static_cast<int>(m_interfaces.size()), 
    //    MAX_VISIBLE_ITEMS
    //);
    int itemsToShow = std::min(
    static_cast<int>(m_interfaces.size()), 
    static_cast<int>(MAX_VISIBLE_ITEMS)
    );

    // Clear the menu area if doing a full redraw
    if (fullRedraw) {
        for (int i = 0; i < MAX_VISIBLE_ITEMS; i++) {
            m_display->drawText(0, 16 + (i * 8), "                ");
            usleep(Config::DISPLAY_CMD_DELAY);
        }
    }
    
    // Draw visible menu items
    for (int i = 0; i < itemsToShow; i++) {
        int itemIndex = i + m_scrollOffset;
        std::string line;
        
        if (itemIndex < static_cast<int>(m_interfaces.size()) - 1) {
            // Regular interface item
            const std::string& name = m_interfaces[itemIndex].name;
            const std::string linkIndicator = m_interfaces[itemIndex].linkUp ? "*" : "";
            line = (m_selectedInterface == itemIndex ? ">" : " ") + name + linkIndicator;
        } else if (itemIndex == static_cast<int>(m_interfaces.size()) - 1) {
            // Back option
            line = (m_selectedInterface == itemIndex ? ">" : " ") + m_interfaces[itemIndex].name;
        }
        
        m_display->drawText(0, 16 + (i * 8), line);
        usleep(Config::DISPLAY_CMD_DELAY);
    }
    
    // Draw scroll indicators if needed
    if (static_cast<int>(m_interfaces.size()) > MAX_VISIBLE_ITEMS) {
        // Clear indicator positions first
        m_display->drawText(122, 16, " ");
        m_display->drawText(122, 16 + ((MAX_VISIBLE_ITEMS - 1) * 8), " ");
        
        // Draw up arrow if there are items above
        if (m_scrollOffset > 0) {
            m_display->drawText(122, 16, "^");
            usleep(Config::DISPLAY_CMD_DELAY);
        }
        
        // Draw down arrow if there are items below
        if (m_scrollOffset + MAX_VISIBLE_ITEMS < static_cast<int>(m_interfaces.size())) {
            m_display->drawText(122, 16 + ((MAX_VISIBLE_ITEMS - 1) * 8), "v");
            usleep(Config::DISPLAY_CMD_DELAY);
        }
    }
}

void NetInfoScreen::Impl::updateSelection(int oldSelection, int newSelection)
{
    // Check if we need to scroll
    bool needScroll = false;
    
    if (newSelection < m_scrollOffset) {
        m_scrollOffset = newSelection;
        needScroll = true;
    } else if (newSelection >= m_scrollOffset + MAX_VISIBLE_ITEMS) {
        m_scrollOffset = newSelection - MAX_VISIBLE_ITEMS + 1;
        needScroll = true;
    }
    
    // If we need to scroll, do a full redraw
    if (needScroll) {
        renderMenu(true);
        return;
    }
    
    // Both selections are visible, just update those lines
    int oldDisplayPos = oldSelection - m_scrollOffset;
    int newDisplayPos = newSelection - m_scrollOffset;
    
    // Only update if positions are within visible range
    if (oldDisplayPos >= 0 && oldDisplayPos < MAX_VISIBLE_ITEMS) {
        // Clear old selection
        std::string line;
        if (oldSelection < static_cast<int>(m_interfaces.size()) - 1) {
            // Regular interface
            const std::string& name = m_interfaces[oldSelection].name;
            const std::string linkIndicator = m_interfaces[oldSelection].linkUp ? "*" : "";
            line = " " + name + linkIndicator;
        } else {
            // Back option
            line = " " + m_interfaces[oldSelection].name;
        }
        
        m_display->drawText(0, 16 + (oldDisplayPos * 8), line);
        usleep(Config::DISPLAY_CMD_DELAY);
    }
    
    if (newDisplayPos >= 0 && newDisplayPos < MAX_VISIBLE_ITEMS) {
        // Set new selection
        std::string line;
        if (newSelection < static_cast<int>(m_interfaces.size()) - 1) {
            // Regular interface
            const std::string& name = m_interfaces[newSelection].name;
            const std::string linkIndicator = m_interfaces[newSelection].linkUp ? "*" : "";
            line = ">" + name + linkIndicator;
        } else {
            // Back option
            line = ">" + m_interfaces[newSelection].name;
        }
        
        m_display->drawText(0, 16 + (newDisplayPos * 8), line);
        usleep(Config::DISPLAY_CMD_DELAY);
    }
}

void NetInfoScreen::Impl::renderInterfaceDetails(int interfaceIndex)
{
    // Ensure interface index is valid
    if (interfaceIndex < 0 || interfaceIndex >= static_cast<int>(m_interfaces.size()) - 1) {
        return;
    }
    
    const InterfaceInfo& iface = m_interfaces[interfaceIndex];
    
    // Clear display
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 3);
    
    // Draw header with interface name
    std::string header = iface.name;
    int headerPos = std::max(0, (16 - static_cast<int>(header.length())) / 2);
    m_display->drawText(headerPos, 0, header);
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Draw separator
    m_display->drawText(0, 8, "----------------");
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Draw link status
    std::string linkStatus = "Link: " + std::string(iface.linkUp ? "Up" : "Down");
    m_display->drawText(0, 16, linkStatus);
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Draw IP address (centered)
    std::string ipStr = iface.ipAddress;
    int ipPos = std::max(0, (16 - static_cast<int>(ipStr.length())) / 2);
    m_display->drawText(ipPos, 24, ipStr);
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Draw MAC address (centered)
    std::string macStr = iface.macAddress;
    int macPos = std::max(0, (16 - static_cast<int>(macStr.length())) / 2);
    m_display->drawText(macPos, 32, macStr);
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Draw netmask (centered)
    std::string netmaskStr = iface.netmask;
    int netmaskPos = std::max(0, (16 - static_cast<int>(netmaskStr.length())) / 2);
    m_display->drawText(netmaskPos, 40, netmaskStr);
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Draw back instruction
    m_display->drawText(0, 48, "Press to return");
    usleep(Config::DISPLAY_CMD_DELAY);
}
