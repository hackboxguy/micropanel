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
        m_display->drawText(0, 0, "   IP Pinger");
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
        statusText = (m_pingResult == 0) ? "Success!" : "No Response";
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
        } else {
            m_pingResult = 1; // Error
        }

        Logger::debug("Ping completed with result: " + std::to_string(m_pingResult));

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
    m_pingInProgress = true;
    m_statusChanged = true;  // Force status update
    m_lastStatusText = "";   // Reset last status

    // Fork to run ping
    pid_t child_pid = fork();

    if (child_pid == 0) {
        // Child process - run ping
        char command[100];
        snprintf(command, sizeof(command), "ping -c 1 -W 2 %s > /dev/null 2>&1", ipAddress.c_str());
        int result = system(command);
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
