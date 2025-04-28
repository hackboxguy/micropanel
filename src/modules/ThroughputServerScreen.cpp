#include "ScreenModules.h"
#include "MenuSystem.h"
#include "DeviceInterfaces.h"
#include "Config.h"
#include "Logger.h"
#include "ModuleDependency.h"
#include <iostream>
#include <unistd.h>
#include <cstdlib>   // For std::exit
#include <sstream>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <fcntl.h>
#include <unistd.h>
/*ThroughputServerScreen::ThroughputServerScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input)
    : ScreenModule(display, input),
      m_selectedOption(0),
      m_port(5201),
      m_localIp(""),
      m_serverPid(-1),
      m_serverThread()
{
    // Initialize configuration
    auto& dependencies = ModuleDependency::getInstance();

    // Debug dependency lookup
    Logger::debug("ThroughputServerScreen: Looking for config in 'throughputserver'");

    // First check server-specific port config, then fall back to general throughput config
    std::string portStr = dependencies.getDependencyPath("throughputserver", "default_port");
    Logger::debug("ThroughputServerScreen: 'throughputserver/default_port' value: '" + portStr + "'");

    if (portStr.empty()) {
        Logger::debug("ThroughputServerScreen: Falling back to 'throughputtest'");
        portStr = dependencies.getDependencyPath("throughputtest", "default_port");
        Logger::debug("ThroughputServerScreen: 'throughputtest/default_port' value: '" + portStr + "'");
    }
    if (!portStr.empty()) {
        try {
            m_port = std::stoi(portStr);
            Logger::debug("ThroughputServerScreen: Using configured port: " + std::to_string(m_port));
        } catch (...) {
            Logger::warning("ThroughputServerScreen: Invalid port value in config, using default 5201");
        }
    }

    // Get local IP address
    getLocalIpAddress();
}
*/
ThroughputServerScreen::ThroughputServerScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input)
    : ScreenModule(display, input),
      m_selectedOption(0),
      m_port(5201),  // Default port
      m_localIp(""),
      m_serverPid(-1),
      m_serverThread()
{
    // Initialize configuration
    auto& dependencies = ModuleDependency::getInstance();

    // First try to get port from the server-specific config
    Logger::debug("ThroughputServerScreen: Attempting to read port from 'throughputserver/default_port'");
    std::string portStr = dependencies.getDependencyPath("throughputserver", "default_port");
    Logger::debug("ThroughputServerScreen: Got value: '" + portStr + "'");

    // If not found, try the general throughput config
    if (portStr.empty()) {
        Logger::debug("ThroughputServerScreen: Falling back to 'throughputtest/default_port'");
        portStr = dependencies.getDependencyPath("throughputtest", "default_port");
        Logger::debug("ThroughputServerScreen: Got value: '" + portStr + "'");
    }

    // Parse port value
    if (!portStr.empty()) {
        try {
            Logger::debug("ThroughputServerScreen: Converting port string '" + portStr + "' to integer");
            int portValue = std::stoi(portStr);

            // Only update m_port if a valid value was found
            if (portValue > 0 && portValue < 65536) {
                m_port = portValue;
                Logger::info("ThroughputServerScreen: Using configured port: " + std::to_string(m_port));
            } else {
                Logger::warning("ThroughputServerScreen: Invalid port value: " + std::to_string(portValue) +
                               ", using default 5201");
            }
        } catch (const std::exception& e) {
            Logger::warning("ThroughputServerScreen: Failed to parse port value '" + portStr +
                           "', using default 5201. Error: " + std::string(e.what()));
        }
    } else {
        Logger::warning("ThroughputServerScreen: No port configuration found, using default 5201");
    }

    // Print the final port value being used
    Logger::info("ThroughputServerScreen: Final configured port is: " + std::to_string(m_port));

    // Get local IP address
    getLocalIpAddress();
}

ThroughputServerScreen::~ThroughputServerScreen() {
    // Ensure server is stopped if running
    stopServer();

    // Make sure the thread has joined
    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }
    // Make sure Avahi is stopped (in case stopServer wasn't called)
    if (m_avahiPid > 0) {
        kill(m_avahiPid, SIGKILL);
        m_avahiPid = -1;
    }
}

void ThroughputServerScreen::enter() {
    Logger::debug("ThroughputServerScreen: Entered");
    m_running = true;
    refreshSettings();
    // Reset selection
    m_selectedOption = 0;

    // Update IP address in case network changed
    getLocalIpAddress();

    // Clear display
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 3);

    // Draw header
    m_display->drawText(0, 0, "Server Settings");
    usleep(Config::DISPLAY_CMD_DELAY);

    // Draw separator
    m_display->drawText(0, 8, "----------------");
    usleep(Config::DISPLAY_CMD_DELAY);

    // Draw options
    renderOptions();
}

void ThroughputServerScreen::update() {
    // Nothing to update periodically
}

void ThroughputServerScreen::exit() {
    Logger::debug("ThroughputServerScreen: Exiting");

    // Note: We deliberately do NOT stop the server here
    // to allow it to continue running in the background
}

bool ThroughputServerScreen::handleInput() {
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
                    } else {
                        m_selectedOption = m_options.size() - 1;
                    }
                } else {
                    // Move down
                    if (m_selectedOption < static_cast<int>(m_options.size() - 1)) {
                        m_selectedOption++;
                    } else {
                        m_selectedOption = 0;
                    }
                }

                // Only redraw if selection changed
                if (oldSelection != m_selectedOption) {
                    renderOptions();
                }

                m_display->updateActivityTimestamp();
            },
            [&buttonPressed, this]() {
                // Handle button press
                buttonPressed = true;
                m_display->updateActivityTimestamp();
                Logger::debug("ThroughputServerScreen: Button pressed");
            }
        );

        if (buttonPressed) {
            // Handle button press based on selected option
            switch (m_selectedOption) {
                case 0: // Start
                    if (!isServerRunning()) {
                        startServer();
                        renderOptions(); // Update display to show new state
                    }
                    break;

                case 1: // Stop
                    if (isServerRunning()) {
                        stopServer();
                        renderOptions(); // Update display to show new state
                    }
                    break;

                case 2: // Back
                    return false; // Exit the screen
            }
        }
    }

    return true; // Continue
}
/*
void ThroughputServerScreen::renderOptions() {
    bool serverRunning = isServerRunning();

    // Draw the menu options
    for (size_t i = 0; i < m_options.size(); i++) {
        // Determine if this option represents current state
        bool isActiveOption = (i == 0 && serverRunning) || (i == 1 && !serverRunning);

        std::string buffer;
        int yPos = 16 + (i * 10);  // Start at y=16 with 10px spacing

        // Clear the line first
        m_display->drawText(0, yPos, "                ");
        usleep(Config::DISPLAY_CMD_DELAY);

        // Format with selection indicator and/or state highlight
        if (static_cast<int>(i) == m_selectedOption) {
            if (isActiveOption) {
                buffer = ">[" + m_options[i] + "]";
            } else {
                buffer = ">" + m_options[i];
            }
        } else {
            if (isActiveOption) {
                buffer = " [" + m_options[i] + "]";
            } else {
                buffer = " " + m_options[i];
            }
        }

        m_display->drawText(0, yPos, buffer);
        usleep(Config::DISPLAY_CMD_DELAY);
    }

    // Draw IP address and port information at the bottom
    m_display->drawText(0, 46, m_localIp);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Show port information
    std::string portInfo = "Port:" + std::to_string(m_port);
    m_display->drawText(0, 56, portInfo);
    usleep(Config::DISPLAY_CMD_DELAY);
}
*/
void ThroughputServerScreen::renderOptions() {
    bool serverRunning = isServerRunning();

    // Clear display first
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 3);

    // Draw header with server status
    std::string headerText = serverRunning ? "Server(Running)" : "Server(Stopped)";
    m_display->drawText(0, 0, headerText);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Draw separator
    m_display->drawText(0, 8, "----------------");
    usleep(Config::DISPLAY_CMD_DELAY);

    // Draw the menu options (Start, Stop, Back)
    for (size_t i = 0; i < m_options.size(); i++) {
        // Determine if this option represents current state
        bool isActiveOption = (i == 0 && serverRunning) || (i == 1 && !serverRunning);

        std::string buffer;
        int yPos = 16 + (i * 10);  // Start at y=16 with 10px spacing

        // Format with selection indicator and/or state highlight
        if (static_cast<int>(i) == m_selectedOption) {
            if (isActiveOption) {
                buffer = ">[" + m_options[i] + "]";
            } else {
                buffer = ">" + m_options[i];
            }
        } else {
            if (isActiveOption) {
                buffer = " [" + m_options[i] + "]";
            } else {
                buffer = " " + m_options[i];
            }
        }

        m_display->drawText(0, yPos, buffer);
        usleep(Config::DISPLAY_CMD_DELAY);
    }

    // Add a blank line after "Back" (which is at y=36)
    // Draw IP at y=46 (with blank line above it)
    // Draw Port at y=46+8=54 (with no blank line between IP and Port)

    // Draw IP address at position 46
    m_display->drawText(0, 48, m_localIp);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Show port information at position 54 (8px below IP - no blank line)
    std::string portInfo = "Port:" + std::to_string(m_port);
    m_display->drawText(0, 56, portInfo);
    usleep(Config::DISPLAY_CMD_DELAY);
}

std::string ThroughputServerScreen::getIperf3Path() {
    auto& dependencies = ModuleDependency::getInstance();

    // Debug iperf3 path lookup
    Logger::debug("ThroughputServerScreen: Looking for iperf3 path");

    // First check server-specific iperf3 path, then fall back to general throughput config
    std::string path = dependencies.getDependencyPath("throughputserver", "iperf3_path");
    Logger::debug("ThroughputServerScreen: 'throughputserver/iperf3_path' value: '" + path + "'");

    if (path.empty()) {
        Logger::debug("ThroughputServerScreen: Falling back to 'throughputtest'");
        path = dependencies.getDependencyPath("throughputtest", "iperf3_path");
        Logger::debug("ThroughputServerScreen: 'throughputtest/iperf3_path' value: '" + path + "'");
    }
    if (path.empty()) {
        return "/usr/bin/iperf3";
    }
    return path;
}

void ThroughputServerScreen::getLocalIpAddress() {
    struct ifaddrs *ifaddr, *ifa;
    int family;
    char host[128];  // Fixed size buffer for IP address

    if (getifaddrs(&ifaddr) == -1) {
        Logger::error("ThroughputServerScreen: Failed to get network interfaces");
        m_localIp = "Unknown";
        return;
    }

    // Look for the first non-loopback IPv4 address
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) {
            continue;
        }

        family = ifa->ifa_addr->sa_family;

        // Only consider IPv4 addresses
        if (family == AF_INET) {
            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &(sa->sin_addr), host, sizeof(host));

            // Skip loopback addresses
            if (strcmp(host, "127.0.0.1") == 0) {
                continue;
            }

            m_localIp = host;
            Logger::debug("ThroughputServerScreen: Local IP address: " + m_localIp);
            break;
        }
    }

    freeifaddrs(ifaddr);

    if (m_localIp.empty()) {
        m_localIp = "Unknown";
        Logger::warning("ThroughputServerScreen: Could not determine local IP address");
    }
}

/*void ThroughputServerScreen::startServer() {
    // Check if iperf3 is available
    std::string iperf3Path = getIperf3Path();
    if (access(iperf3Path.c_str(), X_OK) != 0) {
        Logger::error("ThroughputServerScreen: iperf3 not found at: " + iperf3Path);
        return;
    }

    // First make sure any existing server is stopped
    stopServer();

    // Wait for thread to complete if any
    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }
    int serverPort = m_port;
    // Start iperf3 server in a separate thread to avoid blocking UI
    m_serverThread = std::thread([this, iperf3Path,serverPort]() {
        std::string portStr = std::to_string(serverPort);
        Logger::debug("ThroughputServerScreen: Thread using port: " + portStr);
	// Fork a child process to run iperf3
        pid_t pid = fork();

        if (pid == 0) {
            // We are in the child process
            // Redirect stderr to stdout
            dup2(1, 2);

            // Execute iperf3
            execl(iperf3Path.c_str(), "iperf3", "-s", "-p", portStr.c_str(), NULL);

            // If execl returns, it failed
            std::exit(1);
        } else if (pid > 0) {
            // We are in the parent process
            m_serverPid = pid;
            Logger::info("ThroughputServerScreen: iperf3 server started on port " + portStr +
                         " with PID " + std::to_string(pid));

            // Wait for child process to exit (will only happen when killed or on error)
            int status;
            waitpid(pid, &status, 0);

            // Check exit status
            if (WIFEXITED(status)) {
                Logger::debug("ThroughputServerScreen: iperf3 server exited with status " +
                              std::to_string(WEXITSTATUS(status)));
            } else if (WIFSIGNALED(status)) {
                Logger::debug("ThroughputServerScreen: iperf3 server terminated by signal " +
                              std::to_string(WTERMSIG(status)));
            }

            // Reset PID
            m_serverPid = -1;
        } else {
            // Fork failed
            Logger::error("ThroughputServerScreen: Failed to fork process for iperf3 server");
        }
    });

    // Detach thread to let it run independently
    m_serverThread.detach();

    // Give it a moment to start up
    usleep(100000); // 100ms
}

void ThroughputServerScreen::startServer() {
    // Check if iperf3 is available
    std::string iperf3Path = getIperf3Path();
    if (access(iperf3Path.c_str(), X_OK) != 0) {
        Logger::error("ThroughputServerScreen: iperf3 not found at: " + iperf3Path);
        return;
    }

    // First make sure any existing server is stopped
    stopServer();

    // Wait for thread to complete if any
    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }

    // Log configured port explicitly
    Logger::info("ThroughputServerScreen: Starting server on port: " + std::to_string(m_port));

    // Capture port value for the thread
    int serverPort = m_port;

    // Start iperf3 server in a separate thread to avoid blocking UI
    m_serverThread = std::thread([this, iperf3Path, serverPort]() {
        // Create the port string in this scope
        std::string portStr = std::to_string(serverPort);
        Logger::debug("ThroughputServerScreen: Thread using port: " + portStr);

        // Fork a child process to run iperf3
        pid_t pid = fork();

        if (pid == 0) {
            // We are in the child process

            // Open /dev/null for redirecting stdout and stderr
            int devNull = open("/dev/null", O_WRONLY);
            if (devNull == -1) {
                Logger::error("ThroughputServerScreen: Failed to open /dev/null");
                std::exit(1);
            }

            // Redirect stdout to /dev/null
            if (dup2(devNull, STDOUT_FILENO) == -1) {
                Logger::error("ThroughputServerScreen: Failed to redirect stdout");
                close(devNull);
                std::exit(1);
            }

            // Redirect stderr to /dev/null
            if (dup2(devNull, STDERR_FILENO) == -1) {
                Logger::error("ThroughputServerScreen: Failed to redirect stderr");
                close(devNull);
                std::exit(1);
            }

            // Close the file descriptor as it's no longer needed
            close(devNull);

            // Execute iperf3 with the port string from this scope
            execl(iperf3Path.c_str(), "iperf3", "-s", "-p", portStr.c_str(), NULL);

            // If execl returns, it failed
            std::exit(1);
        } else if (pid > 0) {
            // We are in the parent process
            m_serverPid = pid;
            Logger::info("ThroughputServerScreen: iperf3 server started on port " + portStr +
                         " with PID " + std::to_string(pid));

            // Wait for child process to exit (will only happen when killed or on error)
            int status;
            waitpid(pid, &status, 0);

            // Check exit status
            if (WIFEXITED(status)) {
                Logger::debug("ThroughputServerScreen: iperf3 server exited with status " +
                              std::to_string(WEXITSTATUS(status)));
            } else if (WIFSIGNALED(status)) {
                Logger::debug("ThroughputServerScreen: iperf3 server terminated by signal " +
                              std::to_string(WTERMSIG(status)));
            }

            // Reset PID
            m_serverPid = -1;
        } else {
            // Fork failed
            Logger::error("ThroughputServerScreen: Failed to fork process for iperf3 server");
        }
    });

    // Detach thread to let it run independently
    m_serverThread.detach();

    // Give it a moment to start up
    usleep(100000); // 100ms
}
*/
void ThroughputServerScreen::startServer() {
    // Check if iperf3 is available
    std::string iperf3Path = getIperf3Path();
    if (access(iperf3Path.c_str(), X_OK) != 0) {
        Logger::error("ThroughputServerScreen: iperf3 not found at: " + iperf3Path);
        return;
    }

    // First make sure any existing server is stopped
    stopServer();

    // Wait for thread to complete if any
    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }

    // Log configured port explicitly
    Logger::info("ThroughputServerScreen: Starting server on port: " + std::to_string(m_port));

    // Capture port value for the thread
    int serverPort = m_port;

    // Start iperf3 server in a separate thread to avoid blocking UI
    m_serverThread = std::thread([this, iperf3Path, serverPort]() {
        // Create the port string in this scope
        std::string portStr = std::to_string(serverPort);
        Logger::debug("ThroughputServerScreen: Thread using port: " + portStr);

        // Fork a child process to run iperf3
        pid_t pid = fork();

        if (pid == 0) {
            // We are in the child process
            // Open /dev/null for redirecting stdout and stderr
            int devNull = open("/dev/null", O_WRONLY);
            if (devNull == -1) {
                Logger::error("ThroughputServerScreen: Failed to open /dev/null");
                std::exit(1);
            }

            // Redirect stdout to /dev/null
            if (dup2(devNull, STDOUT_FILENO) == -1) {
                Logger::error("ThroughputServerScreen: Failed to redirect stdout");
                close(devNull);
                std::exit(1);
            }

            // Redirect stderr to /dev/null
            if (dup2(devNull, STDERR_FILENO) == -1) {
                Logger::error("ThroughputServerScreen: Failed to redirect stderr");
                close(devNull);
                std::exit(1);
            }

            // Close the file descriptor as it's no longer needed
            close(devNull);

            // Execute iperf3 with the port string from this scope
            execl(iperf3Path.c_str(), "iperf3", "-s", "-p", portStr.c_str(), NULL);

            // If execl returns, it failed
            std::exit(1);
        } else if (pid > 0) {
            // We are in the parent process
            m_serverPid = pid;
            Logger::info("ThroughputServerScreen: iperf3 server started on port " + portStr +
                         " with PID " + std::to_string(pid));

            // Wait for child process to exit (will only happen when killed or on error)
            int status;
            waitpid(pid, &status, 0);

            // Check exit status
            if (WIFEXITED(status)) {
                Logger::debug("ThroughputServerScreen: iperf3 server exited with status " +
                              std::to_string(WEXITSTATUS(status)));
            } else if (WIFSIGNALED(status)) {
                Logger::debug("ThroughputServerScreen: iperf3 server terminated by signal " +
                              std::to_string(WTERMSIG(status)));
            }

            // Reset PID
            m_serverPid = -1;
        } else {
            // Fork failed
            Logger::error("ThroughputServerScreen: Failed to fork process for iperf3 server");
        }
    });

    // Detach thread to let it run independently
    m_serverThread.detach();

    // Give it a moment to start up to ensure iperf3 is listening
    usleep(100000); // 100ms

    // Start Avahi service announcement after iperf3 is running
    if (isAvahiAvailable()) {
        // Fork process to run avahi-publish
        pid_t avahi_pid = fork();

        if (avahi_pid == 0) {
            // Child process - run avahi-publish
            // Create service name with IP and port for easier identification
            std::string serviceName = "MicroPanel iperf3 " + m_localIp;

            // Execute avahi-publish with the current port
            execl("/usr/bin/avahi-publish", "avahi-publish",
                  "-s", serviceName.c_str(),
                  "_iperf3._tcp", std::to_string(m_port).c_str(),
                  NULL);

            // If execl returns, it failed
            Logger::error("ThroughputServerScreen: Failed to start avahi-publish");
            std::exit(1);
        } else if (avahi_pid > 0) {
            // Parent process
            m_avahiPid = avahi_pid;
            Logger::info("ThroughputServerScreen: Announced service via Avahi with PID " +
                        std::to_string(avahi_pid));
        } else {
            // Fork failed
            Logger::warning("ThroughputServerScreen: Failed to fork for avahi-publish");
        }
    } else {
        Logger::warning("ThroughputServerScreen: Avahi not available, service will not be discoverable");
    }
}

void ThroughputServerScreen::stopServer() {
    // First, stop the Avahi announcement
    if (m_avahiPid > 0) {
        Logger::debug("ThroughputServerScreen: Stopping Avahi announcement with PID " +
                     std::to_string(m_avahiPid));
        // Send SIGTERM to the process
        if (kill(m_avahiPid, SIGTERM) == 0) {
            Logger::info("ThroughputServerScreen: Stopped Avahi announcement");
        } else {
            Logger::warning("ThroughputServerScreen: Failed to stop Avahi announcement, error: " +
                           std::string(strerror(errno)));

            // Try with SIGKILL if SIGTERM failed
            if (kill(m_avahiPid, SIGKILL) == 0) {
                Logger::info("ThroughputServerScreen: Forcefully terminated Avahi announcement");
            }
        }
        // Reset PID
        m_avahiPid = -1;
    }

    // Kill the server process if it's running
    if (m_serverPid > 0) {
        Logger::debug("ThroughputServerScreen: Stopping iperf3 server with PID " + std::to_string(m_serverPid));

        // Send SIGTERM to the process
        if (kill(m_serverPid, SIGTERM) == 0) {
            Logger::info("ThroughputServerScreen: Stopped iperf3 server");
        } else {
            Logger::warning("ThroughputServerScreen: Failed to stop iperf3 server, error: " +
                           std::string(strerror(errno)));

            // Try with SIGKILL if SIGTERM failed
            if (kill(m_serverPid, SIGKILL) == 0) {
                Logger::info("ThroughputServerScreen: Forcefully terminated iperf3 server");
            } else {
                Logger::error("ThroughputServerScreen: Failed to forcefully terminate iperf3 server, error: " +
                             std::string(strerror(errno)));
            }
        }

        // Reset PID
        m_serverPid = -1;
    }
}

bool ThroughputServerScreen::isServerRunning() const {
    if (m_serverPid <= 0) {
        return false;
    }

    // Check if process exists
    if (kill(m_serverPid, 0) == 0) {
        return true;
    }

    // Process doesn't exist anymore
    return false;
}
void ThroughputServerScreen::refreshSettings() {
    auto& dependencies = ModuleDependency::getInstance();

    // Read port from config
    std::string portStr = dependencies.getDependencyPath("throughputserver", "default_port");
    if (portStr.empty()) {
        portStr = dependencies.getDependencyPath("throughputtest", "default_port");
    }

    // Parse port value
    if (!portStr.empty()) {
        try {
            int portValue = std::stoi(portStr);
            if (portValue > 0 && portValue < 65536) {
                if (m_port != portValue) {
                    Logger::info("ThroughputServerScreen: Updating port from " +
                                std::to_string(m_port) + " to " + std::to_string(portValue));
                    m_port = portValue;
                }
            }
        } catch (...) {
            Logger::warning("ThroughputServerScreen: Failed to parse port value: " + portStr);
        }
    }
}
