#include "DeviceInterfaces.h"
#include "Config.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <libudev.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <dirent.h>  // For DIR and readdir
#include "Logger.h"

DeviceManager::DeviceManager()
    : m_deviceDisconnected(false),
      m_monitorThreadRunning(false)
{
}

DeviceManager::~DeviceManager()
{
    stopDisconnectionMonitor();
}

std::pair<std::string, std::string> DeviceManager::detectDevices()
{
    std::string inputDevice = findHmiInputDevice();
    std::string serialDevice = findHmiSerialDevice();
    
    return std::make_pair(inputDevice, serialDevice);
}

bool DeviceManager::checkDevicePresent() const
{
    struct udev* udev;
    struct udev_enumerate* enumerate;
    struct udev_list_entry* devices, *devListEntry;
    bool found = false;
    
    // Create udev context
    udev = udev_new();
    if (!udev) {
        Logger::error("Failed to create udev context");
        return false;
    }
    
    Logger::debug("Looking for HMI device with VID:PID " + std::string(Config::HMI_VENDOR_ID) + ":" + 
                std::string(Config::HMI_PRODUCT_ID));
    
    // List all input devices for debugging
    if (Logger::isVerbose()) {
        Logger::debug("Available input devices:");
        DIR* dir = opendir("/dev/input");
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != NULL) {
                if (strncmp(entry->d_name, "event", 5) == 0) {
                    std::string path = "/dev/input/";
                    path += entry->d_name;
                    
                    // Try to get device name
                    int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
                    if (fd >= 0) {
                        char name[256] = "Unknown";
                        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0) {
                            Logger::debug("  " + path + ": " + name);
                        } else {
                            Logger::debug("  " + path + ": <unknown>");
                        }
                        close(fd);
                    } else {
                        Logger::debug("  " + path + ": <cannot open>");
                    }
                }
            }
            closedir(dir);
        }
    }
    
    // Create enumerate object
    enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enumerate, "usb");
    udev_enumerate_scan_devices(enumerate);
    
    // Get the list of matching devices
    devices = udev_enumerate_get_list_entry(enumerate);
    
    // Iterate through devices
    udev_list_entry_foreach(devListEntry, devices) {
        const char* path = udev_list_entry_get_name(devListEntry);
        struct udev_device* dev = udev_device_new_from_syspath(udev, path);
        
        if (dev) {
            const char* vendor = udev_device_get_sysattr_value(dev, "idVendor");
            const char* product = udev_device_get_sysattr_value(dev, "idProduct");
            const char* manufacturer = udev_device_get_sysattr_value(dev, "manufacturer");
            const char* productName = udev_device_get_sysattr_value(dev, "product");
            
            // Print USB device info for debugging
            if (Logger::isVerbose() && vendor && product) {
                std::string deviceInfo = "USB Device: " + std::string(vendor ? vendor : "?") + ":" + 
                                      std::string(product ? product : "?");
                if (manufacturer || productName) {
                    deviceInfo += " - " + std::string(manufacturer ? manufacturer : "") + " " + 
                               std::string(productName ? productName : "");
                }
                Logger::debug(deviceInfo);
            }
            
            // Check if this is our HMI device
            if (vendor && product &&
                strcmp(vendor, Config::HMI_VENDOR_ID) == 0 &&
                strcmp(product, Config::HMI_PRODUCT_ID) == 0) {
                
                if (manufacturer && productName &&
                    strstr(manufacturer, Config::HMI_MANUFACTURER) != NULL &&
                    strstr(productName, Config::HMI_PRODUCT_NAME) != NULL) {
                    
                    Logger::info("Found device: " + std::string(manufacturer) + " " + 
                              std::string(productName) + " (VID:PID " + vendor + ":" + product + ")");
                    found = true;
                }
            }
            
            udev_device_unref(dev);
            
            if (found) break;
        }
    }
    
    // Clean up
    udev_enumerate_unref(enumerate);
    udev_unref(udev);
    
    return found;
}

bool DeviceManager::checkDevicePresentSilent() const
{
    struct udev* udev;
    struct udev_enumerate* enumerate;
    struct udev_list_entry* devices, *devListEntry;
    bool found = false;
    
    // Create udev context
    udev = udev_new();
    if (!udev) {
        return false;
    }
    
    // Create enumerate object
    enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enumerate, "usb");
    udev_enumerate_scan_devices(enumerate);
    
    // Get the list of matching devices
    devices = udev_enumerate_get_list_entry(enumerate);
    
    // Iterate through devices
    udev_list_entry_foreach(devListEntry, devices) {
        const char* path = udev_list_entry_get_name(devListEntry);
        struct udev_device* dev = udev_device_new_from_syspath(udev, path);
        
        if (dev) {
            const char* vendor = udev_device_get_sysattr_value(dev, "idVendor");
            const char* product = udev_device_get_sysattr_value(dev, "idProduct");
            
            // Check if this is our HMI device
            if (vendor && product &&
                strcmp(vendor, Config::HMI_VENDOR_ID) == 0 &&
                strcmp(product, Config::HMI_PRODUCT_ID) == 0) {
                found = true;
            }
            
            udev_device_unref(dev);
            
            if (found) break;
        }
    }
    
    // Clean up
    udev_enumerate_unref(enumerate);
    udev_unref(udev);
    
    return found;
}

bool DeviceManager::monitorDeviceUntilConnected(std::atomic<bool>& runningFlag)
{
    struct udev* udev;
    struct udev_monitor* mon;
    int fd;

    // Create udev context
    udev = udev_new();
    if (!udev) {
        Logger::error("Failed to create udev context");
        return false;
    }

    // Set up monitoring
    mon = udev_monitor_new_from_netlink(udev, "udev");
    udev_monitor_filter_add_match_subsystem_devtype(mon, "usb", NULL);
    udev_monitor_enable_receiving(mon);
    fd = udev_monitor_get_fd(mon);

    // Set up polling
    struct pollfd fds[1];
    fds[0].fd = fd;
    fds[0].events = POLLIN;

    Logger::info("Waiting for HMI device to be connected...");

    int reconnectAttempts = 0;
    const int maxReconnectAttempts = 30; // Limit the number of reconnect attempts

    //while (reconnectAttempts < maxReconnectAttempts && runningFlag) {
    while (runningFlag) {
        // Check if user cancelled with Ctrl+C
        if (!runningFlag) {
            Logger::info("Detection cancelled by user");
            return false;
        }

        // First check if device is already present
        bool deviceFound = checkDevicePresent();

        if (deviceFound) {
            Logger::info("HMI device found!");
            // Clean up
            udev_monitor_unref(mon);
            udev_unref(udev);
            return true;
        }

        // Wait for device events
        int ret = poll(fds, 1, Config::UDEV_POLL_TIMEOUT);

        if (ret > 0 && (fds[0].revents & POLLIN)) {
            // Get the device
            struct udev_device* dev = udev_monitor_receive_device(mon);
            if (dev) {
                const char* action = udev_device_get_action(dev);

                // Only care about add events
                if (action && strcmp(action, "add") == 0) {
                    // Check if this is potentially our device
                    struct udev_device* usbDev = udev_device_get_parent_with_subsystem_devtype(
                        dev, "usb", "usb_device");

                    if (usbDev) {
                        const char* vendor = udev_device_get_sysattr_value(usbDev, "idVendor");
                        const char* product = udev_device_get_sysattr_value(usbDev, "idProduct");

                        if (vendor && product &&
                            strcmp(vendor, Config::HMI_VENDOR_ID) == 0 &&
                            strcmp(product, Config::HMI_PRODUCT_ID) == 0) {

                            Logger::debug("USB device connected (VID:PID " + std::string(vendor) + ":" +
                                      std::string(product) + ")");

                            // Give some time for all device nodes to be created
                            sleep(2);

                            // Double check that all required interfaces are present
                            if (checkDevicePresent()) {
                                Logger::info("HMI device fully initialized!");
                                udev_device_unref(dev);

                                // Clean up
                                udev_monitor_unref(mon);
                                udev_unref(udev);

                                return true;
                            }
                        }
                    }
                }
                udev_device_unref(dev);
            }
        }

        // If we've waited for a while with no device events, do a periodic check
        // This handles the case where we might have missed an event
        static time_t lastPeriodicCheck = 0;
        time_t now = time(NULL);

        if (now - lastPeriodicCheck >= 5) {  // Check every 5 seconds
            lastPeriodicCheck = now;
            reconnectAttempts++;

            // Print a progress message
            Logger::debug("Waiting for device... Attempt " +
                       std::to_string(reconnectAttempts) + " of " +
                       std::to_string(maxReconnectAttempts));

            // Manual check for device
            if (checkDevicePresent()) {
                Logger::info("HMI device found on periodic check!");
                // Clean up
                udev_monitor_unref(mon);
                udev_unref(udev);
                return true;
            }
        }

        // Brief delay before checking again if no events
        if (ret == 0) {
            usleep(1000000); // 1 second wait between poll attempts
        }
    }

    Logger::warning("Gave up waiting for device after " +
                 std::to_string(maxReconnectAttempts) + " attempts");

    // Clean up
    udev_monitor_unref(mon);
    udev_unref(udev);

    return false;
}

void DeviceManager::startDisconnectionMonitor()
{
    // Only start if not already running
    if (!m_monitorThreadRunning) {
        m_deviceDisconnected = false;
        m_monitorThreadRunning = true;
        m_monitorThread = std::thread(&DeviceManager::disconnectionMonitorThread, this);
    }
}

void DeviceManager::stopDisconnectionMonitor()
{
    if (m_monitorThreadRunning) {
        m_monitorThreadRunning = false;
        if (m_monitorThread.joinable()) {
            m_monitorThread.join();
        }
    }
}

bool DeviceManager::isDeviceDisconnected() const
{
    return m_deviceDisconnected;
}
void DeviceManager::disconnectionMonitorThread()
{
    struct udev* udev;
    struct udev_monitor* mon;
    int fd;
    
    // Create udev context
    udev = udev_new();
    if (!udev) {
        std::cerr << "Failed to create udev context in monitor thread" << std::endl;
        return;
    }
    
    // Set up monitoring
    mon = udev_monitor_new_from_netlink(udev, "udev");
    udev_monitor_filter_add_match_subsystem_devtype(mon, "usb", NULL);
    udev_monitor_enable_receiving(mon);
    fd = udev_monitor_get_fd(mon);
    
    // Set up polling
    struct pollfd fds[1];
    fds[0].fd = fd;
    fds[0].events = POLLIN;
    
    std::cout << "Disconnection monitor thread started" << std::endl;
    
    // Track time for periodic checks
    time_t lastCheckTime = time(NULL);
    
    while (m_monitorThreadRunning) {
        // Only check device connection periodically (every 5 seconds)
        time_t now = time(NULL);
        if (now - lastCheckTime >= 5) {
            // Check that the device is still present
            if (!checkDevicePresentSilent()) {
                std::cout << "Device disconnected (periodic check)" << std::endl;
                m_deviceDisconnected = true;
                break;
            }
            lastCheckTime = now;
        }
        
        // Poll for device events (focusing on removal events)
        int ret = poll(fds, 1, 1000); // 1 second timeout
        
        if (ret > 0 && (fds[0].revents & POLLIN)) {
            // Get the device
            struct udev_device* dev = udev_monitor_receive_device(mon);
            if (dev) {
                const char* action = udev_device_get_action(dev);
                
                // Only process remove events to avoid repeated detection
                if (action && strcmp(action, "remove") == 0) {
                    // Check if this is potentially our device
                    struct udev_device* usbDev = udev_device_get_parent_with_subsystem_devtype(
                        dev, "usb", "usb_device");
                        
                    if (usbDev) {
                        const char* vendor = udev_device_get_sysattr_value(usbDev, "idVendor");
                        const char* product = udev_device_get_sysattr_value(usbDev, "idProduct");
                        
                        if (vendor && product &&
                            strcmp(vendor, Config::HMI_VENDOR_ID) == 0 &&
                            strcmp(product, Config::HMI_PRODUCT_ID) == 0) {
                            
                            std::cout << "USB device disconnected (VID:PID " << vendor << ":" << product << ")" << std::endl;
                            m_deviceDisconnected = true;
                            udev_device_unref(dev);
                            break;
                        }
                    }
                }
                udev_device_unref(dev);
            }
        }
    }
    
    // Clean up
    udev_monitor_unref(mon);
    udev_unref(udev);
    
    std::cout << "Disconnection monitor thread exiting" << std::endl;
}

std::string DeviceManager::findHmiInputDevice() const
{
    struct udev* udev;
    struct udev_enumerate* enumerate;
    struct udev_list_entry* devices, *devListEntry;
    std::string result;
    
    // Create udev context
    udev = udev_new();
    if (!udev) {
        Logger::error("Failed to create udev context");
        return result;
    }
    
    Logger::debug("Searching for HMI input device (VID=" + std::string(Config::HMI_VENDOR_ID) + 
                " PID=" + std::string(Config::HMI_PRODUCT_ID) + 
                " Product=" + std::string(Config::HMI_PRODUCT_NAME) + ")");
    
    // First try to find by looking at all input devices
    enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enumerate, "input");
    udev_enumerate_scan_devices(enumerate);
    devices = udev_enumerate_get_list_entry(enumerate);
    
    // Check each input device
    udev_list_entry_foreach(devListEntry, devices) {
        const char* path = udev_list_entry_get_name(devListEntry);
        struct udev_device* dev = udev_device_new_from_syspath(udev, path);
        
        if (!dev) continue;
        
        const char* devnode = udev_device_get_devnode(dev);
        if (!devnode || strstr(devnode, "/dev/input/event") == NULL) {
            udev_device_unref(dev);
            continue;
        }
        
        // Check the device name
        const char* name = udev_device_get_property_value(dev, "NAME");
        if (name) {
            Logger::debug("Found input device: " + std::string(devnode) + " - Name: " + std::string(name));
            
            // Check if this device's name matches our product
            if (strstr(name, Config::HMI_PRODUCT_NAME) != NULL || 
                strstr(name, "Pico Encoder") != NULL) {
                Logger::debug("Found matching input device by name: " + std::string(devnode));
                result = devnode;
                udev_device_unref(dev);
                break;
            }
        }
        
        // Walk up the device tree to check parent USB device
        struct udev_device* parent = dev;
        struct udev_device* usbDev = NULL;
        
        // Keep going up the device tree looking for the USB device
        while (parent) {
            const char* subsys = udev_device_get_subsystem(parent);
            if (subsys && strcmp(subsys, "usb") == 0) {
                const char* devtype = udev_device_get_devtype(parent);
                if (devtype && strcmp(devtype, "usb_device") == 0) {
                    usbDev = parent;
                    break;
                }
            }
            parent = udev_device_get_parent(parent);
        }
        
        if (usbDev) {
            // Check vendor and product ID
            const char* vendor = udev_device_get_sysattr_value(usbDev, "idVendor");
            const char* product = udev_device_get_sysattr_value(usbDev, "idProduct");
            const char* manufacturer = udev_device_get_sysattr_value(usbDev, "manufacturer");
            const char* productName = udev_device_get_sysattr_value(usbDev, "product");
            
            Logger::debug("  USB device: " +
                       std::string(vendor ? vendor : "unknown") + ":" +
                       std::string(product ? product : "unknown") + " - " +
                       std::string(manufacturer ? manufacturer : "unknown") + " " +
                       std::string(productName ? productName : "unknown"));
            
            // Check if this matches our HMI device
            if (vendor && product &&
                strcmp(vendor, Config::HMI_VENDOR_ID) == 0 &&
                strcmp(product, Config::HMI_PRODUCT_ID) == 0) {
                
                Logger::debug("Found matching input device by VID:PID: " + std::string(devnode));
                result = devnode;
                udev_device_unref(dev);
                break;
            }
        }
    udev_device_unref(dev);
    }
    
    udev_enumerate_unref(enumerate);
    
    // If we didn't find a device, try the more direct approach
    if (result.empty()) {
        Logger::debug("Trying alternative detection method...");
        
        // Look for device by dmesg pattern (recent device appears in dmesg)
        FILE* fp = popen("dmesg | grep -A 2 \"input: DIY Projects Pico Encoder Display as\" | grep -o \"/dev/input/event[0-9]*\"", "r");
        if (fp) {
            char path[64];
            if (fgets(path, sizeof(path), fp) != NULL) {
                // Remove trailing newline
                size_t len = strlen(path);
                if (len > 0 && path[len-1] == '\n') {
                    path[len-1] = '\0';
                }
                
                Logger::debug("Found input device from dmesg: " + std::string(path));
                result = path;
            }
            pclose(fp);
        }
    }
    
    // If we still didn't find a device, check all input devices for Mouse capability
    if (result.empty()) {
        Logger::debug("Searching for any mouse-like input device...");
        
        enumerate = udev_enumerate_new(udev);
        udev_enumerate_add_match_subsystem(enumerate, "input");
        udev_enumerate_scan_devices(enumerate);
        devices = udev_enumerate_get_list_entry(enumerate);
        
        udev_list_entry_foreach(devListEntry, devices) {
            const char* path = udev_list_entry_get_name(devListEntry);
            struct udev_device* dev = udev_device_new_from_syspath(udev, path);
            
            if (!dev) continue;
            
            const char* devnode = udev_device_get_devnode(dev);
            if (!devnode || strstr(devnode, "/dev/input/event") == NULL) {
                udev_device_unref(dev);
                continue;
            }
            
            // Open device and check if it has REL_X capability
            int fd = open(devnode, O_RDONLY | O_NONBLOCK);
            if (fd >= 0) {
                unsigned long evbit[EV_MAX/8/sizeof(long) + 1] = {0};
                unsigned long relbit[REL_MAX/8/sizeof(long) + 1] = {0};
                
                if (ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), evbit) >= 0 &&
                    ioctl(fd, EVIOCGBIT(EV_REL, sizeof(relbit)), relbit) >= 0) {
                    
                    // Check if device has EV_REL and REL_X capability
                    if ((evbit[EV_REL/8/sizeof(long)] & (1 << (EV_REL % (8 * sizeof(long))))) &&
                        (relbit[REL_X/8/sizeof(long)] & (1 << (REL_X % (8 * sizeof(long)))))) {
                        
                        Logger::debug("Found input device with REL_X capability: " + std::string(devnode));
                        result = devnode;
                        close(fd);
                        udev_device_unref(dev);
                        break;
                    }
                }
                close(fd);
            }
            
            udev_device_unref(dev);
        }
        
        udev_enumerate_unref(enumerate);
    }
    
    udev_unref(udev);
    
    if (result.empty()) {
        Logger::error("Failed to find any suitable input device!");
    } else {
        Logger::info("Selected input device: " + result);
    }
    
    return result;
}

std::string DeviceManager::findHmiSerialDevice() const
{
    struct udev* udev;
    struct udev_enumerate* enumerate;
    struct udev_list_entry* devices, *devListEntry;
    std::string result;
    
    // Create udev context
    udev = udev_new();
    if (!udev) {
        std::cerr << "Failed to create udev context" << std::endl;
        return result;
    }
    
    std::cout << "Searching for HMI serial device (VID=" << Config::HMI_VENDOR_ID 
              << " PID=" << Config::HMI_PRODUCT_ID 
              << " Product=" << Config::HMI_PRODUCT_NAME << ")" << std::endl;
    
    // First try standard approach via tty subsystem
    enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enumerate, "tty");
    udev_enumerate_scan_devices(enumerate);
    devices = udev_enumerate_get_list_entry(enumerate);
    
    // Iterate through tty devices
    udev_list_entry_foreach(devListEntry, devices) {
        const char* path = udev_list_entry_get_name(devListEntry);
        struct udev_device* dev = udev_device_new_from_syspath(udev, path);
        
        if (!dev) continue;
        
        const char* devnode = udev_device_get_devnode(dev);
        if (!devnode || strstr(devnode, "/dev/ttyACM") == NULL) {
            udev_device_unref(dev);
            continue;
        }
        
        std::cout << "Found TTY device: " << devnode << std::endl;
        
        // Walk up the device tree to check parent USB device
        struct udev_device* parent = dev;
        struct udev_device* usbDev = NULL;
        
        // Keep going up the device tree looking for the USB device
        while (parent) {
            const char* subsys = udev_device_get_subsystem(parent);
            if (subsys && strcmp(subsys, "usb") == 0) {
                const char* devtype = udev_device_get_devtype(parent);
                if (devtype && strcmp(devtype, "usb_device") == 0) {
                    usbDev = parent;
                    break;
                }
            }
            parent = udev_device_get_parent(parent);
        }
        
        if (usbDev) {
            // Check vendor and product ID
            const char* vendor = udev_device_get_sysattr_value(usbDev, "idVendor");
            const char* product = udev_device_get_sysattr_value(usbDev, "idProduct");
            const char* manufacturer = udev_device_get_sysattr_value(usbDev, "manufacturer");
            const char* productName = udev_device_get_sysattr_value(usbDev, "product");
            
            std::cout << "  USB device: "
                      << (vendor ? vendor : "unknown") << ":"
                      << (product ? product : "unknown") << " - "
                      << (manufacturer ? manufacturer : "unknown") << " "
                      << (productName ? productName : "unknown") << std::endl;
            
            // Check if this matches our HMI device
            if (vendor && product &&
                strcmp(vendor, Config::HMI_VENDOR_ID) == 0 &&
                strcmp(product, Config::HMI_PRODUCT_ID) == 0) {
                
                std::cout << "Found matching serial device by VID:PID: " << devnode << std::endl;
                result = devnode;
                udev_device_unref(dev);
                break;
            }
        }
        
        udev_device_unref(dev);
    }
    
    udev_enumerate_unref(enumerate);
    
    // If we didn't find a device, try the more direct approach
    if (result.empty()) {
        std::cout << "Trying alternative detection method for serial device..." << std::endl;
        
        // Look for device by dmesg pattern
        FILE* fp = popen("dmesg | grep -A 1 \"Product: Pico Encoder Display\" | grep -o \"/dev/ttyACM[0-9]*\"", "r");
        if (fp) {
            char path[64];
            if (fgets(path, sizeof(path), fp) != NULL) {
                // Remove trailing newline
                size_t len = strlen(path);
                if (len > 0 && path[len-1] == '\n') {
                    path[len-1] = '\0';
                }
                
                std::cout << "Found serial device from dmesg: " << path << std::endl;
                result = path;
            }
            pclose(fp);
        }
    }
    
    // Final fallback - just look for any ttyACM device
    if (result.empty()) {
        std::cout << "Checking for any ttyACM device..." << std::endl;
        
        // Try to find any ttyACM device
        DIR* dir = opendir("/dev");
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != NULL) {
                if (strncmp(entry->d_name, "ttyACM", 6) == 0) {
                    std::string path = "/dev/";
                    path += entry->d_name;
                    std::cout << "Found ttyACM device: " << path << std::endl;
                    result = path;
                    break;
                }
            }
            closedir(dir);
        }
    }
    
    udev_unref(udev);
    
    if (result.empty()) {
        std::cerr << "Failed to find any suitable serial device!" << std::endl;
    } else {
        std::cout << "Selected serial device: " << result << std::endl;
    }
    
    return result;
}

