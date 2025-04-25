#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <sys/time.h>
#include "Config.h"

// Forward declarations
class Display;
class DisplayDevice;
class InputDevice;

/**
 * Base class for all menu items
 */
class MenuItem {
public:
    MenuItem(const std::string& label)
        : m_label(label), m_enabled(true) {}
    
    virtual ~MenuItem() = default;
    
    const std::string& getLabel() const {
        return m_label;
    }
    
    virtual void execute() = 0;
    
    void setEnabled(bool enabled) {
        m_enabled = enabled;
    }
    
    bool isEnabled() const {
        return m_enabled;
    }
    
private:
    std::string m_label;
    bool m_enabled;
};

/**
 * A simple action menu item that executes a callback function
 */
class ActionMenuItem : public MenuItem {
public:
    ActionMenuItem(const std::string& label, std::function<void()> action)
        : MenuItem(label), m_action(action) {}
    
    void execute() override {
        if (m_action) {
            m_action();
        }
    }
    
private:
    std::function<void()> m_action;
};

/**
 * A submenu item that contains its own menu
 */
class SubMenuItem : public MenuItem {
public:
    SubMenuItem(const std::string& label, std::shared_ptr<class Menu> submenu)
        : MenuItem(label), m_submenu(submenu) {}
    
    void execute() override;
    
private:
    std::shared_ptr<class Menu> m_submenu;
};

/**
 * Manages the display state and wraps the DisplayDevice
 */
class Display {
public:
    Display(std::shared_ptr<DisplayDevice> device);
    
    // Pass-through display commands
    void clear();
    void drawText(int x, int y, const std::string& text);
    void setCursor(int x, int y);
    void setInverted(bool inverted);
    void setBrightness(int brightness);
    void drawProgressBar(int x, int y, int width, int height, int percentage);
    void setPower(bool on);
    
    // State accessors
    bool isInverted() const { return m_inverted; }
    int getBrightness() const { return m_brightness; }
    bool isPoweredOn() const { return m_poweredOn; }
    bool isPowerSaveEnabled() const { return m_powerSaveEnabled; }
    
    // Power management
    void enablePowerSave(bool enable);
    void updateActivityTimestamp();
    void checkPowerSaveTimeout();
    bool isPowerSaveActivated() const { return m_powerSaveActivated; }
    void resetPowerSaveActivated() { m_powerSaveActivated = false; }
    
    // Check device state
    bool isDisconnected() const;
    
private:
    std::shared_ptr<DisplayDevice> m_device;
    bool m_inverted = false;
    int m_brightness = 128;
    bool m_poweredOn = true;
    bool m_powerSaveEnabled = false;
    bool m_powerSaveActivated = false;
    struct timeval m_lastActivityTime = {0, 0};
};

/**
 * Manages menu item selection and rendering
 */
class Menu {
public:
    Menu(std::shared_ptr<Display> display, const std::string& title = Config::MENU_TITLE);
    
    // Menu item management
    void addItem(std::shared_ptr<MenuItem> item);
    void removeItem(int index);
    void clear();
    std::shared_ptr<MenuItem> getItem(int index) const;
    size_t getItemCount() const { return m_items.size(); }
    
    // Selection state
    int getCurrentSelection() const { return m_currentItem; }
    void setCurrentSelection(int selection);
    
    // Rendering
    void render();
    void updateSelection(int oldSelection, int newSelection);
    
    // Navigation
    void moveSelectionUp(int steps = 1);
    void moveSelectionDown(int steps = 1);
    void executeSelected();
    
    // Input handling
    bool handleRotation(int direction);
    bool handleButtonPress();
    
    // Set Menu title
    void setTitle(const std::string& title) { m_title = title; }
    // Parent/child menu relationships
    void setParent(std::shared_ptr<Menu> parent) { m_parent = parent; }
    std::shared_ptr<Menu> getParent() const { return m_parent.lock(); }
    
private:
    std::string m_title = "MAIN MENU";
    std::shared_ptr<Display> m_display;
    std::vector<std::shared_ptr<MenuItem>> m_items;
    std::weak_ptr<Menu> m_parent;
    
    int m_currentItem = 0;
    int m_scrollOffset = 0;
    bool m_needsUpdate = false;
    bool m_updateInProgress = false;
    struct timeval m_lastUpdateTime = {0, 0};
};

/**
 * Implementation of SubMenuItem::execute() - needs to be here since it references Menu
 */
inline void SubMenuItem::execute() {
    if (m_submenu) {
        m_submenu->render();
        // Main execution loop for submenu would be implemented elsewhere
    }
}
