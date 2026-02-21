#include "ScreenModules.h"
#include "Config.h"
#include "DeviceInterfaces.h"
#include "MenuSystem.h"
#include "Logger.h"
#include <iostream>
#include <unistd.h>
#include <memory>
#include <array>
#include <sstream>
#include <sys/wait.h>
#include <signal.h>
#include <chrono>
#include <fstream>


GenericListScreen::GenericListScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input)
    : ScreenModule(display, input), m_selectedValue("")
{
}

GenericListScreen::~GenericListScreen()
{
    // Clean up any resources
}

void GenericListScreen::setConfig(const nlohmann::json& config)
{
    // Set screen title
    if (config.contains("title") && config["title"].is_string()) {
        m_title = config["title"].get<std::string>();
    }

    // Set module ID if provided
    if (config.contains("id") && config["id"].is_string()) {
        m_id = config["id"].get<std::string>();
    }

    // Clear existing items
    m_items.clear();

    // Parse list items
    if (config.contains("list_items") && config["list_items"].is_array()) {
        for (const auto& item : config["list_items"]) {
            ListItem listItem;

            if (item.contains("title") && item["title"].is_string()) {
                listItem.title = item["title"].get<std::string>();
            }

            if (item.contains("action") && item["action"].is_string()) {
                listItem.action = item["action"].get<std::string>();
            }

            // Parse async properties
            if (item.contains("async") && item["async"].is_boolean()) {
                listItem.async = item["async"].get<bool>();
            }

            if (item.contains("timeout") && item["timeout"].is_number_integer()) {
                listItem.timeout = item["timeout"].get<int>();
            }

            if (item.contains("log_file") && item["log_file"].is_string()) {
                listItem.log_file = item["log_file"].get<std::string>();
            }

            if (item.contains("progress_title") && item["progress_title"].is_string()) {
                listItem.progress_title = item["progress_title"].get<std::string>();
            }

	    if (item.contains("parse_progress") && item["parse_progress"].is_boolean()) {
                listItem.parse_progress = item["parse_progress"].get<bool>();
            }

            if (item.contains("result_pattern") && item["result_pattern"].is_string()) {
                listItem.result_pattern = item["result_pattern"].get<std::string>();
            }

            if (item.contains("result_prefix") && item["result_prefix"].is_string()) {
                listItem.result_prefix = item["result_prefix"].get<std::string>();
            }

            m_items.push_back(listItem);
        }
    }

    // Check if selection script exists
    if (config.contains("list_selection") && config["list_selection"].is_string()) {
        m_selectionScript = config["list_selection"].get<std::string>();
        m_stateMode = true;
    }

    // Set the maximum visible items
    m_maxVisibleItems = 6; // Just use a safe fixed value

    // Check for dynamic items source
    if (config.contains("items_source") && config["items_source"].is_string()) {
        m_itemsSource = config["items_source"].get<std::string>();
    }
    // Check for items path
    if (config.contains("items_path") && config["items_path"].is_string()) {
        m_itemsPath = config["items_path"].get<std::string>();
    }
    // Check for items action template
    if (config.contains("items_action") && config["items_action"].is_string()) {
        m_itemsAction = config["items_action"].get<std::string>();
    }

    // Check for prepend_static_items flag
    if (config.contains("prepend_static_items") && config["prepend_static_items"].is_boolean()) {
        m_prependStaticItems = config["prepend_static_items"].get<bool>();
    }

    // Load dynamic items if source is specified
    if (!m_itemsSource.empty()) {
        loadDynamicItems();
    }

    // Check if callbacks should be used
    if (config.contains("notify_on_exit") && config["notify_on_exit"].is_boolean()) {
        m_notifyOnExit = config["notify_on_exit"].get<bool>();
    }
    if (config.contains("callback_action") && config["callback_action"].is_string()) {
        m_callbackAction = config["callback_action"].get<std::string>();
    }
    Logger::debug("GenericListScreen configured: " + m_id);
}

void GenericListScreen::enter()
{
    Logger::debug("Entering GenericListScreen: " + m_id);
    // Reload dynamic items if needed
    if (!m_itemsSource.empty()) {
        loadDynamicItems();
    }

    // Reset state
    m_selectedIndex = 0;
    m_firstVisibleItem = 0;
    m_shouldExit = false;

    // Clear display
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 5);

    // Draw title
    m_display->drawText(0, 0, m_title);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Draw separator
    m_display->drawText(0, 8, "----------------");
    usleep(Config::DISPLAY_CMD_DELAY);

    // Render the list
    renderList();
}

void GenericListScreen::update()
{
    if (m_asyncState == AsyncState::RUNNING) {
        updateAsyncProgress();
    }
}

void GenericListScreen::exit()
{
    Logger::debug("Exiting GenericListScreen: " + m_id);

    // Clear the display
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 5);
}

bool GenericListScreen::handleInput()
{
    if (m_shouldExit) {
        // If we're exiting and notification is enabled, call the callback
        if (m_notifyOnExit && m_callback && !m_callbackAction.empty()) {
            notifyCallback(m_callbackAction, m_selectedValue);
        }
       return false;
    }

    // Handle async completion states - ANY input (rotation or button) should dismiss the message
    if (m_asyncWaitingForUser && (m_asyncState == AsyncState::COMPLETED ||
                                  m_asyncState == AsyncState::FAILED ||
                                  m_asyncState == AsyncState::TIMEOUT)) {
        if (m_input->waitForEvents(100) > 0) {
            bool inputReceived = false;
            m_input->processEvents(
                [&](int) { inputReceived = true; }, // Rotation also dismisses message
                [&]() { inputReceived = true; }      // Button press dismisses message
            );

            if (inputReceived) {
                Logger::debug("Async completion message dismissed by user input");
                // Return to normal list view
                m_asyncState = AsyncState::IDLE;
                m_asyncWaitingForUser = false;
                m_display->updateActivityTimestamp();
                enter(); // Redraw the list
            }
        }
        return true;
    }

    // Don't handle input during async process
    if (m_asyncState == AsyncState::RUNNING) {
        // Drain any pending input events to prevent them from being processed later
        while (m_input->waitForEvents(10) > 0) {
            m_input->processEvents([](int) {}, []() {});
        }
        return true;
    }

    if (m_input->waitForEvents(100) > 0) {
        bool buttonPressed = false;

        m_input->processEvents(
            [this](int direction) {
                // Handle rotation - navigate through items
                int oldSelection = m_selectedIndex;

                if (direction < 0) {
                    // Move up
                    if (m_selectedIndex > 0) {
                        m_selectedIndex--;
                    }
                } else {
                    // Move down
                    if (m_selectedIndex < static_cast<int>(m_items.size() - 1)) {
                        m_selectedIndex++;
                    }
                }

                // Handle scrolling for long lists
                if (m_selectedIndex < m_firstVisibleItem) {
                    m_firstVisibleItem = m_selectedIndex;
                } else if (m_selectedIndex >= m_firstVisibleItem + m_maxVisibleItems) {
                    m_firstVisibleItem = m_selectedIndex - m_maxVisibleItems + 1;
                }

                // Only redraw if selection changed
                if (oldSelection != m_selectedIndex) {
                    renderList();
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
            // Handle selected item
            if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_items.size())) {
                const auto& selectedItem = m_items[m_selectedIndex];

                // Handle "Back" item
                if (selectedItem.title == "Back" || selectedItem.title == "back" || selectedItem.title == "BACK") {
                    m_shouldExit = true;
                    return true;//false;
                }
                if (selectedItem.async) {
                    startAsyncProcess(selectedItem);
                } else {
                // Execute action if defined
                if (!selectedItem.action.empty()) {
                    executeAction(selectedItem.action);
                    // Call callback immediately if needed
                    if (m_callback && !m_callbackAction.empty() && !m_notifyOnExit) {
                        notifyCallback(m_callbackAction, m_selectedValue);
                    }
		    renderList(); // Redraw after action
                }
		}
            }
        }
    }

    return !m_shouldExit;
}

void GenericListScreen::renderList()
{
    Logger::debug("GenericListScreen::renderList() called for: " + m_id);
    // Clear lines 0 and 8 to ensure clean title and separator redraw
    m_display->drawText(0, 0, "                "); // Clear title line
    usleep(Config::DISPLAY_CMD_DELAY);
    m_display->drawText(0, 8, "                "); // Clear separator line
    usleep(Config::DISPLAY_CMD_DELAY);

    // Always redraw title and separator to ensure they're visible after returning from sub-modules
    m_display->drawText(0, 0, m_title);
    usleep(Config::DISPLAY_CMD_DELAY);
    m_display->drawText(0, 8, "----------------");
    usleep(Config::DISPLAY_CMD_DELAY);

    // If in state mode, run the selection script first
    if (m_stateMode && !m_selectionScript.empty()) {
        // Execute the script to get the current state
        std::string result = executeCommand(m_selectionScript);
        // Remove trailing newline
        if (!result.empty() && result.back() == '\n') {
            result.pop_back();
        }
        // Reset all selection states
        for (auto& item : m_items) {
            item.isSelected = false;
        }
        // Find the matching item
        for (auto& item : m_items) {
            if (item.title == result) {
                item.isSelected = true;
                break;
            }
        }
    }
    // Calculate visible items
    int lastVisibleItem = std::min(m_firstVisibleItem + m_maxVisibleItems,
                                  static_cast<int>(m_items.size()));

    // Draw visible options (clear and draw each line together to reduce flicker)
    for (int i = m_firstVisibleItem; i < lastVisibleItem; i++) {
        int displayIndex = i - m_firstVisibleItem;
        int yPos = 16 + (displayIndex * 8);
        std::string buffer;
        // Format with selection indicator and/or state highlight
        if (i == m_selectedIndex) {
            if (m_items[i].isSelected) {
                buffer = ">[" + m_items[i].title + "]";
            } else {
                buffer = "> " + m_items[i].title;
            }
        } else {
            if (m_items[i].isSelected) {
                buffer = " [" + m_items[i].title + "]";
            } else {
                buffer = "  " + m_items[i].title;
            }
        }
        // Truncate if too long
        if (buffer.length() > 16) {
            buffer = buffer.substr(0, 16);
        }
        // Pad to ensure line is fully overwritten (avoids need for separate clear)
        while (buffer.length() < 16) {
            buffer += " ";
        }
        m_display->drawText(0, yPos, buffer);
        usleep(Config::DISPLAY_CMD_DELAY);
    }
    // Clear any remaining lines if there are fewer items than max visible
    for (int i = lastVisibleItem - m_firstVisibleItem; i < m_maxVisibleItems; i++) {
        int yPos = 16 + (i * 8);
        m_display->drawText(0, yPos, "                ");
        usleep(Config::DISPLAY_CMD_DELAY);
    }

    // Show scroll indicators if needed
    //if (m_firstVisibleItem > 0) {
    //    m_display->drawText(15, 16, "^");
    //}
    //if (lastVisibleItem < static_cast<int>(m_items.size())) {
    //    m_display->drawText(15, 16 + ((m_maxVisibleItems - 1) * 10), "v");
    //}
}

void GenericListScreen::executeAction(const std::string& actionTemplate)
{
    std::string action = actionTemplate;
    // Handle $1 parameter substitution
    size_t paramPos = action.find("$1");
    if (paramPos != std::string::npos) {
        action.replace(paramPos, 2, m_items[m_selectedIndex].title);
    }

    // Check if this is a module launch action
    if (action.find("launch_module:") == 0) {
        // Handle module launching
        std::string moduleType = action.substr(14); // Remove "launch_module:" prefix
        std::string selectedValue = m_items[m_selectedIndex].title;

        Logger::debug("GenericListScreen '" + m_id + "' launching module: " + moduleType + " with parameter: " + selectedValue);

        // Launch the module with the selected value as parameter
        launchModule(moduleType, selectedValue);
    } else {
        // Traditional shell command execution (existing functionality)
        std::string result = executeCommand(action);
        Logger::debug("GenericListScreen '" + m_id + "' executed action: " + action);
        Logger::debug("Executed action: " + action);

        // If in state mode, redraw the screen to show updated state
        if (m_stateMode) {
            renderList();
        }
    }
}

void GenericListScreen::launchModule(const std::string& moduleType, const std::string& parameter)
{
    Logger::debug("GenericListScreen::launchModule - Type: " + moduleType + ", Parameter: " + parameter);

    // Use the callback system to request module launch from parent
    if (m_callback) {
        // Format: "launch_module:textbox:eth0"
        std::string action = "launch_module";
        std::string value = moduleType + ":" + parameter;

        Logger::debug("GenericListScreen: Notifying callback to launch module");
        m_callback->onScreenAction(m_id, action, value);
    } else {
        Logger::warning("GenericListScreen: No callback set - cannot launch module");
    }
}

std::string GenericListScreen::executeCommand(const std::string& command) const
{
    std::array<char, 128> buffer;
    std::string result;

    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) {
        return "ERROR";
    }

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }

    return result;
}

void GenericListScreen::loadDynamicItems()
{
    if (m_itemsSource.empty()) {
        return;  // No dynamic source defined
    }

    Logger::debug("Loading dynamic items from: " + m_itemsSource);

    // Build the command - include path if specified
    std::string command = m_itemsSource;
    if (!m_itemsPath.empty()) {
        command += " " + m_itemsPath;
    }

    // Execute the command to get the list of items
    std::string result = executeCommand(command);

    // Save any static items from list_items (like Stop-Playback and Back)
    std::vector<ListItem> staticItems;
    for (const auto& item : m_items) {
        if (item.title == "Back" || item.title == "Stop-Playback") {
            staticItems.push_back(item);
        }
    }

    // Clear existing items
    m_items.clear();

    // If prepending static items, add them first
    if (m_prependStaticItems) {
        for (const auto& item : staticItems) {
            m_items.push_back(item);
        }
    }

    // Parse the result line by line
    std::istringstream iss(result);
    std::string line;

    while (std::getline(iss, line)) {
        // Skip empty lines
        if (line.empty()) {
            continue;
        }

        // Remove trailing newline if present
        if (line.back() == '\n') {
            line.pop_back();
        }

        // Create a new item
        ListItem item;
        item.title = line;

        // Set the action using the template
        if (!m_itemsAction.empty()) {
            item.action = m_itemsAction;
        }

        m_items.push_back(item);
    }

    // If not prepending, add static items at the end (original behavior)
    if (!m_prependStaticItems) {
        for (const auto& item : staticItems) {
            m_items.push_back(item);
        }
    }

    Logger::debug("Loaded " + std::to_string(m_items.size()) + " items (including static items)");
}


// New async process methods
void GenericListScreen::startAsyncProcess(const ListItem& item)
{
    Logger::debug("Starting async process: " + item.action);
    m_parseProgress = item.parse_progress;
    m_lastParsedPercentage = -1;
    m_asyncResultPattern = item.result_pattern;
    m_asyncResultPrefix = item.result_prefix;

    // Prepare the command with parameter substitution
    std::string command = item.action;
    size_t paramPos = command.find("$1");
    if (paramPos != std::string::npos) {
        command.replace(paramPos, 2, item.title);
    }

    // Prepare progress title with parameter substitution
    m_asyncProgressTitle = item.progress_title;
    paramPos = m_asyncProgressTitle.find("$1");
    if (paramPos != std::string::npos) {
        m_asyncProgressTitle.replace(paramPos, 2, item.title);
    }

    // Clear the log file
    if (!item.log_file.empty()) {
        std::ofstream ofs(item.log_file, std::ofstream::trunc);
        ofs.close();
    }

    // Fork the process
    m_asyncPid = fork();
    if (m_asyncPid == 0) {
        // Child process - redirect output to log file
        if (!item.log_file.empty()) {
            freopen(item.log_file.c_str(), "w", stdout);
            freopen(item.log_file.c_str(), "w", stderr);
        }

        // Execute the command
        execl("/bin/sh", "sh", "-c", command.c_str(), (char*)NULL);
        _exit(1); // Should not reach here
    } else if (m_asyncPid > 0) {
        // Parent process - set up async state
        m_asyncState = AsyncState::RUNNING;
        m_asyncStartTime = std::chrono::steady_clock::now();
        m_asyncLogFile = item.log_file;
        m_asyncTimeout = item.timeout;
        m_asyncWaitingForUser = false;

        // Reset display state tracking
        m_lastDisplayedPercentage = -1;
        m_lastDisplayedTime = "";
        m_asyncDisplayInitialized = false;

        // Clear display and show initial progress
        m_display->clear();
        usleep(Config::DISPLAY_CMD_DELAY * 5);
        renderAsyncProgress();

        Logger::debug("Async process started with PID: " + std::to_string(m_asyncPid));
    } else {
        // Fork failed
        Logger::debug("Failed to fork async process");
        m_asyncState = AsyncState::FAILED;
        m_asyncResultMessage = "Failed to start process";
        m_asyncWaitingForUser = true;
        renderAsyncProgress();
    }
}

void GenericListScreen::updateAsyncProgress()
{
    // Check if process completed
    checkAsyncCompletion();

    // Check for timeout
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - m_asyncStartTime).count();

    if (elapsed >= m_asyncTimeout && m_asyncState == AsyncState::RUNNING) {
        Logger::debug("Async process timed out after " + std::to_string(elapsed) + " seconds");
        killAsyncProcess();
        m_asyncState = AsyncState::TIMEOUT;
        m_asyncResultMessage = "Action timed-out\nUpate failed!";
        m_asyncWaitingForUser = true;
    }

    // Update display
    renderAsyncProgress();
}

void GenericListScreen::renderAsyncProgress()
{
    if (m_asyncState == AsyncState::RUNNING) {
        // Calculate current progress info
        int currentPercentage = calculateProgressPercentage();
        std::string currentTime = formatElapsedTime();

        // Only update display if something changed or first time
        if (!m_asyncDisplayInitialized) {
            // First time - clear and draw everything
            m_display->clear();
            usleep(Config::DISPLAY_CMD_DELAY * 5);

            // Show progress title
            m_display->drawText(0, 0, m_asyncProgressTitle);
            usleep(Config::DISPLAY_CMD_DELAY);

            // Show initial progress
            std::string progressText = std::to_string(currentPercentage) + "% - " + currentTime;
            m_display->drawText(0, 16, progressText);
            usleep(Config::DISPLAY_CMD_DELAY);

            m_lastDisplayedPercentage = currentPercentage;
            m_lastDisplayedTime = currentTime;
            m_asyncDisplayInitialized = true;

        } else if (currentPercentage != m_lastDisplayedPercentage || currentTime != m_lastDisplayedTime) {
            // Only update the progress line if values changed
            std::string progressText = std::to_string(currentPercentage) + "% - " + currentTime;

            // Clear only the progress line area
            m_display->drawText(0, 16, "                "); // Clear with spaces
            usleep(Config::DISPLAY_CMD_DELAY);

            // Draw updated progress
            m_display->drawText(0, 16, progressText);
            usleep(Config::DISPLAY_CMD_DELAY);

            m_lastDisplayedPercentage = currentPercentage;
            m_lastDisplayedTime = currentTime;
        }
        // If nothing changed, don't update display at all

    } else if (m_asyncState == AsyncState::COMPLETED) {
        // Clear display once for completion message
        m_display->clear();
        usleep(Config::DISPLAY_CMD_DELAY * 5);

        m_display->drawText(0, 0, "Action");
        usleep(Config::DISPLAY_CMD_DELAY);
        m_display->drawText(0, 8, "Success!");
        usleep(Config::DISPLAY_CMD_DELAY);

        // Show result string if available (e.g., detected FPGA, flash info, etc.)
        if (!m_resultString.empty()) {
            m_display->drawText(0, 16, m_resultString);
            usleep(Config::DISPLAY_CMD_DELAY);
            m_display->drawText(0, 32, "Press any button");
            usleep(Config::DISPLAY_CMD_DELAY);
            m_display->drawText(0, 40, "to continue");
            usleep(Config::DISPLAY_CMD_DELAY);
        } else {
            m_display->drawText(0, 24, "Press any button");
            usleep(Config::DISPLAY_CMD_DELAY);
            m_display->drawText(0, 32, "to continue");
            usleep(Config::DISPLAY_CMD_DELAY);
        }

    } else if (m_asyncState == AsyncState::FAILED || m_asyncState == AsyncState::TIMEOUT) {
        // Clear display once for error message
        m_display->clear();
        usleep(Config::DISPLAY_CMD_DELAY * 5);

        // Split multi-line messages
        std::string msg = m_asyncResultMessage;
        size_t newlinePos = msg.find('\n');
        if (newlinePos != std::string::npos) {
            m_display->drawText(0, 0, msg.substr(0, newlinePos));
            usleep(Config::DISPLAY_CMD_DELAY);
            m_display->drawText(0, 8, msg.substr(newlinePos + 1));
            usleep(Config::DISPLAY_CMD_DELAY);
        } else {
            m_display->drawText(0, 0, msg);
            usleep(Config::DISPLAY_CMD_DELAY);
        }

        m_display->drawText(0, 24, "Press button");
        usleep(Config::DISPLAY_CMD_DELAY);
        m_display->drawText(0, 32, "to continue");
        usleep(Config::DISPLAY_CMD_DELAY);
    }
}

void GenericListScreen::checkAsyncCompletion()
{
    if (m_asyncPid <= 0 || m_asyncState != AsyncState::RUNNING) {
        return;
    }

    // Check if process is still running
    int status;
    pid_t result = waitpid(m_asyncPid, &status, WNOHANG);

    if (result == m_asyncPid) {
        // Process completed
        Logger::debug("Async process completed with status: " + std::to_string(status));

        // Check log file for success/error patterns
        if (parseLogForCompletion()) {
            m_asyncState = AsyncState::COMPLETED;
        } else {
            m_asyncState = AsyncState::FAILED;
            m_asyncResultMessage = "Update failed!";
        }

        m_asyncWaitingForUser = true;
        m_asyncPid = -1;

    } else if (result == -1) {
        // Error checking process status
        Logger::debug("Error checking async process status");
        m_asyncState = AsyncState::FAILED;
        m_asyncResultMessage = "Update status error";
        m_asyncWaitingForUser = true;
        m_asyncPid = -1;
    }
    // result == 0 means process is still running
}

void GenericListScreen::killAsyncProcess()
{
    if (m_asyncPid > 0) {
        Logger::debug("Killing async process: " + std::to_string(m_asyncPid));
        kill(m_asyncPid, SIGTERM);
        usleep(1000000); // Wait 1 second

        // Check if it's still running, force kill if needed
        int status;
        if (waitpid(m_asyncPid, &status, WNOHANG) == 0) {
            kill(m_asyncPid, SIGKILL);
            waitpid(m_asyncPid, &status, 0);
        }

        m_asyncPid = -1;
    }
}

bool GenericListScreen::parseLogForCompletion()
{
    if (m_asyncLogFile.empty()) {
        return true; // Assume success if no log file specified
    }

    std::ifstream logFile(m_asyncLogFile);
    if (!logFile.is_open()) {
        return false;
    }

    std::string line;
    bool foundSuccess = false;
    bool foundError = false;

    // Clear any previous result string
    m_resultString = "";

    while (std::getline(logFile, line)) {
        if (line.find("[SUCCESS]") != std::string::npos) {
            foundSuccess = true;
        }
        if (line.find("[ERROR]") != std::string::npos) {
            foundError = true;
        }

	// Check for RH850 MCU success patterns
        if (line.find("Flash verification successful") != std::string::npos ||
            line.find("Optionbyte verification successful") != std::string::npos) {
            foundSuccess = true;
        }

        // Check for RH850 MCU error patterns
        if (line.find("Error") != std::string::npos ||
            line.find("Failed") != std::string::npos ||
            line.find("failed") != std::string::npos) {
            foundError = true;
        }

        // Extract result using configurable pattern (generic result extraction)
        if (!m_asyncResultPattern.empty() && line.find(m_asyncResultPattern) != std::string::npos) {
            size_t pos = line.find(m_asyncResultPattern);
            if (pos != std::string::npos) {
                std::string extractedValue = line.substr(pos + m_asyncResultPattern.length());
                // Trim leading/trailing whitespace
                size_t start = extractedValue.find_first_not_of(" \t\r\n");
                size_t end = extractedValue.find_last_not_of(" \t\r\n");
                if (start != std::string::npos && end != std::string::npos) {
                    extractedValue = extractedValue.substr(start, end - start + 1);
                    // Apply prefix if configured
                    if (!m_asyncResultPrefix.empty()) {
                        m_resultString = m_asyncResultPrefix + extractedValue;
                    } else {
                        m_resultString = extractedValue;
                    }
                }
            }
        }
    }

    logFile.close();

    // Success if we found [SUCCESS] and no [ERROR]
    return foundSuccess && !foundError;
}

int GenericListScreen::calculateProgressPercentage()
{
    if (m_parseProgress) {
        int parsedProgress = parseProgressFromLog();
        if (parsedProgress >= 0) {
            return parsedProgress;
        }
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - m_asyncStartTime).count();

    int percentage = (elapsed * 100) / m_asyncTimeout;
    return std::min(percentage, 99); // Cap at 99% until actually complete
}

std::string GenericListScreen::formatElapsedTime()
{
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - m_asyncStartTime).count();

    int minutes = elapsed / 60;
    int seconds = elapsed % 60;

    return std::to_string(minutes) + ":" +
           (seconds < 10 ? "0" : "") + std::to_string(seconds);
}

int GenericListScreen::parseProgressFromLog()
{
    if (m_asyncLogFile.empty()) return -1;

    std::ifstream logFile(m_asyncLogFile);
    if (!logFile.is_open()) return -1;

    std::string line;
    int lastPercentage = -1;

    while (std::getline(logFile, line)) {
        // Look for percentage patterns like "14.7%" or "29.0%"
        size_t percentPos = line.find('%');
        if (percentPos != std::string::npos) {
            // Find the start of the number before %
            size_t start = percentPos;
            while (start > 0 && (std::isdigit(line[start-1]) || line[start-1] == '.')) {
                start--;
            }

            if (start < percentPos) {
                std::string percentStr = line.substr(start, percentPos - start);
                try {
                    float percent = std::stof(percentStr);
                    lastPercentage = static_cast<int>(percent);
                } catch (...) {
                    // Ignore parsing errors
                }
            }
        }
    }

    logFile.close();
    return lastPercentage;
}
void GenericListScreen::handleGPIORotation(int direction) {
    Logger::debug("GenericListScreen GPIO rotation: " + std::to_string(direction));

    // Handle async completion states - rotation should also dismiss the completion message
    if (m_asyncWaitingForUser && (m_asyncState == AsyncState::COMPLETED ||
                                  m_asyncState == AsyncState::FAILED ||
                                  m_asyncState == AsyncState::TIMEOUT)) {
        Logger::debug("Async process completed, rotation dismissing message and returning to list view");
        // Return to normal list view (same as button press behavior)
        m_asyncState = AsyncState::IDLE;
        m_asyncWaitingForUser = false;
        m_display->updateActivityTimestamp();
        enter(); // Redraw the list
        return; // Don't process the rotation as navigation
    }

    // Don't handle rotation during async process (while it's running)
    if (m_asyncState == AsyncState::RUNNING) {
        Logger::debug("Ignoring rotation - async process is running");
        return;
    }

    // Use the same navigation logic as handleInput()
    int oldSelection = m_selectedIndex;

    if (direction < 0) {
        // Move up
        if (m_selectedIndex > 0) {
            m_selectedIndex--;
        }
    } else {
        // Move down
        if (m_selectedIndex < static_cast<int>(m_items.size() - 1)) {
            m_selectedIndex++;
        }
    }

    // Handle scrolling for long lists (same as handleInput)
    if (m_selectedIndex < m_firstVisibleItem) {
        m_firstVisibleItem = m_selectedIndex;
    } else if (m_selectedIndex >= m_firstVisibleItem + m_maxVisibleItems) {
        m_firstVisibleItem = m_selectedIndex - m_maxVisibleItems + 1;
    }

    // Only redraw if selection changed (same as handleInput)
    if (oldSelection != m_selectedIndex) {
        renderList();
    }

    m_display->updateActivityTimestamp();
}

bool GenericListScreen::handleGPIOButtonPress() {
    Logger::debug("GenericListScreen GPIO button press - selecting item");

    // Handle async completion states - only accept input when waiting for user
    // This prevents the "press any button to continue" from triggering menu selection
    if (m_asyncWaitingForUser && (m_asyncState == AsyncState::COMPLETED ||
                                  m_asyncState == AsyncState::FAILED ||
                                  m_asyncState == AsyncState::TIMEOUT)) {
        Logger::debug("Async process completed, returning to list view");
        // Return to normal list view
        m_asyncState = AsyncState::IDLE;
        m_asyncWaitingForUser = false;
        m_display->updateActivityTimestamp();
        enter(); // Redraw the list
        return true; // Stay in the module, don't exit
    }

    // Don't process button presses during async process
    if (m_asyncState == AsyncState::RUNNING) {
        Logger::debug("Ignoring button press - async process is running");
        return true;
    }

    // Use the same selection logic as handleInput()
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_items.size())) {
        const auto& selectedItem = m_items[m_selectedIndex];

        // Handle "Back" item (same as handleInput)
        if (selectedItem.title == "Back" || selectedItem.title == "back" || selectedItem.title == "BACK") {
            m_shouldExit = true;
            return false; // Exit the module
        }

        if (selectedItem.async) {
            startAsyncProcess(selectedItem);
        } else {
            // Execute action if defined (same as handleInput)
            if (!selectedItem.action.empty()) {
                executeAction(selectedItem.action);
                // Call callback immediately if needed
                if (m_callback && !m_callbackAction.empty() && !m_notifyOnExit) {
                    notifyCallback(m_callbackAction, m_selectedValue);
                }
                renderList(); // Redraw after action
            }
        }
    }

    m_display->updateActivityTimestamp();
    return !m_shouldExit; // Continue running unless we should exit
}
