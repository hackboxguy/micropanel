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
 * Base interface for display devices (serial and I2C)
 */
class BaseDisplayDevice : public DeviceInterface {
public:
    BaseDisplayDevice(const std::string& devicePath) : DeviceInterface(devicePath) {}
    virtual ~BaseDisplayDevice() = default;

    // Pure virtual display commands that both serial and I2C must implement
    virtual void clear() = 0;
    virtual void drawText(int x, int y, const std::string& text) = 0;
    virtual void setCursor(int x, int y) = 0;
    virtual void setInverted(bool inverted) = 0;
    virtual void setBrightness(int brightness) = 0;
    virtual void drawProgressBar(int x, int y, int width, int height, int percentage) = 0;
    virtual void setPower(bool on) = 0;

    // Optional buffering interface (used by serial, may be used by I2C)
    virtual void sendCommand(const uint8_t* data, size_t length) {(void)data; (void)length;}
    virtual void bufferCommand(const uint8_t* data, size_t length) {(void)data; (void)length;}
    virtual void flushBuffer() {}

    virtual bool isDisconnected() const { return false; }
};

/**
 * Handles communication with the display via serial (EXISTING - now inherits from BaseDisplayDevice)
 */
class DisplayDevice : public BaseDisplayDevice {
public:
    DisplayDevice(const std::string& devicePath = Config::DEFAULT_SERIAL_DEVICE);
    ~DisplayDevice();

    bool open() override;
    void close() override;
    bool checkConnection() const override;

    void sendCommand(const uint8_t* data, size_t length) override;
    void bufferCommand(const uint8_t* data, size_t length) override;
    void flushBuffer() override;

    // Display specific commands
    void clear() override;
    void drawText(int x, int y, const std::string& text) override;
    void setCursor(int x, int y) override;
    void setInverted(bool inverted) override;
    void setBrightness(int brightness) override;
    void drawProgressBar(int x, int y, int width, int height, int percentage) override;
    void setPower(bool on) override;

    bool isDisconnected() const override {
        return m_disconnected;
    }

private:
    void flushBufferInternal();  // Internal flush without locking (caller must hold m_mutex)

    struct {
        uint8_t buffer[Config::CMD_BUFFER_SIZE];
        size_t used;
        struct timeval lastFlush;
    } m_cmdBuffer;

    std::mutex m_mutex;
    std::atomic<bool> m_disconnected{false};
};

/**
 * Font data for I2C display rendering
 */
class Font8x8 {
public:
    static const uint8_t font8x8_basic[128][8];
};

/**
 * I2C-based SSD1306 display device  
 */
class I2CDisplayDevice : public BaseDisplayDevice {
public:
    I2CDisplayDevice(const std::string& devicePath = "/dev/i2c-1");
    ~I2CDisplayDevice();

    bool open() override;
    void close() override;
    bool checkConnection() const override;

    // Display commands
    void clear() override;
    void drawText(int x, int y, const std::string& text) override;
    void setCursor(int x, int y) override;
    void setInverted(bool inverted) override;
    void setBrightness(int brightness) override;
    void drawProgressBar(int x, int y, int width, int height, int percentage) override;
    void setPower(bool on) override;

    bool isDisconnected() const override {
        return m_disconnected.load();
    }

private:
    static constexpr uint8_t SSD1306_ADDR = 0x3C;
    static constexpr int DISPLAY_WIDTH = 128;
    static constexpr int DISPLAY_HEIGHT = 64;
    static constexpr int DISPLAY_PAGES = 8;

    // SSD1306 Commands
    static constexpr uint8_t SSD1306_SET_CONTRAST = 0x81;
    static constexpr uint8_t SSD1306_DISPLAY_RAM = 0xA4;
    static constexpr uint8_t SSD1306_DISPLAY_NORMAL = 0xA6;
    static constexpr uint8_t SSD1306_DISPLAY_INVERTED = 0xA7;
    static constexpr uint8_t SSD1306_DISPLAY_OFF = 0xAE;
    static constexpr uint8_t SSD1306_DISPLAY_ON = 0xAF;
    static constexpr uint8_t SSD1306_SET_DISPLAY_OFFSET = 0xD3;
    static constexpr uint8_t SSD1306_SET_COM_PINS = 0xDA;
    static constexpr uint8_t SSD1306_SET_VCOM_DETECT = 0xDB;
    static constexpr uint8_t SSD1306_SET_DISPLAY_CLOCK_DIV = 0xD5;
    static constexpr uint8_t SSD1306_SET_PRECHARGE = 0xD9;
    static constexpr uint8_t SSD1306_SET_MULTIPLEX = 0xA8;
    static constexpr uint8_t SSD1306_SET_START_LINE = 0x40;
    static constexpr uint8_t SSD1306_MEMORY_MODE = 0x20;
    static constexpr uint8_t SSD1306_COLUMN_ADDR = 0x21;
    static constexpr uint8_t SSD1306_PAGE_ADDR = 0x22;
    static constexpr uint8_t SSD1306_COM_SCAN_DEC = 0xC8;
    static constexpr uint8_t SSD1306_SEG_REMAP_REVERSE = 0xA1;
    static constexpr uint8_t SSD1306_CHARGE_PUMP = 0x8D;

    // I2C communication
    bool writeCommand(uint8_t command);
    bool writeData(const uint8_t* data, size_t length);
    bool initializeDisplay();

    // Display buffer and cursor management
    uint8_t m_displayBuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT / 8];
    uint8_t m_cursorX = 0;
    uint8_t m_cursorY = 0;
    bool m_inverted = false;
    std::atomic<bool> m_disconnected{false};
    std::mutex m_mutex;

    // Font rendering
    void drawCharacter(char c);
    void updateDisplay(int startPage, int endPage, int startCol, int endCol);
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
    // EXISTING: Rotary encoder state tracking
    struct {
        struct timeval lastKeyTime = {0, 0};
        int consecutiveSameKey = 0;
        int lastDirection = 0;
        struct timeval lastEventTime = {0, 0};
        int pairedEventCount = 0;
        int totalRelX = 0;
        int totalRelY = 0;  // Added to track vertical movement
    } m_state;

    // NEW: Keyboard synthesis state tracking
    struct {
        bool leftPending = false;
        bool rightPending = false;
        bool upPending = false;
        bool downPending = false;
        struct timeval leftFirstTime = {0, 0};
        struct timeval rightFirstTime = {0, 0};
        struct timeval upFirstTime = {0, 0};
        struct timeval downFirstTime = {0, 0};
    } m_keyboardState;

    // NEW: Process pending keyboard synthesis events
    void processKeyboardSynthesis(std::function<void(int)> onRotation);
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
    std::pair<std::string, std::string> detectDevicesWithFallback(const std::string& fallbackInput, const std::string& fallbackSerial);
    bool checkDevicePresent(bool silent = false) const;
    bool monitorDeviceUntilConnected(std::atomic<bool>& runningFlag);

    // Disconnection monitoring
    void startDisconnectionMonitor();
    void stopDisconnectionMonitor();
    bool isDeviceDisconnected() const;

private:
    std::string findHmiInputDevice() const;
    std::string findHmiSerialDevice() const;
    void disconnectionMonitorThread();

    std::atomic<bool> m_deviceDisconnected{false};
    std::atomic<bool> m_monitorThreadRunning{false};
    std::thread m_monitorThread;
};
