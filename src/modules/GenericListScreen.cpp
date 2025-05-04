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
    // Nothing to update periodically
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

    return !m_shouldExit;
}

void GenericListScreen::renderList()
{
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

    // Clear the list area
    for (int i = 0; i < m_maxVisibleItems; i++) {
        int yPos = 16 + (i * 8);
        m_display->drawText(0, yPos, "                ");
        usleep(Config::DISPLAY_CMD_DELAY);
    }
    // Draw visible options
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
        m_display->drawText(0, yPos, buffer);
        usleep(Config::DISPLAY_CMD_DELAY);
    }

/*    // Calculate visible items
    int lastVisibleItem = std::min(m_firstVisibleItem + m_maxVisibleItems,
                                  static_cast<int>(m_items.size()));
    // Clear the list area
    for (int i = 0; i < m_maxVisibleItems; i++) {
        int yPos = 16 + (i * 10); // Fixed positioning
        m_display->drawText(0, yPos, "                ");
        usleep(Config::DISPLAY_CMD_DELAY);
    }

    // Draw visible options
    for (int i = m_firstVisibleItem; i < lastVisibleItem; i++) {
        int displayIndex = i - m_firstVisibleItem;
        int yPos = 16 + (displayIndex * 10);

        std::string buffer;
        // Format with selection indicator
        if (i == m_selectedIndex) {
            buffer = "> " + m_items[i].title;
        } else {
            buffer = "  " + m_items[i].title;
        }

        // Truncate if too long
        if (buffer.length() > 16) {
            buffer = buffer.substr(0, 16);
        }

        m_display->drawText(0, yPos, buffer);
        usleep(Config::DISPLAY_CMD_DELAY);
    }
*/
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

    // Execute the command
    std::string result = executeCommand(action);
    Logger::debug("GenericListScreen '" + m_id + "' executed action: " + action);
    Logger::debug("Executed action: " + action);
    // If in state mode, redraw the screen to show updated state
    if (m_stateMode) {
        renderList();
    }
}

std::string GenericListScreen::executeCommand(const std::string& command) const
{
    std::array<char, 128> buffer;
    std::string result;

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return "ERROR";
    }

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }

    pclose(pipe);
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

    // Add back the static items in their original order
    for (const auto& item : staticItems) {
        m_items.push_back(item);
    }

    Logger::debug("Loaded " + std::to_string(m_items.size()) + " items (including static items)");
}
