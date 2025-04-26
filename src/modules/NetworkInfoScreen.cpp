#include "ScreenModules.h"
#include "MenuSystem.h"
#include "DeviceInterfaces.h"
#include "Config.h"
#include <iostream>
#include <unistd.h>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <sys/ioctl.h>

NetworkInfoScreen::NetworkInfoScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input)
    : ScreenModule(display, input)
{
}

void NetworkInfoScreen::enter()
{
    // Get network information
    std::string ipStr = "Unknown";
    std::string macStr = "Unknown";
    std::string ifaceStr = "Unknown";
    
    getNetworkInfo(ipStr, macStr, ifaceStr);
    
    // Clear display and show network info
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 3);
    
    // Draw the title
    m_display->drawText(0, 0, "Network Setting");
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Draw a separator
    m_display->drawText(0, 8, "----------------");
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Format IP with interface name
    std::string tempStr = "IP:" + ifaceStr;
    m_display->drawText(0, 16, tempStr);
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Split IP into chunks that fit on the display
    size_t remaining = ipStr.length();
    size_t pos = 0;
    int yPos = 24;
    
    while (remaining > 0) {
        size_t chunk = std::min(remaining, static_cast<size_t>(15));
        std::string tempChunk = ipStr.substr(pos, chunk);
        
        m_display->drawText(0, yPos, tempChunk);
        usleep(Config::DISPLAY_CMD_DELAY);
        
        pos += chunk;
        remaining -= chunk;
        yPos += 10;
    }
    
    yPos += 8;
    
    // Format MAC address
    m_display->drawText(0, yPos, "MAC:");
    usleep(Config::DISPLAY_CMD_DELAY);
    yPos += 8;
    
    // Display MAC address
    m_display->drawText(0, yPos, macStr);
    usleep(Config::DISPLAY_CMD_DELAY);
}

void NetworkInfoScreen::update()
{
    // Nothing to update periodically
}

void NetworkInfoScreen::exit()
{
    // Nothing to clean up
}

bool NetworkInfoScreen::handleInput()
{
    // Check for button press to exit
    if (m_input->waitForEvents(100) > 0) {
        bool buttonPressed = false;
        
        m_input->processEvents(
            [](int) {
                // Ignore rotation in this screen
            },
            [&]() {
                // Button press exits
                buttonPressed = true;
            }
        );
        
        if (buttonPressed) {
            m_display->updateActivityTimestamp();
            return false; // Exit
        }
    }
    
    return true; // Continue
}

// Implementation of network info gathering
void NetworkInfoScreen::getNetworkInfo(std::string& ipStr, std::string& macStr, std::string& ifaceStr)
{
    struct ifaddrs* ifaddr, *ifa;
    int family, s;
    char host[NI_MAXHOST];
    bool foundIp = false;

    // Initialize with defaults
    ipStr = "Not connected";
    macStr = "Not available";
    ifaceStr = "Not available";

    // Get IP address
    if (getifaddrs(&ifaddr) == -1) {
        std::cerr << "getifaddrs failed: " << strerror(errno) << std::endl;
        return;
    }

    // First pass: Look for non-loopback interfaces
    for (ifa = ifaddr; ifa != NULL && !foundIp; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        family = ifa->ifa_addr->sa_family;

        // Skip if not IPv4
        if (family != AF_INET)
            continue;

        // Skip loopback interfaces (lo)
        if (strcmp(ifa->ifa_name, "lo") == 0)
            continue;

        // Check if interface is up and running
        if (!(ifa->ifa_flags & IFF_UP) || !(ifa->ifa_flags & IFF_RUNNING))
            continue;

        s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                      host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

        if (s != 0) {
            std::cerr << "getnameinfo failed: " << gai_strerror(s) << std::endl;
            continue;
        }

        ipStr = host;
        ifaceStr = ifa->ifa_name;
        foundIp = true;

        // Try to get MAC address for the same interface
        struct ifreq ifr;
        int fd = socket(AF_INET, SOCK_DGRAM, 0);

        if (fd >= 0) {
            strcpy(ifr.ifr_name, ifa->ifa_name);
            if (ioctl(fd, SIOCGIFHWADDR, &ifr) == 0) {
                unsigned char* mac = (unsigned char*)ifr.ifr_hwaddr.sa_data;
                char macBuf[18];
                snprintf(macBuf, sizeof(macBuf), "%02X:%02X:%02X:%02X:%02X:%02X",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                macStr = macBuf;
            }
            close(fd);
        }
    }

    // If we didn't find any suitable interface, use loopback as fallback
    if (!foundIp) {
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL)
                continue;

            family = ifa->ifa_addr->sa_family;

            if (family != AF_INET)
                continue;

            if (strcmp(ifa->ifa_name, "lo") == 0) {
                s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                              host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

                if (s != 0) {
                    std::cerr << "getnameinfo failed: " << gai_strerror(s) << std::endl;
                    continue;
                }

                ipStr = host;
                ifaceStr = "lo";

                // Try to get MAC for loopback too, though it likely won't have one
                struct ifreq ifr;
                int fd = socket(AF_INET, SOCK_DGRAM, 0);

                if (fd >= 0) {
                    strcpy(ifr.ifr_name, ifa->ifa_name);
                    if (ioctl(fd, SIOCGIFHWADDR, &ifr) == 0) {
                        unsigned char* mac = (unsigned char*)ifr.ifr_hwaddr.sa_data;
                        char macBuf[18];
                        snprintf(macBuf, sizeof(macBuf), "%02X:%02X:%02X:%02X:%02X:%02X",
                                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                        macStr = macBuf;
                    }
                    close(fd);
                }

                break;
            }
        }
    }

    freeifaddrs(ifaddr);
}
