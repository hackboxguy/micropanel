#include "DeviceInterfaces.h"
#include "Config.h"
#include "Logger.h"
#include <cstring>
#include <cerrno>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/select.h>

InputDevice::InputDevice(const std::string& devicePath)
    : DeviceInterface(devicePath)
{
    // Initialize state
    m_state = {};

    // Initialize keyboard synthesis state
    m_keyboardState = {};
}

InputDevice::~InputDevice()
{
    close();
}

bool InputDevice::open()
{
    // Return true if already open
    if (isOpen()) {
        std::cout << "Input device already open with fd: " << m_fd << std::endl;
        return true;
    }

    std::cout << "Opening input device: " << m_devicePath << std::endl;

    // Open the input device in read-only mode
    m_fd = ::open(m_devicePath.c_str(), O_RDONLY);
    if (m_fd < 0) {
        std::cerr << "Failed to open input device: " << strerror(errno) << std::endl;
        return false;
    }

    std::cout << "Input device opened successfully with fd: " << m_fd << std::endl;

    // Set non-blocking mode
    setNonBlocking();
    // NEW: Grab exclusive access to the device
    if (ioctl(m_fd, EVIOCGRAB, 1) < 0) {
        std::cerr << "Failed to get exclusive access to input device: " << strerror(errno) << std::endl;
        // You can decide whether to continue or fail here
        // If this is critical, you might want to return false
        // For now, we'll just log the error and continue
    } else {
        std::cout << "Successfully grabbed exclusive access to input device" << std::endl;
    }

    // Test reading device capabilities
    unsigned long evbit[EV_MAX/8/sizeof(long) + 1];
    if (ioctl(m_fd, EVIOCGBIT(0, sizeof(evbit)), evbit) < 0) {
        std::cerr << "Failed to get device capabilities: " << strerror(errno) << std::endl;
    } else {
        std::cout << "Device supports:" << std::endl;
        if (evbit[EV_REL/8/sizeof(long)] & (1 << (EV_REL % (8 * sizeof(long))))) {
            std::cout << "  - EV_REL (Relative axes)" << std::endl;
        }
        if (evbit[EV_KEY/8/sizeof(long)] & (1 << (EV_KEY % (8 * sizeof(long))))) {
            std::cout << "  - EV_KEY (Keys/Buttons)" << std::endl;
        }
    }

    return true;
}

void InputDevice::close()
{
    if (isOpen()) {
        ioctl(m_fd, EVIOCGRAB, 0);
        ::close(m_fd);
        m_fd = -1;
    }
}

bool InputDevice::checkConnection() const
{
    if (!isOpen()) {
        return false;
    }

    // Try to read the device name - this should succeed if device is connected
    char name[256];
    if (ioctl(m_fd, EVIOCGNAME(sizeof(name)), name) < 0) {
        if (errno == EIO || errno == ENODEV || errno == ENXIO) {
            // These errors indicate device disconnection
            return false;
        }
    }

    return true;
}

void InputDevice::setNonBlocking()
{
    if (isOpen()) {
        int flags = fcntl(m_fd, F_GETFL, 0);
        if (flags < 0) {
            std::cerr << "Error getting file flags: " << strerror(errno) << std::endl;
            return;
        }
        fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);
    }
}

int InputDevice::waitForEvents(int timeoutMs)
{
    if (!isOpen()) {
        std::cerr << "Input device not open in waitForEvents" << std::endl;
        return -1;
    }

    // Double-check that the file descriptor is still valid
    if (fcntl(m_fd, F_GETFD) < 0) {
        std::cerr << "Input device file descriptor is invalid: " << strerror(errno) << std::endl;
        // Try to reopen the device
        close();
        if (!open()) {
            std::cerr << "Failed to reopen input device" << std::endl;
            return -1;
        }
        std::cout << "Successfully reopened input device" << std::endl;
        // NEW: Re-establish exclusive grab after reopening
        if (ioctl(m_fd, EVIOCGRAB, 1) < 0) {
            std::cerr << "Failed to re-grab exclusive access to input device: " << strerror(errno) << std::endl;
        } else {
            std::cout << "Successfully re-grabbed exclusive access to input device" << std::endl;
        }
    }

    fd_set readfds;
    struct timeval tv;
    FD_ZERO(&readfds);
    FD_SET(m_fd, &readfds);

    // Set timeout
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    // Wait for events with timeout
    int ret = select(m_fd + 1, &readfds, nullptr, nullptr, &tv);

    if (ret > 0) {
        ;//std::cout << "waitForEvents: Events available on input device" << std::endl;
    } else if (ret == 0) {
        // Timeout - normal, don't log to avoid spam
    } else if (errno != EINTR) {
        std::cerr << "Select error: " << strerror(errno) << std::endl;
    }

    return ret;
}

// NEW: Process pending keyboard synthesis events (simplified - no longer needed for single events)
void InputDevice::processKeyboardSynthesis(std::function<void(int)> onRotation)
{
    // NOTE: Currently not used since we switched to single events for keyboards
    // Keeping the function for future use if needed
    (void)onRotation; // Suppress unused parameter warning
}

bool InputDevice::processEvents(std::function<void(int)> onRotation, std::function<void()> onButtonPress)
{
    if (!isOpen()) {
        std::cerr << "Input device not open in processEvents" << std::endl;
        return false;
    }

    // First, process any pending keyboard synthesis events
    processKeyboardSynthesis(onRotation);

    struct input_event ev;
    int eventCount = 0;
    int btnPress = 0;
    bool pendingMovement = false;
    bool pendingVerticalMovement = false;
    struct timeval now;

    // Read and coalesce events
    ssize_t bytesRead;
    if (Logger::isVerbose()) {
        Logger::debug("Starting processEvents - device fd=" + std::to_string(m_fd));
    }
    while ((bytesRead = read(m_fd, &ev, sizeof(ev))) > 0 && eventCount < Config::MAX_EVENTS_PER_ITERATION) {
        if (Logger::isVerbose()) {
            Logger::debug("Input event received: type=" + std::to_string(ev.type) +
                         " code=" + std::to_string(ev.code) +
                         " value=" + std::to_string(ev.value));
        }

        // Handle SYN_REPORT events (type 0)
        if (ev.type == 0) {
            continue;
        }

        // Handle EV_KEY events (both keyboard keys and mouse buttons)
        if (ev.type == EV_KEY) {
            // Handle BTN_LEFT (RP2040 enter button) - EXISTING FUNCTIONALITY
            if (ev.code == BTN_LEFT && ev.value == 1) {
                // Mouse left button press (value 1 = pressed) - EXISTING
                if (Logger::isVerbose()) {
                    Logger::debug("BTN_LEFT press detected (RP2040 enter button)");
                }
                btnPress = 1;
                eventCount++;
            }
            // NEW: Handle keyboard keys - only process key press events (value == 1)
            else if (ev.value == 1) {
                gettimeofday(&now, nullptr);

                switch (ev.code) {
                    case KEY_LEFT: // 105
                        if (Logger::isVerbose()) {
                            Logger::debug("KEY_LEFT press detected - sending single REL_X event");
                        }
                        // Send single event immediately (no dual events for keyboard)
                        if (onRotation) {
                            onRotation(-5);
                        }
                        eventCount++;
                        break;

                    case KEY_RIGHT: // 106
                        if (Logger::isVerbose()) {
                            Logger::debug("KEY_RIGHT press detected - sending single REL_X event");
                        }
                        // Send single event immediately (no dual events for keyboard)
                        if (onRotation) {
                            onRotation(5);
                        }
                        eventCount++;
                        break;

                    case KEY_UP: // 103
                        if (Logger::isVerbose()) {
                            Logger::debug("KEY_UP press detected - sending single REL_Y event");
                        }
                        // Send single event immediately (no dual events for keyboard)
                        if (onRotation) {
                            onRotation(-5);  // UP moves menu selection up
                        }
                        eventCount++;
                        break;

                    case KEY_DOWN: // 108
                        if (Logger::isVerbose()) {
                            Logger::debug("KEY_DOWN press detected - sending single REL_Y event");
                        }
                        // Send single event immediately (no dual events for keyboard)
                        if (onRotation) {
                            onRotation(5);   // DOWN moves menu selection down
                        }
                        eventCount++;
                        break;

                    case KEY_ENTER: // 28
                        if (Logger::isVerbose()) {
                            Logger::debug("KEY_ENTER press detected");
                        }
                        btnPress = 1;
                        eventCount++;
                        break;

                    default:
                        // Ignore other keys
                        break;
                }
            }
            // Continue to next event
            continue;
        }

        // EXISTING: Process relative movement events (rotary encoder)
        else if (ev.type == EV_REL && ev.code == REL_X) {
            //std::cout << "Rotary encoder movement detected! Value: " << ev.value << std::endl;

            // Get current time
            gettimeofday(&now, nullptr);

            // Calculate time diff in milliseconds since last event
            long timeDiffMs = 0;
            if (m_state.lastEventTime.tv_sec != 0) {
                timeDiffMs = (now.tv_sec - m_state.lastEventTime.tv_sec) * 1000 +
                           (now.tv_usec - m_state.lastEventTime.tv_usec) / 1000;
            }

            // Update the last event time
            m_state.lastEventTime = now;

            // Reset paired count if this is a new movement after a long gap
            if (timeDiffMs > 100) {
                m_state.pairedEventCount = 0;
                m_state.totalRelX = 0;
                m_state.totalRelY = 0;
            }

            // Accumulate the value
            m_state.totalRelX += ev.value;
            m_state.pairedEventCount++;
            pendingMovement = true;

            //std::cout << "Accumulated REL_X: " << m_state.totalRelX
            //          << " (paired events: " << m_state.pairedEventCount << ")" << std::endl;

            eventCount++;
        }
        else if (ev.type == EV_REL && ev.code == REL_Y) {
            //std::cout << "Vertical movement detected! Value: " << ev.value << std::endl;

            // Get current time
            gettimeofday(&now, nullptr);

            // Calculate time diff in milliseconds since last event
            long timeDiffMs = 0;
            if (m_state.lastEventTime.tv_sec != 0) {
                timeDiffMs = (now.tv_sec - m_state.lastEventTime.tv_sec) * 1000 +
                           (now.tv_usec - m_state.lastEventTime.tv_usec) / 1000;
            }

            // Update the last event time
            m_state.lastEventTime = now;

            // Reset paired count if this is a new movement after a long gap
            if (timeDiffMs > 100) {
                m_state.pairedEventCount = 0;
                m_state.totalRelX = 0;
                m_state.totalRelY = 0;
            }

            // Accumulate the value
            m_state.totalRelY += ev.value;
            m_state.pairedEventCount++;
            pendingVerticalMovement = true;

            //std::cout << "Accumulated REL_Y: " << m_state.totalRelY
            //          << " (paired events: " << m_state.pairedEventCount << ")" << std::endl;

            eventCount++;
        }
        // Remove the duplicate BTN_LEFT handling since it's now handled above
        // (The old BTN_LEFT handling code was here but is now moved up to the EV_KEY section)

        // Avoid processing too many events at once
        if (eventCount >= Config::MAX_EVENTS_PER_ITERATION) {
            break;
        }
    }

    if (bytesRead < 0) {
        if (errno != EAGAIN) {
            if (Logger::isVerbose()) {
                Logger::debug("Error reading from input device: " + std::string(strerror(errno)));
            }
        } else if (Logger::isVerbose()) {
            Logger::debug("No events available (EAGAIN)");
        }
    } else if (bytesRead == 0 && Logger::isVerbose()) {
        Logger::debug("No bytes read (EOF)");
    }

    // Process button press if detected
    if (btnPress && onButtonPress) {
        if (Logger::isVerbose()) {
            Logger::debug("Calling button press callback");
        }
        onButtonPress();
    } else if (btnPress && !onButtonPress && Logger::isVerbose()) {
        Logger::debug("Button press detected but no callback provided");
    }

    // EXISTING: Handle relative movement processing
    // Now we handle the movement only if:
    // 1. We've received 2 or more events (paired_event_count >= 2), which means we've seen both events from one rotation
    // 2. OR if it's been more than 30ms since the last event, which means we might not get a paired event
    gettimeofday(&now, nullptr);
    long timeSinceLastMs = (m_state.lastEventTime.tv_sec - m_state.lastEventTime.tv_sec) * 1000 +
                         (now.tv_usec - m_state.lastEventTime.tv_usec) / 1000;

    if ((pendingMovement || pendingVerticalMovement) &&
        (m_state.pairedEventCount >= 2 || timeSinceLastMs > Config::EVENT_PROCESS_THRESHOLD)) {
        // Process horizontal movement (REL_X)
        if (m_state.totalRelX != 0 && onRotation) {
            if (Logger::isVerbose()) {
                Logger::debug("Calling rotation callback with REL_X value: " + std::to_string(m_state.totalRelX));
            }
            onRotation(m_state.totalRelX);
        }

        // Process vertical movement (REL_Y)
        // For vertical movement, we invert the value as up should be positive (REL_Y is negative for up)
        if (m_state.totalRelY != 0 && onRotation) {
            if (Logger::isVerbose()) {
                Logger::debug("Calling rotation callback with REL_Y value: " + std::to_string(-m_state.totalRelY));
            }
            // Invert Y value for more intuitive direction (negative is down, positive is up)
            onRotation(-m_state.totalRelY);
        }

        // Reset tracking variables
        m_state.pairedEventCount = 0;
        m_state.totalRelX = 0;
        m_state.totalRelY = 0;
    }

    // If we processed the maximum events, flush any remaining
    if (eventCount >= Config::MAX_EVENTS_PER_ITERATION) {
        // Drain input buffer
        while (read(m_fd, &ev, sizeof(ev)) > 0) {
            // Just drain, don't process
        }
    }

    if (Logger::isVerbose()) {
        Logger::debug("processEvents returning with eventCount=" + std::to_string(eventCount));
    }
    return eventCount > 0;
}
