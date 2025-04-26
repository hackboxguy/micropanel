#include "ScreenModules.h"
#include "MenuSystem.h"
#include "DeviceInterfaces.h"
#include "Config.h"
#include <iostream>
#include <unistd.h>
#include <cstdlib>
#include <ctime>

InternetTestScreen::InternetTestScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input)
    : ScreenModule(display, input),
      m_testCompleted(false),
      m_testResult(-1),
      m_progress(0)
{
}

void InternetTestScreen::enter()
{
    // Clear display and show initial screen
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 3);
    
    // Draw title
    m_display->drawText(0, 0, " Internet Test");
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Draw separator
    m_display->drawText(0, 8, "----------------");
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Draw testing message
    m_display->drawText(20, 20, "Testing");
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Initial progress bar (0%)
    m_display->drawProgressBar(10, 35, 108, 15, 0);
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Reset state
    m_testCompleted = false;
    m_testResult = -1;
    m_progress = 0;
    
    // Record start time
    m_startTime = time(nullptr);
    
    // Start the test in a separate thread
    startTest();
}

void InternetTestScreen::startTest()
{
    // Start a new thread for the test
    m_testThread = std::thread([this]() {
        // Perform the ping test
        int result = pingServer(m_testServer, m_timeoutSec);
        
        // Update state when complete
        m_testResult = result;
        m_testCompleted = true;
        m_progress = 100;
    });
    
    // Detach the thread to allow it to run independently
    m_testThread.detach();
}

void InternetTestScreen::update()
{
    if (m_testCompleted) {
        // Test has finished, update UI once
        if (m_progress < 100) {
            // Update progress bar to 100%
            m_display->drawProgressBar(10, 35, 108, 15, 100);
            m_progress = 100;
            
            // Show result message
            m_display->drawText(0, 60, "                ");  // Clear line
            if (m_testResult == 0) {
                m_display->drawText(30, 60, "Connected!");
            } else {
                m_display->drawText(20, 60, "No Connection");
            }
        }
    } else {
        // Test still running, update progress bar
        time_t currentTime = time(nullptr);
        time_t elapsed = currentTime - m_startTime;
        
        // Calculate progress percentage (based on timeout)
        int progress = (elapsed * 100) / m_timeoutSec;
        if (progress > 95) progress = 95;  // Cap at 95% until complete
        
        // Only update if progress has changed
        if (progress > m_progress) {
            m_progress = progress;
            
            // Update the progress bar
            m_display->drawProgressBar(10, 35, 108, 15, progress);
            
            // Add animated dots to the "Testing" message
            int dots = (elapsed % 4);
            std::string message = "Testing";
            for (int i = 0; i < dots; i++) {
                message += ".";
            }
            
            m_display->drawText(0, 20, "                ");  // Clear line
            m_display->drawText(20, 20, message);
        }
    }
}

void InternetTestScreen::exit()
{
    // No cleanup needed as thread is detached
}

bool InternetTestScreen::handleInput()
{
    if (m_input->waitForEvents(100) > 0) {
        bool buttonPressed = false;
        
        m_input->processEvents(
            [](int) {
                // Ignore rotation in this screen
            },
            [&]() {
                // Button press exits
                buttonPressed = true;
            }
        );
        
        if (buttonPressed) {
            m_display->updateActivityTimestamp();
            return false; // Exit
        }
    }
    
    return true; // Continue
}

int InternetTestScreen::pingServer(const std::string& server, int timeoutSec)
{
    // Construct ping command with timeout and minimal output
    std::string command = "ping -c 1 -W " + std::to_string(timeoutSec) + " " + server + " > /dev/null 2>&1";
    
    // Execute the command
    int result = system(command.c_str());
    
    // Return 0 for success, non-zero for failure
    return (result == 0) ? 0 : 1;
}
