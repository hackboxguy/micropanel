#include "MenuSystem.h"
#include "DeviceInterfaces.h"
#include <iostream>
#include <unistd.h>
#include <algorithm>

Menu::Menu(std::shared_ptr<Display> display, const std::string& title)
    : m_display(display), m_title(title)
{
    gettimeofday(&m_lastUpdateTime, nullptr);
}

void Menu::addItem(std::shared_ptr<MenuItem> item)
{
    m_items.push_back(item);
}

void Menu::removeItem(int index)
{
    if (index >= 0 && static_cast<size_t>(index) < m_items.size()) {
        m_items.erase(m_items.begin() + index);
        
        // Adjust current selection if needed
        if (m_currentItem >= static_cast<int>(m_items.size())) {
            m_currentItem = static_cast<int>(m_items.size()) - 1;
            if (m_currentItem < 0) {
                m_currentItem = 0;
            }
        }
    }
}

void Menu::clear()
{
    m_items.clear();
    m_currentItem = 0;
    m_scrollOffset = 0;
}

std::shared_ptr<MenuItem> Menu::getItem(int index) const
{
    if (index >= 0 && static_cast<size_t>(index) < m_items.size()) {
        return m_items[index];
    }
    return nullptr;
}

void Menu::setCurrentSelection(int selection)
{
    if (selection >= 0 && static_cast<size_t>(selection) < m_items.size()) {
        int oldSelection = m_currentItem;
        m_currentItem = selection;
        updateSelection(oldSelection, m_currentItem);
    }
}

void Menu::updateSelection(int oldSelection, int newSelection)
{
    // Check if either the old or new selection is currently visible
    bool oldVisible = (oldSelection >= m_scrollOffset &&
                      oldSelection < m_scrollOffset + Config::MENU_VISIBLE_ITEMS);
    bool newVisible = (newSelection >= m_scrollOffset &&
                     newSelection < m_scrollOffset + Config::MENU_VISIBLE_ITEMS);

    // If the new selection isn't visible, we need to scroll and redraw the whole menu
    if (!newVisible) {
        // Full redraw will handle the scrolling
        render();
        return;
    }
    
    // If the old selection isn't visible, just update the new selection
    if (!oldVisible) {
        int menuPos = newSelection - m_scrollOffset;
        // Format with the arrow indicator
        std::string buffer = "> " + m_items[newSelection]->getLabel();
        
        // Calculate position and draw
        int yPos = Config::MENU_START_Y + (menuPos * Config::MENU_ITEM_SPACING);
        m_display->drawText(0, yPos, buffer);
        return;
    }
    
    // Both old and new selection are visible, so just update those two lines
    // Update the old selection (remove the arrow)
    if (oldSelection >= 0 && static_cast<size_t>(oldSelection) < m_items.size()) {
        int menuPos = oldSelection - m_scrollOffset;
        // Format without the arrow indicator
        std::string buffer = "  " + m_items[oldSelection]->getLabel();
        
        // Calculate y position and draw
        int yPos = Config::MENU_START_Y + (menuPos * Config::MENU_ITEM_SPACING);
        m_display->drawText(0, yPos, buffer);
        usleep(Config::DISPLAY_CMD_DELAY);
    }

    // Update the new selection (add the arrow)
    if (newSelection >= 0 && static_cast<size_t>(newSelection) < m_items.size()) {
        int menuPos = newSelection - m_scrollOffset;
        // Format with the arrow indicator
        std::string buffer = "> " + m_items[newSelection]->getLabel();
        
        // Calculate y position and draw
        int yPos = Config::MENU_START_Y + (menuPos * Config::MENU_ITEM_SPACING);
        m_display->drawText(0, yPos, buffer);
        usleep(Config::DISPLAY_CMD_DELAY);
    }
}

void Menu::render()
{
    struct timeval now;
    gettimeofday(&now, nullptr);

    // Calculate time diff in milliseconds since last update
    long timeDiffMs = (now.tv_sec - m_lastUpdateTime.tv_sec) * 1000 +
                     (now.tv_usec - m_lastUpdateTime.tv_usec) / 1000;

    // Debounce display updates (don't update more often than every 100ms)
    if (timeDiffMs < Config::DISPLAY_UPDATE_DEBOUNCE) {
        m_needsUpdate = true;
        return;
    }

    // If an update is already in progress, just mark that we need another one
    if (m_updateInProgress) {
        m_needsUpdate = true;
        return;
    }

    m_updateInProgress = true;
    m_needsUpdate = false;

    // Calculate the scroll offset to ensure the selected item is visible
    if (m_currentItem < m_scrollOffset) {
        // Selected item is above the visible area - scroll up
        m_scrollOffset = m_currentItem;
    } else if (m_currentItem >= m_scrollOffset + Config::MENU_VISIBLE_ITEMS) {
        // Selected item is below the visible area - scroll down
        m_scrollOffset = m_currentItem - Config::MENU_VISIBLE_ITEMS + 1;
    }

    // Ensure scroll offset stays within valid range
    m_scrollOffset = std::max(0, m_scrollOffset);
    m_scrollOffset = std::min(static_cast<int>(m_items.size()) - Config::MENU_VISIBLE_ITEMS, m_scrollOffset);
    m_scrollOffset = std::max(0, m_scrollOffset);  // In case we have fewer items than visible slots

    // Clear the display first
    m_display->clear();
    usleep(Config::DISPLAY_CLEAR_DELAY);  // 50ms delay after clear

    // Draw a title at the top
    m_display->drawText(24, 0, m_title);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Draw a separator line
    m_display->drawText(0, 8, Config::MENU_SEPARATOR);
    usleep(Config::DISPLAY_CMD_DELAY);

    // Now draw visible menu items with proper spacing
    int displayedItems = 0;
    int maxItemsToDisplay = std::min(static_cast<int>(m_items.size()), Config::MENU_VISIBLE_ITEMS);

    for (int i = 0; i < maxItemsToDisplay; i++) {
        int menuIndex = i + m_scrollOffset;

        // Skip if we've gone past the end of the menu
        if (static_cast<size_t>(menuIndex) >= m_items.size()) {
            break;
        }

        std::string buffer;
        // Format menu item text with selection indicator
        if (menuIndex == m_currentItem) {
            buffer = "> " + m_items[menuIndex]->getLabel();
        } else {
            buffer = "  " + m_items[menuIndex]->getLabel();
        }

        // Calculate y position based on menu start position and spacing
        int yPos = Config::MENU_START_Y + (i * Config::MENU_ITEM_SPACING);
        m_display->drawText(0, yPos, buffer);
        displayedItems++;

        // Add delay between drawing commands
        usleep(Config::DISPLAY_CMD_DELAY);
    }

    // Draw scroll indicators if needed
    if (m_items.size() > Config::MENU_VISIBLE_ITEMS) {
        // Draw up arrow if there are items above
        if (m_scrollOffset > 0) {
            m_display->drawText(Config::DISPLAY_WIDTH - Config::MENU_SCROLL_INDICATOR_WIDTH, 
                             Config::MENU_START_Y, "^");
        }

        // Draw down arrow if there are items below
        if (m_scrollOffset + Config::MENU_VISIBLE_ITEMS < m_items.size()) {
            int yPos = Config::MENU_START_Y + ((Config::MENU_VISIBLE_ITEMS - 1) * Config::MENU_ITEM_SPACING);
            m_display->drawText(Config::DISPLAY_WIDTH - Config::MENU_SCROLL_INDICATOR_WIDTH, yPos, "v");
        }
    }

    // Make sure all commands are processed
    usleep(Config::DISPLAY_CMD_DELAY * 2);

    // Update the timestamp
    gettimeofday(&m_lastUpdateTime, nullptr);

    m_updateInProgress = false;

    // If another update was requested during this one, do it now
    if (m_needsUpdate) {
        usleep(Config::DISPLAY_CMD_DELAY * 2);
        m_needsUpdate = false;
        render();
    }
}

void Menu::moveSelectionUp(int steps)
{
    // Remember old selection
    int oldSelection = m_currentItem;

    // Move the selection up
    for (int i = 0; i < steps; i++) {
        if (m_currentItem > 0) {
            m_currentItem--;
        }
    }

    // If the selection actually changed, update the display
    if (oldSelection != m_currentItem) {
        updateSelection(oldSelection, m_currentItem);
    }
}

void Menu::moveSelectionDown(int steps)
{
    // Remember old selection
    int oldSelection = m_currentItem;

    // Move the selection down
    for (int i = 0; i < steps; i++) {
        if (m_currentItem < static_cast<int>(m_items.size() - 1)) {
            m_currentItem++;
        }
    }

    // If the selection actually changed, update the display
    if (oldSelection != m_currentItem) {
        updateSelection(oldSelection, m_currentItem);
    }
}

void Menu::executeSelected()
{
    if (m_currentItem >= 0 && static_cast<size_t>(m_currentItem) < m_items.size()) {
        if (m_items[m_currentItem]->isEnabled()) {
            m_items[m_currentItem]->execute();
        }
    }
}

bool Menu::handleRotation(int direction)
{
    //std::cout << "Menu::handleRotation called with direction: " << direction << std::endl;
    m_display->updateActivityTimestamp();
    
    if (direction < 0) {
        //std::cout << "Moving selection up" << std::endl;
        moveSelectionUp(1);
    } else if (direction > 0) {
        //std::cout << "Moving selection down" << std::endl;
        moveSelectionDown(1);
    }
    
    return true;
}

bool Menu::handleButtonPress()
{
    m_display->updateActivityTimestamp();
    executeSelected();
    return true;
}
