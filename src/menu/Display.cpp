#include "MenuSystem.h"
#include "DeviceInterfaces.h"
#include "Config.h"
#include <unistd.h>
#include <iostream>

Display::Display(std::shared_ptr<DisplayDevice> device)
    : m_device(device)
{
    // Initialize the activity timestamp
    gettimeofday(&m_lastActivityTime, nullptr);
}

void Display::clear()
{
    if (m_device) {
        m_device->clear();
    }
}

void Display::drawText(int x, int y, const std::string& text)
{
    if (m_device) {
        m_device->drawText(x, y, text);
    }
}

void Display::setCursor(int x, int y)
{
    if (m_device) {
        m_device->setCursor(x, y);
    }
}

void Display::setInverted(bool inverted)
{
    if (m_device) {
        m_device->setInverted(inverted);
        m_inverted = inverted;
    }
}

void Display::setBrightness(int brightness)
{
    if (m_device) {
        m_device->setBrightness(brightness);
        m_brightness = brightness;
    }
}

void Display::drawProgressBar(int x, int y, int width, int height, int percentage)
{
    if (m_device) {
        m_device->drawProgressBar(x, y, width, height, percentage);
    }
}

void Display::setPower(bool on)
{
    // Only send command if the state is changing
    if (on == m_poweredOn) {
        return;
    }

    if (m_device) {
        m_device->setPower(on);
    }

    m_poweredOn = on;

    // Signal power save activation when turning off
    if (!on) {
        m_powerSaveActivated = true;
        std::cout << "Power save activated - signaling all menus to exit" << std::endl;
    } else {
        m_powerSaveActivated = false;
    }

    std::cout << "Display power set to: " << (on ? "ON" : "OFF") << std::endl;
}

void Display::enablePowerSave(bool enable)
{
    m_powerSaveEnabled = enable;
    
    // If enabling power save, initialize the activity timestamp
    if (enable) {
        gettimeofday(&m_lastActivityTime, nullptr);
        
        // Make sure display is on initially
        if (!m_poweredOn) {
            setPower(true);
        }
    }
}

void Display::updateActivityTimestamp()
{
    gettimeofday(&m_lastActivityTime, nullptr);

    // If the display was off, turn it back on
    if (m_powerSaveEnabled && !m_poweredOn) {
        // Wake up display
        setPower(true);
    }
}

void Display::checkPowerSaveTimeout()
{
    if (!m_powerSaveEnabled || !m_poweredOn) {
        return;
    }

    struct timeval now;
    gettimeofday(&now, nullptr);

    // Calculate time diff in seconds
    long timeDiffSec = (now.tv_sec - m_lastActivityTime.tv_sec);

    if (timeDiffSec >= Config::POWER_SAVE_TIMEOUT_SEC) {
        std::cout << "Power save timeout reached (" << timeDiffSec 
                  << " seconds of inactivity)" << std::endl;

        // Turn off the display
        setPower(false);
        m_powerSaveActivated = true;
    }
}

bool Display::isDisconnected() const
{
    return m_device ? m_device->isDisconnected() : true;
}
