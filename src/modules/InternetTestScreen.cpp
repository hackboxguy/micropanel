#include "ScreenModules.h"
#include "MenuSystem.h"
#include "DeviceInterfaces.h"
#include "Config.h"
#include "Logger.h"
#include <iostream>
#include <unistd.h>
#include <cstdlib>
#include <ctime>
#include <chrono>

InternetTestScreen::InternetTestScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input)
    : ScreenModule(display, input),
      m_testCompleted(false),
      m_testResult(-1),
      m_progress(0)
{
}

void InternetTestScreen::enter()
{
    Logger::debug("InternetTestScreen: Entered");
    m_running = true;
    
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
    m_display->drawText(20, 20, "Testing...");
    usleep(Config::DISPLAY_CMD_DELAY);

    // Initial progress bar (0%)
    m_display->drawProgressBar(10, 35, 108, 15, 0);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Reset state
    m_testCompleted = false;
    m_testResult = -1;
    m_progress = 0;
    m_progressLastUpdated = 0;
    m_animationLastUpdated = 0;
    m_resultDisplayed = false;

    // Record start time
    m_startTime = std::chrono::steady_clock::now();

    // Start the test in a separate thread
    startTest();
    
    Logger::debug("InternetTestScreen: Test started");
}

void InternetTestScreen::startTest()
{
    // Start a new thread for the test
    m_testThread = std::thread([this]() {
        Logger::debug("InternetTestScreen: Test thread started for " + m_testServer);
        
        // Sleep for a short time to allow UI to initialize
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Perform the ping test
        int result = pingServer(m_testServer, m_timeoutSec);
        
        Logger::debug("InternetTestScreen: Test completed with result " + std::to_string(result));

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
    auto currentTime = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        currentTime - m_startTime).count();
    
    // Update animation every 500ms
    bool shouldUpdateAnimation = (elapsedMs - m_animationLastUpdated >= 500);
    if (shouldUpdateAnimation) {
        m_animationLastUpdated = elapsedMs;
        
        if (!m_testCompleted) {
            // Update animation dots
            int dots = ((elapsedMs / 500) % 4);
            std::string message = "Testing";
            for (int i = 0; i < dots; i++) {
                message += ".";
            }
            for (int i = dots; i < 3; i++) {
                message += " ";
            }
            
            m_display->drawText(0, 20, "                ");
            m_display->drawText(20, 20, message);
            usleep(Config::DISPLAY_CMD_DELAY);
            
            Logger::debug("InternetTestScreen: Animation update: " + message);
        }
    }
    
    // Update progress every 250ms if test is running
    bool shouldUpdateProgress = (elapsedMs - m_progressLastUpdated >= 250);
    if (shouldUpdateProgress && !m_testCompleted) {
        m_progressLastUpdated = elapsedMs;
        
        // Calculate progress as a percentage of the timeout
        int progress = static_cast<int>((elapsedMs * 100) / (m_timeoutSec * 1000));
        if (progress > 95) progress = 95;  // Cap at 95% until complete
        
        if (progress > m_progress) {
            m_progress = progress;
            m_display->drawProgressBar(10, 35, 108, 15, progress);
            usleep(Config::DISPLAY_CMD_DELAY);
            Logger::debug("InternetTestScreen: Progress update: " + std::to_string(progress) + "%");
        }
    }
    
    // Handle test completion
    if (m_testCompleted && !m_resultDisplayed) {
        // Update to 100%
        m_display->drawProgressBar(10, 35, 108, 15, 100);
        usleep(Config::DISPLAY_CMD_DELAY);
        
        // Clear testing message
        m_display->drawText(0, 20, "                ");
        usleep(Config::DISPLAY_CMD_DELAY);
        
        // Show result
        if (m_testResult == 0) {
            m_display->drawText(20, 20, "CONNECTED!");
            Logger::debug("InternetTestScreen: Showing CONNECTED message");
        } else {
            m_display->drawText(20, 20, "NO CONNECTION");
            Logger::debug("InternetTestScreen: Showing NO CONNECTION message");
        }
        usleep(Config::DISPLAY_CMD_DELAY);
        
        // Show press button message
        m_display->drawText(15, 60, "Press to exit");
        usleep(Config::DISPLAY_CMD_DELAY);
        
        m_resultDisplayed = true;
        Logger::debug("InternetTestScreen: Result displayed");
    }
}

void InternetTestScreen::exit()
{
    Logger::debug("InternetTestScreen: Exiting");
    
    // Clean up
    m_running = false;
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 3);
}

bool InternetTestScreen::handleInput()
{
    // Process any pending input
    if (m_input->waitForEvents(100) > 0) {
        bool buttonPressed = false;

        m_input->processEvents(
            [this](int) {
                // Ignore rotation in this screen
                m_display->updateActivityTimestamp();
            },
            [&buttonPressed, this]() {
                // Button press 
                buttonPressed = true;
                m_display->updateActivityTimestamp();
                Logger::debug("InternetTestScreen: Button pressed");
            }
        );

        if (buttonPressed) {
            if (m_testCompleted) {
                Logger::debug("InternetTestScreen: Test completed, exiting on button press");
                return false; // Exit if test is complete
            } else {
                // If test is still running, mark it as complete with an interrupted status
                Logger::debug("InternetTestScreen: Test interrupted by user");
                m_testCompleted = true;
                m_testResult = 2; // 2 = interrupted
                return true; // Stay in screen to show result
            }
        }
    }

    return m_running; // Continue as long as running is true
}

int InternetTestScreen::pingServer(const std::string& server, int timeoutSec)
{
    Logger::debug("InternetTestScreen: Pinging server " + server);
    
    // Construct ping command with timeout and minimal output
    std::string command = "ping -c 1 -W " + std::to_string(timeoutSec) + " " + server + " > /dev/null 2>&1";

    // Execute the command
    int result = system(command.c_str());
    
    // Log the result
    Logger::debug("InternetTestScreen: Ping returned " + std::to_string(result));

    // Return 0 for success, non-zero for failure
    return (result == 0) ? 0 : 1;
}
