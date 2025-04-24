#include "ScreenModules.h"
#include "MenuSystem.h"
#include "DeviceInterfaces.h"
#include "Config.h"
#include <iostream>
#include <unistd.h>

BrightnessScreen::BrightnessScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input)
    : ScreenModule(display, input)
{
}

void BrightnessScreen::enter()
{
    // Remember the current brightness to restore if needed
    m_previousBrightness = m_display->getBrightness();
    
    // Initialize the screen
    setupScreen();
    
    // Update initial brightness display
    updateBrightnessValue(m_previousBrightness);
}

void BrightnessScreen::update()
{
    // Nothing to do in regular updates
}

void BrightnessScreen::exit()
{
    // Nothing to clean up
}

bool BrightnessScreen::handleInput()
{
    if (m_input->waitForEvents(100) > 0) {
        bool buttonPressed = false;
        
        m_input->processEvents(
            [this](int direction) {
                // Rotation adjusts brightness
                int currentBrightness = m_display->getBrightness();
                
                if (direction < 0) {
                    // Decrease brightness (rotate left)
                    currentBrightness -= 10;
                    if (currentBrightness < 0) currentBrightness = 0;
                } else {
                    // Increase brightness (rotate right)
                    currentBrightness += 10;
                    if (currentBrightness > 255) currentBrightness = 255;
                }
                
                updateBrightnessValue(currentBrightness);
                m_display->updateActivityTimestamp();
            },
            [&]() {
                // Button press exits
                buttonPressed = true;
                m_display->updateActivityTimestamp();
            }
        );
        
        if (buttonPressed) {
            return false; // Exit the screen
        }
    }
    
    return true; // Continue running
}

void BrightnessScreen::setupScreen()
{
    // Clear display
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 3);
    
    // Draw title
    m_display->drawText(25, 5, "Brightness");
    usleep(Config::DISPLAY_CMD_DELAY);
    m_display->drawText(0, 8, "----------------");
    usleep(Config::DISPLAY_CMD_DELAY);
}

void BrightnessScreen::updateBrightnessValue(int brightness)
{
    // Calculate percentage (0-255 -> 0-100%)
    int percentage = (brightness * 100) / 255;
    
    // Format the percentage text
    char text[16];
    snprintf(text, sizeof(text), "%d%%", percentage);
    
    // Clear the previous text area first
    m_display->drawText(50, 20, "    ");
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Draw new value
    m_display->drawText(50, 20, text);
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Update progress bar
    m_display->drawProgressBar(10, 30, 108, 15, percentage);
    
    // Set the actual display brightness
    m_display->setBrightness(brightness);
    
    //std::cout << "Brightness set to " << brightness << " (" << percentage << "%)" << std::endl;
}
