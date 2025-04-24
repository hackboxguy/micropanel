#pragma once

#include <cstdint>

/**
 * Configuration constants for MicroPanel
 */
namespace Config {
    // Display properties
    constexpr int DISPLAY_WIDTH = 128;
    constexpr int DISPLAY_HEIGHT = 64;
    constexpr int CHAR_WIDTH = 6;
    constexpr int CHAR_HEIGHT = 8;

    // Menu configuration
    constexpr int MENU_START_Y = 16;
    constexpr int MENU_ITEM_SPACING = 8;
    constexpr int MENU_MAX_ITEMS = 10;
    constexpr const char* MENU_TITLE = "MAIN MENU";
    constexpr const char* MENU_SEPARATOR = "----------------";
    constexpr int MENU_VISIBLE_ITEMS = 6;
    constexpr int MENU_SCROLL_INDICATOR_WIDTH = 3;
    constexpr int STAT_UPDATE_SEC = 1;

    // Device detection
    constexpr const char* DEFAULT_INPUT_DEVICE = "/dev/input/event0";
    constexpr const char* DEFAULT_SERIAL_DEVICE = "/dev/ttyACM0";
    constexpr const char* HMI_VENDOR_ID = "1209";
    constexpr const char* HMI_PRODUCT_ID = "0001";
    constexpr const char* HMI_PRODUCT_NAME = "Pico Encoder Display";
    constexpr const char* HMI_MANUFACTURER = "DIY Projects";
    constexpr int DETECTION_POLL_INTERVAL = 2000;  // Poll every 2 seconds
    constexpr int UDEV_POLL_TIMEOUT = 100;         // 100ms timeout for udev events
    
    // Protocol commands
    constexpr uint8_t CMD_CLEAR = 0x01;
    constexpr uint8_t CMD_DRAW_TEXT = 0x02;
    constexpr uint8_t CMD_SET_CURSOR = 0x03;
    constexpr uint8_t CMD_INVERT = 0x04;
    constexpr uint8_t CMD_BRIGHTNESS = 0x05;
    constexpr uint8_t CMD_PROGRESS_BAR = 0x06;
    constexpr uint8_t CMD_POWER_MODE = 0x07;

    // Timing constants
    constexpr int DISPLAY_CMD_DELAY = 10000;       // 10ms delay between display commands
    constexpr int DISPLAY_CLEAR_DELAY = 50000;     // 50ms delay after clear
    constexpr int DISPLAY_UPDATE_DEBOUNCE = 100;   // 100ms between display updates
    constexpr int KEY_ACCELERATION_THRESHOLD = 200; // 200ms for acceleration
    constexpr int EVENT_PROCESS_THRESHOLD = 30;    // 30ms for event processing
    constexpr int INPUT_SELECT_TIMEOUT = 20000;    // 20ms timeout for select
    constexpr int MAIN_LOOP_DELAY = 5000;          // 5ms delay in main loop
    constexpr int STARTUP_DELAY = 1000000;         // 1s delay at startup
    constexpr int CMD_BUFFER_FLUSH_INTERVAL = 50;  // 50ms between buffer flushes

    // Power save constants
    constexpr int POWER_SAVE_TIMEOUT_SEC = 10;     // Default timeout in seconds for power save

    // Serial command buffer
    constexpr int CMD_BUFFER_SIZE = 256;

    // Input event handling limits
    constexpr int MAX_EVENTS_PER_ITERATION = 5;
    constexpr int MAX_ACCELERATION_STEPS = 3;

    // Version
    constexpr const char* VERSION = "2.0.0";
}
