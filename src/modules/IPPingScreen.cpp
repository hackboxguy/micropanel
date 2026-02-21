#include "ScreenModules.h"
#include "MenuSystem.h"
#include "DeviceInterfaces.h"
#include "IPSelector.h"
#include "Logger.h"
#include "Config.h"
#include <iostream>
#include <unistd.h>
#include <string>
#include <memory>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <csignal>
#include <sys/wait.h>
#include <fstream>
#include <sstream>

static const std::string PING_TEMP_FILE = "/tmp/micropanel_ping_result.txt";

IPPingScreen::IPPingScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input)
    : ScreenModule(display, input)
{
    // Default IP initialization
    m_targetIp = "192.168.001.001";

    // Create IP Selector with callback
    auto callback = [this](const std::string& ip) {
        m_targetIp = ip;
        Logger::debug("IP address changed to: " + ip);
    };

    // Create redraw callback to update menu
    auto redrawCallback = [this]() {
        renderMenu(false);
    };

    m_ipSelector = std::make_unique<IPSelector>(m_targetIp, 16, callback, redrawCallback);
}

void IPPingScreen::enter() {
    Logger::debug("IPPingScreen: Entered");

    // Reset state
    m_state = IPPingMenuState::MENU_STATE_IP;
    m_pingInProgress = false;
    m_pingPid = -1;
    m_pingResult = -1;
    m_statusMessage.clear();
    m_shouldExit = false;
    m_lastStatusText = "";   // Reset last status
    m_statusChanged = true;  // Force initial status update
    m_pingTimeMs = 0.0;      // Reset ping time

    // Reset IP selector
    m_ipSelector->reset();

    // Full redraw
    renderMenu(true);
}

void IPPingScreen::update() {
    // Check ping status periodically if ping is in progress
    if (m_pingInProgress) {
        bool wasInProgress = m_pingInProgress;
        checkPingStatus();

        // Only update status if ping state changed or still in progress
        if (wasInProgress != m_pingInProgress || m_pingInProgress) {
            m_statusChanged = true;
        }
    }

    // Only update the status line when needed
    if (m_statusChanged) {
        updateStatusLine();
        m_statusChanged = false;
    }
}
void IPPingScreen::exit() {
    Logger::debug("IPPingScreen: Exiting");

    // Terminate any ongoing ping process
    if (m_pingInProgress && m_pingPid > 0) {
        kill(m_pingPid, SIGTERM);
        waitpid(m_pingPid, nullptr, 0);
        m_pingInProgress = false;
        m_pingPid = -1;
    }

    // Clean up temporary file if it exists
    unlink(PING_TEMP_FILE.c_str());

    // Clear display
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 3);
}

bool IPPingScreen::handleInput() {
    if (m_input->waitForEvents(100) > 0) {
        bool buttonPressed = false;
        int rotationDirection = 0;

        m_input->processEvents(
            [this, &rotationDirection](int direction) {
                // Store rotation direction
                rotationDirection = direction;
                m_display->updateActivityTimestamp();
            },
            [this, &buttonPressed]() {
                buttonPressed = true;
                m_display->updateActivityTimestamp();
            }
        );

        // Process button and rotation
        bool redrawNeeded = false;
        IPPingMenuState previousState = m_state;

        // Handle button press
        if (buttonPressed) {
            // Handle button based on current state
            switch (m_state) {
                case IPPingMenuState::MENU_STATE_IP:
                    // Let IP selector handle button first
                    if (m_ipSelector->handleButton()) {
                        redrawNeeded = true;
                    }
                    break;

                case IPPingMenuState::MENU_STATE_PING:
                    // Start ping
                    startPing();
                    redrawNeeded = true;
                    break;

                case IPPingMenuState::MENU_STATE_EXIT:
                    // Exit screen
                    m_shouldExit = true;
                    break;
            }
        }

        // Handle rotation
        if (rotationDirection != 0) {
            bool handled = false;

            // First try IP selector
            if (m_state == IPPingMenuState::MENU_STATE_IP) {
                handled = m_ipSelector->handleRotation(rotationDirection);
            }

            // If not handled by IP selector, handle menu navigation
            if (!handled) {
                if (rotationDirection < 0) {
                    // Rotate left - previous menu item
                    switch (m_state) {
                        case IPPingMenuState::MENU_STATE_IP:
                            m_state = IPPingMenuState::MENU_STATE_EXIT;
                            break;
                        case IPPingMenuState::MENU_STATE_PING:
                            m_state = IPPingMenuState::MENU_STATE_IP;
                            break;
                        case IPPingMenuState::MENU_STATE_EXIT:
                            m_state = IPPingMenuState::MENU_STATE_PING;
                            break;
                    }
                } else {
                    // Rotate right - next menu item
                    switch (m_state) {
                        case IPPingMenuState::MENU_STATE_IP:
                            m_state = IPPingMenuState::MENU_STATE_PING;
                            break;
                        case IPPingMenuState::MENU_STATE_PING:
                            m_state = IPPingMenuState::MENU_STATE_EXIT;
                            break;
                        case IPPingMenuState::MENU_STATE_EXIT:
                            m_state = IPPingMenuState::MENU_STATE_IP;
                            break;
                    }
                }
                redrawNeeded = true;
            }
        }

        // Redraw if state changed or needed
        if (redrawNeeded || previousState != m_state) {
            renderMenu(false);
        }
    }

    // Check if we should exit
    return !m_shouldExit;
}

void IPPingScreen::renderMenu(bool fullRedraw) {
    if (fullRedraw) {
        // Clear the screen
        m_display->clear();
        usleep(Config::DISPLAY_CMD_DELAY * 3);

        // Draw header
        //m_display->drawText(0, 0, "   IP Pinger");
        m_display->drawText(0, 0, "   Ping Test");
        usleep(Config::DISPLAY_CMD_DELAY);

        // Draw separator
        m_display->drawText(0, 8, Config::MENU_SEPARATOR);
        usleep(Config::DISPLAY_CMD_DELAY);
    }

    // Prepare draw function for IP selector
    auto drawFunc = [this](int x, int y, const std::string& text) {
        m_display->drawText(x, y, text);
        usleep(Config::DISPLAY_CMD_DELAY);
    };

    // Draw IP address with appropriate selection
    bool ipSelected = (m_state == IPPingMenuState::MENU_STATE_IP);
    m_ipSelector->draw(ipSelected, drawFunc);

    // Draw Ping line with selection marker
    std::string pingLine = (m_state == IPPingMenuState::MENU_STATE_PING ? ">Ping" : " Ping");
    m_display->drawText(0, 32, pingLine);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Draw Exit line with selection marker
    std::string exitLine = (m_state == IPPingMenuState::MENU_STATE_EXIT ? ">Exit" : " Exit");
    m_display->drawText(0, 40, exitLine);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Update status line
    updateStatusLine();
}

void IPPingScreen::updateStatusLine() {
    std::string statusText;

    // Handle progress of ping
    if (m_pingInProgress) {
        static int dots = 0;
        statusText = "Pinging" + std::string(dots, '.');
        dots = (dots + 1) % 4;
    }
    // Handle ping result
    else if (m_pingResult != -1) {
        if (m_pingResult == 0) {
            // Format with ping time if available
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "Success!(%.1fms)", m_pingTimeMs);
            statusText = buffer;
        } else {
            statusText = "No Response";
        }
    }

    // Only update display if status text has changed
    if (statusText != m_lastStatusText) {
        m_display->drawText(0, 48, "                ");
        if (!statusText.empty()) {
            m_display->drawText(0, 48, statusText);
        }
        usleep(Config::DISPLAY_CMD_DELAY);

        // Save current status text
        m_lastStatusText = statusText;
    }
}

void IPPingScreen::checkPingStatus() {
    if (!m_pingInProgress) return;

    // Check if the ping process has completed
    int status;
    pid_t result = waitpid(m_pingPid, &status, WNOHANG);

    if (result == m_pingPid) {
        // Ping process has completed
        m_pingInProgress = false;
        m_pingPid = -1;

        // Determine ping result
        if (WIFEXITED(status)) {
            m_pingResult = WEXITSTATUS(status);

            // If ping succeeded, try to read the ping time from temp file
            if (m_pingResult == 0) {
                std::ifstream pingFile(PING_TEMP_FILE);
                if (pingFile.is_open()) {
                    std::string line;
                    if (std::getline(pingFile, line) && !line.empty()) {
                        try {
                            m_pingTimeMs = std::stod(line);
                            // Additional validation: if time is 0.0, it might indicate parsing failure
                            if (m_pingTimeMs <= 0.0) {
                                Logger::error("Invalid ping time value: " + line);
                                m_pingResult = 1; // Treat as failure
                            }
                        } catch (...) {
                            Logger::error("Failed to parse ping time: " + line);
                            m_pingResult = 1; // Treat as failure
                            m_pingTimeMs = 0.0;
                        }
                    } else {
                        // Empty file or no line - ping actually failed
                        Logger::error("No ping time found in result file");
                        m_pingResult = 1; // Treat as failure
                        m_pingTimeMs = 0.0;
                    }
                    pingFile.close();
                } else {
                    // Could not open temp file - ping failed
                    Logger::error("Could not open ping result file");
                    m_pingResult = 1; // Treat as failure
                }

                // Clean up temporary file
                unlink(PING_TEMP_FILE.c_str());
            }
        } else {
            m_pingResult = 1; // Error
        }

        Logger::debug("Ping completed with result: " + std::to_string(m_pingResult) +
                     (m_pingResult == 0 ? " time: " + std::to_string(m_pingTimeMs) + "ms" : ""));

        // Mark status as changed so it will update once
        m_statusChanged = true;
    }
}


void IPPingScreen::startPing() {
    if (m_pingInProgress) return;

    // Get current IP for ping
    std::string ipAddress = m_ipSelector->getIp();
    Logger::debug("Starting ping to " + ipAddress);

    // Reset ping state
    m_pingResult = -1;
    m_pingTimeMs = 0.0;
    m_pingInProgress = true;
    m_statusChanged = true;  // Force status update
    m_lastStatusText = "";   // Reset last status

    // Fork to run ping
    pid_t child_pid = fork();

    if (child_pid == 0) {
        // Child process - run ping
        std::string command = "ping -c 1 -W 2 " + ipAddress +
            " | grep 'time=' | awk -F'time=' '{print $2}' | awk '{print $1}' > " + PING_TEMP_FILE;
        int result = system(command.c_str());
        std::quick_exit(result == 0 ? 0 : 1);
    } else if (child_pid < 0) {
        // Fork failed
        Logger::error("Failed to fork ping process");
        m_pingInProgress = false;
        m_pingResult = 1;
        m_statusChanged = true;  // Force status update
    } else {
        // Parent process
        m_pingPid = child_pid;
    }
}


const std::string& IPPingScreen::getSelectedIp() const {
    return m_ipSelector->getIp();
}


void IPPingScreen::handleGPIORotation(int direction)
{
    Logger::debug("IPPingScreen::handleGPIORotation(" + std::to_string(direction) + ")");

    bool handled = false;
    IPPingMenuState previousState = m_state;

    // First try IP selector if we're in IP state
    if (m_state == IPPingMenuState::MENU_STATE_IP) {
        handled = m_ipSelector->handleRotation(direction);
    }

    // If not handled by IP selector, handle menu navigation
    if (!handled) {
        if (direction < 0) {
            // Rotate left - previous menu item
            switch (m_state) {
                case IPPingMenuState::MENU_STATE_IP:
                    m_state = IPPingMenuState::MENU_STATE_EXIT;
                    break;
                case IPPingMenuState::MENU_STATE_PING:
                    m_state = IPPingMenuState::MENU_STATE_IP;
                    break;
                case IPPingMenuState::MENU_STATE_EXIT:
                    m_state = IPPingMenuState::MENU_STATE_PING;
                    break;
            }
        } else {
            // Rotate right - next menu item
            switch (m_state) {
                case IPPingMenuState::MENU_STATE_IP:
                    m_state = IPPingMenuState::MENU_STATE_PING;
                    break;
                case IPPingMenuState::MENU_STATE_PING:
                    m_state = IPPingMenuState::MENU_STATE_EXIT;
                    break;
                case IPPingMenuState::MENU_STATE_EXIT:
                    m_state = IPPingMenuState::MENU_STATE_IP;
                    break;
            }
        }

        Logger::debug("Menu state changed from " + std::to_string((int)previousState) + " to " + std::to_string((int)m_state));
    }

    // Redraw if state changed or IP selector was handled
    if (handled || previousState != m_state) {
        renderMenu(false);
    }

    m_display->updateActivityTimestamp();
}

bool IPPingScreen::handleGPIOButtonPress()
{
    Logger::debug("IPPingScreen::handleGPIOButtonPress() - state: " + std::to_string((int)m_state));

    bool redrawNeeded = false;

    // Handle button based on current state
    switch (m_state) {
        case IPPingMenuState::MENU_STATE_IP:
            // Let IP selector handle button
            Logger::debug("Handling IP selector button press");
            if (m_ipSelector->handleButton()) {
                redrawNeeded = true;
            }
            break;

        case IPPingMenuState::MENU_STATE_PING:
            // Start ping
            Logger::debug("Starting ping operation");
            startPing();
            redrawNeeded = true;
            break;

        case IPPingMenuState::MENU_STATE_EXIT:
            // Exit screen
            Logger::debug("Exit selected - leaving IPPingScreen");
            m_shouldExit = true;
            return false; // Exit the screen
    }

    // Redraw if needed
    if (redrawNeeded) {
        renderMenu(false);
    }

    m_display->updateActivityTimestamp();
    return !m_shouldExit; // Continue running unless exit was selected
}
