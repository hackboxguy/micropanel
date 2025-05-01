#include "ScreenModules.h"
#include "Config.h"
#include "DeviceInterfaces.h"
#include "MenuSystem.h"
#include "Logger.h"
#include <iostream>
#include <unistd.h>
#include <memory>
#include <array>

GenericListScreen::GenericListScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input)
    : ScreenModule(display, input)
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
    m_maxVisibleItems = 4; // Just use a safe fixed value
    
    Logger::debug("GenericListScreen configured: " + m_id);
}

void GenericListScreen::enter()
{
    Logger::debug("Entering GenericListScreen: " + m_id);
    
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
                    return false;
                }
                
                // Execute action if defined
                if (!selectedItem.action.empty()) {
                    executeAction(selectedItem.action);
                    renderList(); // Redraw after action
                }
            }
        }
    }
    
    return !m_shouldExit;
}

void GenericListScreen::renderList()
{
    // Calculate visible items
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
    
    // Show scroll indicators if needed
    if (m_firstVisibleItem > 0) {
        m_display->drawText(15, 16, "^");
    }
    if (lastVisibleItem < static_cast<int>(m_items.size())) {
        m_display->drawText(15, 16 + ((m_maxVisibleItems - 1) * 10), "v");
    }
}

void GenericListScreen::executeAction(const std::string& actionTemplate)
{
    std::string action = actionTemplate;
    
    // Execute the command
    std::string result = executeCommand(action);
    Logger::debug("Executed action: " + action);
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
