#include "ScreenModules.h"
#include "MenuSystem.h"
#include "DeviceInterfaces.h"
#include "Config.h"
#include <iostream>
#include <unistd.h>
#include <string>

// Initialize static counter
int CounterScreen::s_counter = 0;

// Implementation for HelloWorldScreen

HelloWorldScreen::HelloWorldScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input)
    : ScreenModule(display, input)
{
}

void HelloWorldScreen::enter()
{
    // Clear the display
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 3);
    
    // Draw the hello world message
    m_display->drawText(0, 0, "Hello, World!");
    m_display->drawText(0, 10, "Pressed Enter!");
    
    // Record start time for auto-exit
    m_startTime = time(nullptr);
}

void HelloWorldScreen::update()
{
    // Auto-exit after display time
    if (time(nullptr) - m_startTime >= m_displayTime) {
        m_running = false;
    }
}

void HelloWorldScreen::exit()
{
    // Nothing to clean up
}

bool HelloWorldScreen::handleInput()
{
    // Check for any input activity
    if (m_input->waitForEvents(100) > 0) {
        bool buttonPressed = false;
        
        m_input->processEvents(
            [](int direction) {
                // Just consume rotation events without doing anything
                // This prevents rotation events from "leaking" back to the main menu
                std::cout << "HelloWorld: Ignoring rotation event" << std::endl;
            },
            [&]() {
                // Button press exits
                buttonPressed = true;
                std::cout << "HelloWorld: Button pressed, exiting" << std::endl;
            }
        );
        
        if (buttonPressed) {
            m_display->updateActivityTimestamp();
            return false; // Exit
        }
    }
    
    return true; // Continue
}

// Implementation for CounterScreen

CounterScreen::CounterScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input)
    : ScreenModule(display, input)
{
}

void CounterScreen::enter()
{
    // Increment counter
    s_counter++;
    
    // Clear the display
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 3);
    
    // Draw the counter
    m_display->drawText(0, 0, "Counter:");
    
    // Format counter value
    std::string counterStr = std::to_string(s_counter);
    m_display->drawText(0, 10, counterStr);
    
    // Record start time for auto-exit
    m_startTime = time(nullptr);
}

void CounterScreen::update()
{
    // Auto-exit after display time
    if (time(nullptr) - m_startTime >= m_displayTime) {
        m_running = false;
    }
}

void CounterScreen::exit()
{
    // Nothing to clean up
}
bool CounterScreen::handleInput()
{
    // Check for any input activity
    if (m_input->waitForEvents(100) > 0) {
        bool buttonPressed = false;
        
        m_input->processEvents(
            [](int direction) {
                // Just consume rotation events without doing anything
                // This prevents rotation events from "leaking" back to the main menu
                std::cout << "Counter: Ignoring rotation event" << std::endl;
            },
            [&]() {
                // Button press exits
                buttonPressed = true;
                std::cout << "Counter: Button pressed, exiting" << std::endl;
            }
        );
        
        if (buttonPressed) {
            m_display->updateActivityTimestamp();
            return false; // Exit
        }
    }
    
    return true; // Continue
}
