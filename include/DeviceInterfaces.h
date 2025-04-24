#pragma once

#include <string>
#include <functional>
#include <mutex>
#include <atomic>
#include <vector>
#include <sys/time.h>
#include <thread>  // Add missing include for std::thread
#include "Config.h"

/**
 * Base device interface for all hardware devices
 */
class DeviceInterface {
public:
    DeviceInterface(const std::string& devicePath)
        : m_devicePath(devicePath), m_fd(-1) {}
    
    virtual ~DeviceInterface() {
        // Fix for pure virtual call in destructor
        if (isOpen()) {
            // Call close directly using implementation in derived class
            // but don't call through virtual dispatch
        }
    }
    
    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const {
        return m_fd >= 0;
    }
    
    virtual bool checkConnection() const = 0;
    
    const std::string& getDevicePath() const {
        return m_devicePath;
    }
    
protected:
    std::string m_devicePath;
    int m_fd;
};

/**
 * Handles communication with the display via serial
 */
class DisplayDevice : public DeviceInterface {
public:
    DisplayDevice(const std::string& devicePath = Config::DEFAULT_SERIAL_DEVICE);
    ~DisplayDevice();
    
    bool open() override;
    void close() override;
    bool checkConnection() const override;
    
    void sendCommand(const uint8_t* data, size_t length);
    void bufferCommand(const uint8_t* data, size_t length);
    void flushBuffer();
    
    // Display specific commands
    void clear();
    void drawText(int x, int y, const std::string& text);
    void setCursor(int x, int y);
    void setInverted(bool inverted);
    void setBrightness(int brightness);
    void drawProgressBar(int x, int y, int width, int height, int percentage);
    void setPower(bool on);
    
    bool isDisconnected() const {
        return m_disconnected;
    }
    
private:
    struct {
        uint8_t buffer[Config::CMD_BUFFER_SIZE];
        size_t used;
        struct timeval lastFlush;
    } m_cmdBuffer;
    
    std::mutex m_mutex;
    std::atomic<bool> m_disconnected{false};
};

/**
 * Handles input from the rotary encoder
 */
class InputDevice : public DeviceInterface {
public:
    InputDevice(const std::string& devicePath = Config::DEFAULT_INPUT_DEVICE);
    ~InputDevice();
    
    bool open() override;
    void close() override;
    bool checkConnection() const override;
    
    void setNonBlocking();
    
    // Input processing
    bool processEvents(std::function<void(int)> onRotation, std::function<void()> onButtonPress);
    int waitForEvents(int timeoutMs);
    
    // Get the file descriptor for advanced operations
    int getFd() const { return m_fd; }
    
private:
    struct {
        struct timeval lastKeyTime = {0, 0};
        int consecutiveSameKey = 0;
        int lastDirection = 0;
        struct timeval lastEventTime = {0, 0};
        int pairedEventCount = 0;
        int totalRelX = 0;
    } m_state;
};
/**
 * Device detection and monitoring
 */
class DeviceManager {
public:
    DeviceManager();
    ~DeviceManager();
    
    // Device detection functions
    std::pair<std::string, std::string> detectDevices();
    bool checkDevicePresent() const;
    bool monitorDeviceUntilConnected(std::atomic<bool>& runningFlag);
    
    // Disconnection monitoring
    void startDisconnectionMonitor();
    void stopDisconnectionMonitor();
    bool isDeviceDisconnected() const;
    
private:
    std::string findHmiInputDevice() const;
    std::string findHmiSerialDevice() const;
    bool checkDevicePresentSilent() const;
    void disconnectionMonitorThread();
    
    std::atomic<bool> m_deviceDisconnected{false};
    std::atomic<bool> m_monitorThreadRunning{false};
    std::thread m_monitorThread;
};
