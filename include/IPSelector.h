#pragma once

#include <string>
#include <functional>

/**
 * @class IPSelector
 * @brief Reusable IP address selector/editor UI element
 *
 * This class provides a UI element for selecting and editing an IP address.
 * It supports cursor navigation and digit editing modes.
 */
class IPSelector {
public:
    /**
     * @brief Construct a new IPSelector
     *
     * @param defaultIp Default IP address to start with
     * @param yPos Y position on the display
     * @param callback Callback function to be called when IP is changed
     * @param redrawCallback Optional callback to trigger UI redraw
     */
    IPSelector(const std::string& defaultIp = "192.168.001.001",
               int yPos = 0,
               std::function<void(const std::string&)> callback = nullptr,
               std::function<void()> redrawCallback = nullptr);

    /**
     * @brief Reset the selector state
     */
    void reset();

    /**
     * @brief Handle button press
     *
     * @return true if the button was handled
     */
    bool handleButton();

    /**
     * @brief Handle rotation of the encoder
     *
     * @param direction Rotation direction (negative = left, positive = right)
     * @return true if the rotation was handled
     */
    bool handleRotation(int direction);

    /**
     * @brief Draw the IP selector on the display
     *
     * @param selected Whether this selector is currently selected
     * @param drawFunc Function to draw text on the display
     */
    void draw(bool selected, std::function<void(int, int, const std::string&)> drawFunc);

    /**
     * @brief Get the current IP address
     *
     * @return The current IP address
     */
    const std::string& getIp() const;

    /**
     * @brief Set the IP address
     *
     * @param ipAddress New IP address
     */
    void setIp(const std::string& ipAddress);

    /**
     * @brief Check if the selector is in edit mode
     *
     * @return true if in edit mode (cursor or digit)
     */
    bool isEditing() const;

private:
    // Increment the digit at cursor position
    void incrementDigit();

    // Decrement the digit at cursor position
    void decrementDigit();

    // Move cursor left, skipping dots
    void moveCursorLeft();

    // Move cursor right, skipping dots
    void moveCursorRight();

    std::string m_ipAddress;                               // Current IP address
    int m_cursorPosition = 0;                             // Current cursor position
    bool m_cursorMode = false;                            // Whether cursor positioning mode is active
    bool m_digitEditMode = false;                         // Whether digit edit mode is active
    int m_yPosition = 0;                                  // Y position on the display
    std::function<void(const std::string&)> m_onIpChanged; // Callback when IP is changed
    std::function<void()> m_onRedraw;                     // Callback to trigger redraw
};
