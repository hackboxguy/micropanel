#include "DeviceInterfaces.h"
#include "Config.h"
#include "Logger.h"
#include <iostream>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <i2c/smbus.h>

I2CDisplayDevice::I2CDisplayDevice(const std::string& devicePath)
    : BaseDisplayDevice(devicePath) {
    std::memset(m_displayBuffer, 0, sizeof(m_displayBuffer));
    Logger::debug("I2CDisplayDevice created for: " + devicePath);
}

I2CDisplayDevice::~I2CDisplayDevice() {
    close();
}

bool I2CDisplayDevice::open() {
    Logger::debug("Opening I2C device: " + m_devicePath);
    
    // Open I2C device
    m_fd = ::open(m_devicePath.c_str(), O_RDWR);
    if (m_fd < 0) {
        std::cerr << "Failed to open I2C device " << m_devicePath << ": " << strerror(errno) << std::endl;
        return false;
    }

    // Set I2C slave address
    if (ioctl(m_fd, I2C_SLAVE, SSD1306_ADDR) < 0) {
        std::cerr << "Failed to set I2C slave address: " << strerror(errno) << std::endl;
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    Logger::debug("I2C device opened successfully, initializing display...");

    // Initialize the SSD1306 display
    if (!initializeDisplay()) {
        std::cerr << "Failed to initialize SSD1306 display" << std::endl;
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    std::cout << "SSD1306 display initialized successfully" << std::endl;
    return true;
}

void I2CDisplayDevice::close() {
    if (isOpen()) {
        // Turn off display before closing
        writeCommand(SSD1306_DISPLAY_OFF);
        
        ::close(m_fd);
        m_fd = -1;
        std::cout << "I2C device closed" << std::endl;
    }
}

bool I2CDisplayDevice::checkConnection() const {
    //if (!isOpen()) {
    //    return false;
    //}
    // Try to write a harmless command to test connection
    //uint8_t testData[2] = {0x00, SSD1306_DISPLAY_RAM}; // Display RAM command
    //ssize_t result = write(m_fd, testData, 2);
    //return result == 2;
    return isOpen();
}

bool I2CDisplayDevice::writeCommand(uint8_t command) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!isOpen()) {
        return false;
    }

    uint8_t data[2] = {0x00, command}; // Control byte 0x00 = command
    ssize_t result = write(m_fd, data, 2);
    
    if (result != 2) {
        std::cerr << "I2C command write failed: " << strerror(errno) << std::endl;
        m_disconnected.store(true);
        return false;
    }

    return true;
}

bool I2CDisplayDevice::writeData(const uint8_t* data, size_t length) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!isOpen() || !data) {
        return false;
    }

    // Create buffer with control byte for data
    std::vector<uint8_t> buffer(length + 1);
    buffer[0] = 0x40; // Control byte 0x40 = data
    std::memcpy(buffer.data() + 1, data, length);

    ssize_t result = write(m_fd, buffer.data(), buffer.size());
    
    if (result != static_cast<ssize_t>(buffer.size())) {
        std::cerr << "I2C data write failed: " << strerror(errno) << std::endl;
        m_disconnected.store(true);
        return false;
    }

    return true;
}

bool I2CDisplayDevice::initializeDisplay() {
    std::cout << "Initializing SSD1306..." << std::endl;

    // Give display time to power up
    usleep(100000); // 100ms

    // SSD1306 initialization sequence (based on your RP2040 code)
    if (!writeCommand(SSD1306_DISPLAY_OFF)) return false;
    if (!writeCommand(SSD1306_SET_DISPLAY_CLOCK_DIV)) return false;
    if (!writeCommand(0x80)) return false; // Suggested ratio
    if (!writeCommand(SSD1306_SET_MULTIPLEX)) return false;
    if (!writeCommand(DISPLAY_HEIGHT - 1)) return false;
    if (!writeCommand(SSD1306_SET_DISPLAY_OFFSET)) return false;
    if (!writeCommand(0x00)) return false;
    if (!writeCommand(SSD1306_SET_START_LINE | 0x00)) return false;
    if (!writeCommand(SSD1306_CHARGE_PUMP)) return false;
    if (!writeCommand(0x14)) return false; // Enable charge pump
    if (!writeCommand(SSD1306_MEMORY_MODE)) return false;
    if (!writeCommand(0x00)) return false; // Horizontal addressing mode

    // Set orientation for normal display (top-left origin)
    if (!writeCommand(SSD1306_SEG_REMAP_REVERSE)) return false; // 0xA1
    if (!writeCommand(SSD1306_COM_SCAN_DEC)) return false;      // 0xC8

    if (!writeCommand(SSD1306_SET_COM_PINS)) return false;
    if (!writeCommand(0x12)) return false;
    if (!writeCommand(SSD1306_SET_CONTRAST)) return false;
    if (!writeCommand(0xCF)) return false;
    if (!writeCommand(SSD1306_SET_PRECHARGE)) return false;
    if (!writeCommand(0xF1)) return false;
    if (!writeCommand(SSD1306_SET_VCOM_DETECT)) return false;
    if (!writeCommand(0x40)) return false;
    if (!writeCommand(SSD1306_DISPLAY_RAM)) return false;
    if (!writeCommand(SSD1306_DISPLAY_NORMAL)) return false;
    if (!writeCommand(SSD1306_DISPLAY_ON)) return false;

    // Clear the display
    clear();

    std::cout << "SSD1306 initialization complete" << std::endl;
    return true;
}

void I2CDisplayDevice::clear() {
    Logger::debug("I2CDisplayDevice::clear()");
    
    // Clear framebuffer
    std::memset(m_displayBuffer, 0, sizeof(m_displayBuffer));

    // Set address range for whole display
    if (!writeCommand(SSD1306_PAGE_ADDR)) return;
    if (!writeCommand(0)) return;
    if (!writeCommand(DISPLAY_PAGES - 1)) return;
    if (!writeCommand(SSD1306_COLUMN_ADDR)) return;
    if (!writeCommand(0)) return;
    if (!writeCommand(DISPLAY_WIDTH - 1)) return;

    // Send cleared buffer to display
    writeData(m_displayBuffer, sizeof(m_displayBuffer));

    // Reset cursor position
    m_cursorX = 0;
    m_cursorY = 0;
}

void I2CDisplayDevice::drawText(int x, int y, const std::string& text) {
    Logger::debug("I2CDisplayDevice::drawText(" + std::to_string(x) + "," + std::to_string(y) + ",\"" + text + "\")");
    
    setCursor(x, y);
    
    for (char c : text) {
        drawCharacter(c);
    }
}

void I2CDisplayDevice::setCursor(int x, int y) {
    // Clamp to valid display range
    if (x < 0) x = 0;
    if (x >= DISPLAY_WIDTH) x = DISPLAY_WIDTH - 1;
    if (y < 0) y = 0;
    if (y >= DISPLAY_HEIGHT) y = DISPLAY_HEIGHT - 1;
    m_cursorX = static_cast<uint8_t>(x);
    m_cursorY = static_cast<uint8_t>(y);
}

void I2CDisplayDevice::setInverted(bool inverted) {
    Logger::debug("I2CDisplayDevice::setInverted(" + std::string(inverted ? "true" : "false") + ")");
    
    m_inverted = inverted;
    
    if (inverted) {
        writeCommand(SSD1306_DISPLAY_INVERTED);
    } else {
        writeCommand(SSD1306_DISPLAY_NORMAL);
    }
}

void I2CDisplayDevice::setBrightness(int brightness) {
    Logger::debug("I2CDisplayDevice::setBrightness(" + std::to_string(brightness) + ")");
    
    // Clamp brightness to valid range
    uint8_t contrast = static_cast<uint8_t>(brightness > 255 ? 255 : (brightness < 0 ? 0 : brightness));
    
    if (!writeCommand(SSD1306_SET_CONTRAST)) return;
    writeCommand(contrast);
}

void I2CDisplayDevice::drawProgressBar(int x, int y, int width, int height, int percentage) {
    Logger::debug("I2CDisplayDevice::drawProgressBar(" + std::to_string(x) + "," + std::to_string(y) + "," +
                  std::to_string(width) + "," + std::to_string(height) + "," + std::to_string(percentage) + "%)");
    
    // Ensure progress is within range
    if (percentage > 100) percentage = 100;
    if (percentage < 0) percentage = 0;

    // Calculate progress width in pixels
    int progressWidth = (width * percentage) / 100;

    // Calculate which pages the bar spans
    int startPage = y / 8;
    int endPage = (y + height - 1) / 8;

    // Draw the progress bar
    for (int page = startPage; page <= endPage && page < DISPLAY_PAGES; page++) {
        for (int col = x; col < x + width && col < DISPLAY_WIDTH; col++) {
            uint8_t mask = 0;

            // Calculate which bits in the byte should be set
            for (int bit = 0; bit < 8; bit++) {
                int pixelY = page * 8 + bit;
                if (pixelY >= y && pixelY < y + height) {
                    // This bit is part of the progress bar
                    if (col == x || col == x + width - 1 ||
                        pixelY == y || pixelY == y + height - 1) {
                        // This pixel is part of the border
                        mask |= (1 << bit);
                    }
                    else if (col < x + progressWidth) {
                        // This pixel is part of the filled area
                        mask |= (1 << bit);
                    }
                }
            }

            // Update display buffer
            int pos = page * DISPLAY_WIDTH + col;
            if (pos < static_cast<int>(sizeof(m_displayBuffer))) {
                m_displayBuffer[pos] = mask;
            }
        }
    }

    // Update the affected display region
    updateDisplay(startPage, endPage, x, x + width - 1);
}

void I2CDisplayDevice::setPower(bool on) {
    Logger::debug("I2CDisplayDevice::setPower(" + std::string(on ? "true" : "false") + ")");
    
    if (on) {
        writeCommand(SSD1306_DISPLAY_ON);
    } else {
        writeCommand(SSD1306_DISPLAY_OFF);
    }
}

void I2CDisplayDevice::drawCharacter(char c) {
    if (c < 0 || c > 127) c = '?'; // Handle non-ASCII chars

    // Calculate buffer position
    int page = m_cursorY / 8;  // Page = y / 8
    int col = m_cursorX;       // Column = x

    // Check bounds
    if (page >= DISPLAY_PAGES || col > DISPLAY_WIDTH - 8) {
        return;
    }

    // Get character data and transpose it (convert rows to columns)
    // This matches your RP2040 implementation
    uint8_t transposed[8] = {0};

    // Transpose the character (swap rows and columns)
    for (int srcRow = 0; srcRow < 8; srcRow++) {
        uint8_t srcByte = Font8x8::font8x8_basic[static_cast<uint8_t>(c)][srcRow];

        for (int srcCol = 0; srcCol < 8; srcCol++) {
            if (srcByte & (1 << srcCol)) {
                // Set the corresponding bit in the transposed character
                transposed[srcCol] |= (1 << srcRow);
            }
        }
    }

    // Copy transposed character to buffer
    for (int i = 0; i < 8; i++) {
        if (col + i < DISPLAY_WIDTH) {
            int pos = page * DISPLAY_WIDTH + (col + i);
            if (pos < static_cast<int>(sizeof(m_displayBuffer))) {
                m_displayBuffer[pos] = transposed[i];
            }
        }
    }

    // Update the display for this character
    updateDisplay(page, page, col, col + 7);

    // Advance cursor - move 8 pixels to the right
    m_cursorX += 8;

    // Wrap to next line if needed
    if (m_cursorX > DISPLAY_WIDTH - 8) {
        m_cursorX = 0;
        m_cursorY += 8; // Move down one character row (8 pixels)
        if (m_cursorY >= DISPLAY_HEIGHT) {
            m_cursorY = 0; // Wrap to top if we reach the bottom
        }
    }
}

void I2CDisplayDevice::updateDisplay(int startPage, int endPage, int startCol, int endCol) {
    // Clamp values to valid ranges
    if (startPage < 0) startPage = 0;
    if (endPage >= DISPLAY_PAGES) endPage = DISPLAY_PAGES - 1;
    if (startCol < 0) startCol = 0;
    if (endCol >= DISPLAY_WIDTH) endCol = DISPLAY_WIDTH - 1;

    // Update each page in the specified region
    for (int page = startPage; page <= endPage; page++) {
        // Set page and column address range
        if (!writeCommand(SSD1306_PAGE_ADDR)) return;
        if (!writeCommand(static_cast<uint8_t>(page))) return;
        if (!writeCommand(static_cast<uint8_t>(page))) return;
        if (!writeCommand(SSD1306_COLUMN_ADDR)) return;
        if (!writeCommand(static_cast<uint8_t>(startCol))) return;
        if (!writeCommand(static_cast<uint8_t>(endCol))) return;

        // Send the data for this region
        int dataStart = page * DISPLAY_WIDTH + startCol;
        int dataLength = endCol - startCol + 1;
        
        if (dataStart >= 0 && dataStart + dataLength <= static_cast<int>(sizeof(m_displayBuffer))) {
            writeData(&m_displayBuffer[dataStart], dataLength);
        }
    }
}
