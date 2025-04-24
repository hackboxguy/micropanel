#include "DeviceInterfaces.h"
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <iostream>
#include <sys/ioctl.h>

// Constructor
DisplayDevice::DisplayDevice(const std::string& devicePath)
    : DeviceInterface(devicePath)
{
    m_cmdBuffer.used = 0;
    gettimeofday(&m_cmdBuffer.lastFlush, nullptr);
}

// Destructor
DisplayDevice::~DisplayDevice()
{
    close();
}

// Open the serial device
bool DisplayDevice::open()
{
    // Return true if already open
    if (isOpen()) {
        return true;
    }
    
    m_fd = ::open(m_devicePath.c_str(), O_RDWR | O_NOCTTY);
    if (m_fd < 0) {
        std::cerr << "Failed to open serial device: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Configure serial port
    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    
    if (tcgetattr(m_fd, &tty) != 0) {
        std::cerr << "Failed to get serial attributes: " << strerror(errno) << std::endl;
        ::close(m_fd);
        m_fd = -1;
        return false;
    }
    
    // Set baud rate to 115200
    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);
    
    // 8N1 mode, no flow control
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;
    
    // Raw input
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    
    // Raw output
    tty.c_oflag &= ~OPOST;
    
    // Set attributes
    if (tcsetattr(m_fd, TCSANOW, &tty) != 0) {
        std::cerr << "Failed to set serial attributes: " << strerror(errno) << std::endl;
        ::close(m_fd);
        m_fd = -1;
        return false;
    }
    
    // Flush any pending data
    tcflush(m_fd, TCIOFLUSH);
    
    // Reset disconnection status
    m_disconnected = false;
    
    return true;
}

// Close the serial device
void DisplayDevice::close()
{
    if (isOpen()) {
        // Send any remaining buffered commands
        flushBuffer();
        
        // Close the file descriptor
        ::close(m_fd);
        m_fd = -1;
    }
}

// Check if the device is still connected
bool DisplayDevice::checkConnection() const
{
    if (!isOpen()) {
        return false;
    }
    
    // Check if the file descriptor is valid
    struct termios term;
    if (tcgetattr(m_fd, &term) < 0) {
        if (errno == EIO || errno == ENODEV || errno == ENXIO) {
            // These errors indicate device disconnection
            return false;
        }
    }
    
    return true;
}

// Buffer a command to be sent later
void DisplayDevice::bufferCommand(const uint8_t* data, size_t length)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // If buffer would overflow, flush it first
    if (m_cmdBuffer.used + length > Config::CMD_BUFFER_SIZE) {
        flushBuffer();
    }
    
    // Copy new command to buffer
    memcpy(m_cmdBuffer.buffer + m_cmdBuffer.used, data, length);
    m_cmdBuffer.used += length;
    
    // Update last action timestamp
    gettimeofday(&m_cmdBuffer.lastFlush, nullptr);
}

// Flush the command buffer to the serial device
void DisplayDevice::flushBuffer()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_cmdBuffer.used > 0 && isOpen()) {
        ssize_t bytesWritten = write(m_fd, m_cmdBuffer.buffer, m_cmdBuffer.used);
        if (bytesWritten < 0) {
            std::cerr << "Error writing to serial device: " << strerror(errno) << std::endl;
            
            // If error indicates device disconnection, set the flag
            if (errno == EIO || errno == ENODEV || errno == ENXIO) {
                std::cerr << "Serial buffer write error indicates device disconnection" << std::endl;
                m_disconnected = true;
            }
        } else if ((size_t)bytesWritten < m_cmdBuffer.used) {
            std::cerr << "Warning: Only wrote " << bytesWritten << " of " 
                      << m_cmdBuffer.used << " bytes" << std::endl;
        }
        
        // Flush the output only if device still connected
        if (!m_disconnected) {
            if (tcdrain(m_fd) < 0) {
                std::cerr << "Error draining serial output: " << strerror(errno) << std::endl;
                
                // Check if tcdrain error indicates device disconnection
                if (errno == EIO || errno == ENODEV || errno == ENXIO) {
                    std::cerr << "Serial buffer drain error indicates device disconnection" << std::endl;
                    m_disconnected = true;
                }
            }
        }
        
        m_cmdBuffer.used = 0;
    }
}

// Send a command immediately to the serial device
void DisplayDevice::sendCommand(const uint8_t* data, size_t length)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (isOpen()) {
        // Write the data and check return value
        ssize_t bytesWritten = write(m_fd, data, length);
        if (bytesWritten < 0) {
            std::cerr << "Error writing to serial device: " << strerror(errno) << std::endl;
            
            // If error indicates device disconnection, set the flag
            if (errno == EIO || errno == ENODEV || errno == ENXIO) {
                std::cerr << "Serial write error indicates device disconnection" << std::endl;
                m_disconnected = true;
            }
        } else if ((size_t)bytesWritten < length) {
            std::cerr << "Warning: Only wrote " << bytesWritten << " of " 
                      << length << " bytes" << std::endl;
        }
        
        // Flush the output buffer to ensure command is sent immediately
        // But only if the device hasn't been disconnected
        if (!m_disconnected) {
            if (tcdrain(m_fd) < 0) {
                std::cerr << "Error draining serial output: " << strerror(errno) << std::endl;
                
                // Check if tcdrain error indicates device disconnection
                if (errno == EIO || errno == ENODEV || errno == ENXIO) {
                    std::cerr << "Serial drain error indicates device disconnection" << std::endl;
                    m_disconnected = true;
                }
            }
        }
    }
}

// Clear the display
void DisplayDevice::clear()
{
    uint8_t cmd = Config::CMD_CLEAR;
    sendCommand(&cmd, 1);
}

// Draw text at position
void DisplayDevice::drawText(int x, int y, const std::string& text)
{
    size_t textLen = text.length();
    std::vector<uint8_t> cmd(textLen + 3);
    
    cmd[0] = Config::CMD_DRAW_TEXT;
    cmd[1] = static_cast<uint8_t>(x);
    cmd[2] = static_cast<uint8_t>(y);
    memcpy(cmd.data() + 3, text.c_str(), textLen);
    
    sendCommand(cmd.data(), cmd.size());
}

// Set cursor position
void DisplayDevice::setCursor(int x, int y)
{
    uint8_t cmd[3];
    cmd[0] = Config::CMD_SET_CURSOR;
    cmd[1] = static_cast<uint8_t>(x);
    cmd[2] = static_cast<uint8_t>(y);
    sendCommand(cmd, 3);
}

// Invert display colors
void DisplayDevice::setInverted(bool inverted)
{
    uint8_t cmd[2];
    cmd[0] = Config::CMD_INVERT;
    cmd[1] = inverted ? 1 : 0;
    sendCommand(cmd, 2);
}

// Send brightness command
void DisplayDevice::setBrightness(int brightness)
{
    uint8_t cmd[2];
    cmd[0] = Config::CMD_BRIGHTNESS;
    cmd[1] = static_cast<uint8_t>(brightness);
    sendCommand(cmd, 2);
}

// Send progress bar command
void DisplayDevice::drawProgressBar(int x, int y, int width, int height, int percentage)
{
    uint8_t cmd[6];
    cmd[0] = Config::CMD_PROGRESS_BAR;
    cmd[1] = static_cast<uint8_t>(x);
    cmd[2] = static_cast<uint8_t>(y);
    cmd[3] = static_cast<uint8_t>(width);
    cmd[4] = static_cast<uint8_t>(height);
    cmd[5] = static_cast<uint8_t>(percentage);
    sendCommand(cmd, 6);
}

// Set power mode
void DisplayDevice::setPower(bool on)
{
    uint8_t cmd[2];
    cmd[0] = Config::CMD_POWER_MODE;
    cmd[1] = on ? 0x01 : 0x00;
    sendCommand(cmd, 2);
}
