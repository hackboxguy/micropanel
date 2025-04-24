#include "ScreenModules.h"
#include "MenuSystem.h"
#include "DeviceInterfaces.h"
#include "Config.h"
#include <iostream>
#include <unistd.h>
#include <linux/input.h>

void ScreenModule::run()
{
    // Set running flag
    m_running = true;
    
    // Enter the module (initialize display)
    enter();
    
    // Make sure we have the user's full attention
    // by clearing any pending input events before we start
    struct input_event ev;
    while (read(m_input->getFd(), &ev, sizeof(ev)) > 0) {
        // Just drain any pending events
    }
    
    while (m_running) {
        // Check for device disconnection
        if (m_display->isDisconnected()) {
            std::cout << "Device disconnected during module execution" << std::endl;
            break;
        }
        
        // Check for power save mode activation
        if (m_display->isPowerSaveEnabled()) {
            m_display->checkPowerSaveTimeout();
            if (!m_display->isPoweredOn() || m_display->isPowerSaveActivated()) {
                std::cout << "Power save detected - exiting module" << std::endl;
                break;
            }
        }
        
        // Handle input (returns false if exit is requested)
        if (!handleInput()) {
            break;
        }
        
        // Update module display if needed
        update();
        
        // Small delay to reduce CPU usage
        usleep(Config::MAIN_LOOP_DELAY);
    }
    
    // Exit the module (cleanup)
    exit();
    
    // Reset running flag
    m_running = false;
}
// Note: The Hello and Counter screen implementations have been removed from this file
// as they are now implemented in HelloCounterScreens.cpp

bool ScreenModule::handleInput()
{
    // This is a default implementation that subclasses should override
    // Check if input device is still valid
    if (!m_input || !m_input->isOpen()) {
        std::cerr << "Input device is invalid or closed in ScreenModule::handleInput" << std::endl;
        return false; // Exit the module
    }
    
    // Basic input handling - exit on button press
    if (m_input->waitForEvents(100) > 0) {
        bool buttonPressed = false;
        
        m_input->processEvents(
            [](int direction) {
                // Ignore rotation events by default
            },
            [&]() {
                // Button press exits by default
                buttonPressed = true;
            }
        );
        
        if (buttonPressed) {
            return false; // Exit the module
        }
    }
    
    return true; // Continue running
}
//
