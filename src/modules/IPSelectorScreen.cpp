#include "IPSelectorScreen.h"
#include "MenuSystem.h"
#include "DeviceInterfaces.h"
#include "Logger.h"
#include "Config.h"
#include <unistd.h>
#include <functional>

IPSelectorScreen::IPSelectorScreen(std::shared_ptr<Display> display, 
                                 std::shared_ptr<InputDevice> input,
                                 const std::string& title,
                                 const std::string& defaultIp,
                                 std::function<void(const std::string&)> onComplete)
    : ScreenModule(display, input),
      m_title(title),
      m_selectedIp(defaultIp),
      m_onComplete(onComplete)
{
    // Create the IP selector with a callback
    auto callback = [this](const std::string& ip) {
        this->onIpChanged(ip);
    };
    
    m_ipSelector = std::make_unique<IPSelector>(defaultIp, 30, callback);
}

void IPSelectorScreen::enter()
{
    Logger::debug("IPSelectorScreen: Entered");
    
    m_shouldExit = false;

    // Clear display
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 3);
    
    // Draw title
    m_display->drawText(0, 0, m_title);
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Draw separator
    m_display->drawText(0, 8, Config::MENU_SEPARATOR);
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Draw instructions
    m_display->drawText(0, 16, "Enter IP Address:");
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Create a draw function that uses our display
    auto drawFunc = [this](int x, int y, const std::string& text) {
        m_display->drawText(x, y, text);
        usleep(Config::DISPLAY_CMD_DELAY);
    };
    
    // Draw the IP selector
    m_ipSelector->draw(true, drawFunc);
}

void IPSelectorScreen::update()
{
    // Create a draw function that uses our display
    auto drawFunc = [this](int x, int y, const std::string& text) {
        m_display->drawText(x, y, text);
        usleep(Config::DISPLAY_CMD_DELAY);
    };
    
    // Redraw the IP selector if we're still active
    if (!m_shouldExit) {
        m_ipSelector->draw(true, drawFunc);
    }
}

void IPSelectorScreen::exit()
{
    Logger::debug("IPSelectorScreen: Exiting with IP: " + m_selectedIp);
    
    // Call the completion callback if provided
    if (m_onComplete) {
        m_onComplete(m_selectedIp);
    }
    
    // Clear display before returning to menu
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 3);
}

bool IPSelectorScreen::handleInput()
{
    if (m_input->waitForEvents(100) > 0) {
        bool buttonPressed = false;
        
        m_input->processEvents(
            [this](int direction) {
                // Pass rotation events to the IP selector
                bool handled = m_ipSelector->handleRotation(direction);
                if (handled) {
                    // If the selector handled the rotation, update the display
                    m_display->updateActivityTimestamp();
                    update();
                }
            },
            [&]() {
                // Handle button press
                buttonPressed = true;
                m_display->updateActivityTimestamp();
                
                // If in edit mode, pass to the selector
                if (m_ipSelector->isEditing()) {
                    bool handled = m_ipSelector->handleButton();
                    if (handled) {
                        // If the selector handled the button, update the display
                        update();
                    }
                } else {
                    // If not in edit mode, button press means we're done
                    m_shouldExit = true;
                }
            }
        );
        
        // Exit if button was pressed while not in edit mode
        if (buttonPressed && m_shouldExit) {
            return false; // Exit the screen
        }
    }
    
    return true; // Continue running
}

const std::string& IPSelectorScreen::getSelectedIp() const
{
    return m_selectedIp;
}

void IPSelectorScreen::onIpChanged(const std::string& ipAddress)
{
    Logger::debug("IP address changed to: " + ipAddress);
    m_selectedIp = ipAddress;
    
    // Update immediately
    update();
}
