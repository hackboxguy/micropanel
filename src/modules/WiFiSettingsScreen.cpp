#include "ScreenModules.h"
#include "MenuSystem.h"
#include "DeviceInterfaces.h"
#include "Config.h"
#include <iostream>
#include <unistd.h>

WiFiSettingsScreen::WiFiSettingsScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input)
    : ScreenModule(display, input),
      m_selectedOption(0)
{
}

void WiFiSettingsScreen::enter()
{
    // Get current WiFi state
    m_currentWiFiState = getWiFiStatus();
    
    // Clear display and show submenu
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 3);
    
    // Draw title
    m_display->drawText(0, 0, " WiFi Settings");
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Draw separator
    m_display->drawText(0, 8, "----------------");
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Draw menu options
    renderOptions();
}

void WiFiSettingsScreen::update()
{
    // Nothing to update periodically
}

void WiFiSettingsScreen::exit()
{
    // Nothing to clean up
}

bool WiFiSettingsScreen::handleInput()
{
    if (m_input->waitForEvents(100) > 0) {
        bool buttonPressed = false;
        
        m_input->processEvents(
            [this](int direction) {
                // Handle rotation - move selection up or down
                int oldSelection = m_selectedOption;
                
                if (direction < 0) {
                    // Move up
                    if (m_selectedOption > 0) {
                        m_selectedOption--;
                    }
                } else {
                    // Move down
                    if (m_selectedOption < static_cast<int>(m_options.size() - 1)) {
                        m_selectedOption++;
                    }
                }
                
                // Only redraw if selection changed
                if (oldSelection != m_selectedOption) {
                    renderOptions();
                }
                
                m_display->updateActivityTimestamp();
            },
            [&]() {
                // Handle button press
                buttonPressed = true;
                m_display->updateActivityTimestamp();
            }
        );
        
        if (buttonPressed) {
            // Handle button press based on selected option
            if (m_selectedOption == 2) {
                return false; // Exit the screen
            } else {
                // Try to change WiFi state
                bool newState = (m_selectedOption == 0); // Option 0 = "Turn On", Option 1 = "Turn Off"
                
                // Only act if state would change
                if (newState != m_currentWiFiState) {
                    // Update the state
                    setWiFiStatus(newState);
                    m_currentWiFiState = newState;
                    
                    // Redraw options to update state indicators
                    renderOptions();
                }
            }
        }
    }
    
    return true; // Continue
}

void WiFiSettingsScreen::renderOptions()
{
    // Draw the options with selection indicator
    for (size_t i = 0; i < m_options.size(); i++) {
        // Determine if this option represents current state
        bool isCurrentState = (i == 0 && m_currentWiFiState) ||
                             (i == 1 && !m_currentWiFiState);
        
        std::string buffer;
        int yPos = 16 + (i * 10);  // Start at y=16 with 10px spacing
        
        // Clear the line first to avoid display artifacts
        m_display->drawText(0, yPos, "                ");
        usleep(Config::DISPLAY_CMD_DELAY);
        
        // Format with selection indicator and/or state highlight
        if (static_cast<int>(i) == m_selectedOption) {
            if (isCurrentState) {
                buffer = ">[" + m_options[i] + "]";
            } else {
                buffer = ">" + m_options[i];
            }
        } else {
            if (isCurrentState) {
                buffer = " [" + m_options[i] + "]";
            } else {
                buffer = " " + m_options[i];
            }
        }
        
        m_display->drawText(0, yPos, buffer);
        usleep(Config::DISPLAY_CMD_DELAY);
    }
}

bool WiFiSettingsScreen::getWiFiStatus() const
{
    // This is a mock function that should be implemented to check the actual WiFi state
    // For this example we just return a stored value
    static bool wifiState = false;
    return wifiState;
}

void WiFiSettingsScreen::setWiFiStatus(bool enabled)
{
    // This is a mock function that should be implemented to control the actual WiFi
    static bool wifiState = false;
    
    if (enabled != wifiState) {
        wifiState = enabled;
        std::cout << "WiFi state changed to " << (enabled ? "ON" : "OFF") << std::endl;
        
        // In a real implementation, you would control the WiFi hardware here:
        // Example:
        // if (enabled) {
        //     system("systemctl start wpa_supplicant");
        // } else {
        //     system("systemctl stop wpa_supplicant");
        // }
    }
}
