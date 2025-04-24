#pragma once

#include "ScreenModules.h"
#include "IPSelector.h"
#include <memory>
#include <string>

/**
 * @class IPSelectorScreen
 * @brief Screen module for IP address selection
 * 
 * This screen module demonstrates the usage of the IPSelector class
 * and allows the user to select an IP address.
 */
class IPSelectorScreen : public ScreenModule {
public:
    /**
     * @brief Constructor
     * 
     * @param display The display to use
     * @param input The input device to use
     * @param title The title to display
     * @param defaultIp The default IP address
     * @param onComplete Callback when IP is selected
     */
    IPSelectorScreen(std::shared_ptr<Display> display, 
                    std::shared_ptr<InputDevice> input,
                    const std::string& title = "IP Selector",
                    const std::string& defaultIp = "192.168.001.001",
                    std::function<void(const std::string&)> onComplete = nullptr);
    
    /**
     * @brief Destructor
     */
    ~IPSelectorScreen() override = default;
    
    /**
     * @brief Initialize the screen
     */
    void enter() override;
    
    /**
     * @brief Update the screen
     */
    void update() override;
    
    /**
     * @brief Clean up resources
     */
    void exit() override;
    
    /**
     * @brief Handle input events
     * 
     * @return true to continue, false to exit
     */
    bool handleInput() override;
    
    /**
     * @brief Get the selected IP address
     * 
     * @return The selected IP address
     */
    const std::string& getSelectedIp() const;
    
private:
    /**
     * @brief Called when the IP address is changed
     * 
     * @param ipAddress The new IP address
     */
    void onIpChanged(const std::string& ipAddress);
    
    std::string m_title;
    std::unique_ptr<IPSelector> m_ipSelector;
    std::string m_selectedIp;
    std::function<void(const std::string&)> m_onComplete;
    bool m_shouldExit = false;
};
