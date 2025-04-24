#include "IPSelector.h"
#include "Logger.h"
#include <algorithm>
#include <unistd.h>

// Constructor
IPSelector::IPSelector(const std::string& defaultIp,
                       int yPos,
                       std::function<void(const std::string&)> callback,
                       std::function<void()> redrawCallback)
    : m_ipAddress(defaultIp.empty() ? "192.168.001.001" : defaultIp),
      m_cursorPosition(0),
      m_cursorMode(false),
      m_digitEditMode(false),
      m_yPosition(yPos),
      m_onIpChanged(callback),
      m_onRedraw(redrawCallback)
{
    // Ensure IP address is properly formatted (XXX.XXX.XXX.XXX)
    if (m_ipAddress.size() != 15) {
        Logger::warning("IP address not formatted correctly, using default");
        m_ipAddress = "192.168.001.001";
    }
}

// Reset the selector state
void IPSelector::reset()
{
    m_cursorMode = false;
    m_digitEditMode = false;
    m_cursorPosition = 0;
}

// Increment the digit at cursor position
void IPSelector::incrementDigit()
{
    // Skip dots
    if (m_ipAddress[m_cursorPosition] == '.') {
        return;
    }

    char digit = m_ipAddress[m_cursorPosition];

    // Increment the digit, wrapping from 9 to 0
    if (digit >= '0' && digit <= '9') {
        digit = (digit == '9') ? '0' : digit + 1;
        m_ipAddress[m_cursorPosition] = digit;

        // Call IP changed callback if provided
        if (m_onIpChanged) {
            m_onIpChanged(m_ipAddress);
        }

        // Trigger redraw if callback is provided
        if (m_onRedraw) {
            m_onRedraw();
        }
    }
}

// Decrement the digit at cursor position
void IPSelector::decrementDigit()
{
    // Skip dots
    if (m_ipAddress[m_cursorPosition] == '.') {
        return;
    }

    char digit = m_ipAddress[m_cursorPosition];

    // Decrement the digit, wrapping from 0 to 9
    if (digit >= '0' && digit <= '9') {
        digit = (digit == '0') ? '9' : digit - 1;
        m_ipAddress[m_cursorPosition] = digit;

        // Call IP changed callback if provided
        if (m_onIpChanged) {
            m_onIpChanged(m_ipAddress);
        }

        // Trigger redraw if callback is provided
        if (m_onRedraw) {
            m_onRedraw();
        }
    }
}

// Move cursor left, skipping dots
void IPSelector::moveCursorLeft()
{
    m_cursorPosition--;

    // Skip dots
    if (m_cursorPosition == 3 ||
        m_cursorPosition == 7 ||
        m_cursorPosition == 11) {
        m_cursorPosition--;
    }

    // Exit cursor mode if moving left from first position
    if (m_cursorPosition < 0) {
        m_cursorMode = false;
        m_digitEditMode = false;
        Logger::debug("Exiting cursor mode (moved left from first position)");
    }
}

// Move cursor right, skipping dots
void IPSelector::moveCursorRight()
{
    m_cursorPosition++;

    // Skip dots
    if (m_cursorPosition == 3 ||
        m_cursorPosition == 7 ||
        m_cursorPosition == 11) {
        m_cursorPosition++;
    }

    // Exit cursor mode if moving right past last position
    if (m_cursorPosition > 14) {
        m_cursorMode = false;
        m_digitEditMode = false;
        Logger::debug("Exiting cursor mode (moved right past last position)");
    }
}

// Handle button press
bool IPSelector::handleButton()
{
    Logger::debug("IPSelector: handleButton - Current state: cursorMode=" + 
                  std::to_string(m_cursorMode) + ", digitEditMode=" + 
                  std::to_string(m_digitEditMode));

    // First press: enter cursor mode
    if (!m_cursorMode && !m_digitEditMode) {
        m_cursorMode = true;
        m_cursorPosition = 0;
        
        // Trigger redraw
        if (m_onRedraw) {
            m_onRedraw();
        }
        
        Logger::debug("Entered cursor mode");
        return true;
    }

    // Second press: enter digit edit mode
    if (m_cursorMode && !m_digitEditMode) {
        m_digitEditMode = true;
        
        // Trigger redraw
        if (m_onRedraw) {
            m_onRedraw();
        }
        
        Logger::debug("Entered digit edit mode");
        return true;
    }

    // Third press: exit digit edit mode
    if (m_digitEditMode) {
        m_digitEditMode = false;
        
        // Trigger redraw
        if (m_onRedraw) {
            m_onRedraw();
        }
        
        Logger::debug("Exited digit edit mode");
        return true;
    }

    return false;
}


// Handle rotation
bool IPSelector::handleRotation(int direction)
{
    Logger::debug("IPSelector: handleRotation - Current state: cursorMode=" +
                  std::to_string(m_cursorMode) + ", digitEditMode=" +
                  std::to_string(m_digitEditMode) +
                  ", direction=" + std::to_string(direction) +
                  ", cursorPosition=" + std::to_string(m_cursorPosition));

    // Skip rotation if not in edit modes
    if (!m_cursorMode) {
        Logger::debug("Rotation ignored: not in cursor mode");
        return false;
    }

    // In digit edit mode, change the digit
    if (m_digitEditMode) {
        if (direction < 0) {
            decrementDigit();
            Logger::debug("Decremented digit at position " + std::to_string(m_cursorPosition));
        } else if (direction > 0) {
            incrementDigit();
            Logger::debug("Incremented digit at position " + std::to_string(m_cursorPosition));
        }
    }
    // In cursor mode (not digit edit), move the cursor
    else {
        if (direction < 0) {
            moveCursorLeft();
            Logger::debug("Moved cursor left to position " + std::to_string(m_cursorPosition));
        } else if (direction > 0) {
            moveCursorRight();
            Logger::debug("Moved cursor right to position " + std::to_string(m_cursorPosition));
        }
    }
    
    // Trigger redraw whenever there's cursor movement or digit editing
    if (m_onRedraw) {
        m_onRedraw();
    }
    
    return true;
}

// Draw the IP selector
void IPSelector::draw(bool selected, std::function<void(int, int, const std::string&)> drawFunc)
{
    if (!drawFunc) {
        Logger::error("Draw function not provided to IPSelector");
        return;
    }

    // IP Address line
    const std::string marker = selected ? ">" : " ";
    std::string line = marker + m_ipAddress;
    drawFunc(0, m_yPosition, line);

    // Cursor line: always show when IP is selected, with cursor positioning logic
    if (selected) {
        std::string cursorLine(16, ' ');

        // Determine cursor character
        if (m_digitEditMode && m_cursorPosition < 15) {
            cursorLine[1 + m_cursorPosition] = '*';  // Asterisk for digit edit mode
        } else if (m_cursorMode && m_cursorPosition < 15) {
            cursorLine[1 + m_cursorPosition] = '^';  // Caret for cursor position mode
        }

        drawFunc(0, m_yPosition + 8, cursorLine);
    } else {
        // Clear cursor line when not selected
        drawFunc(0, m_yPosition + 8, "                ");
    }
}

// Get the current IP address
const std::string& IPSelector::getIp() const
{
    return m_ipAddress;
}

// Set the IP address
void IPSelector::setIp(const std::string& ipAddress)
{
    if (ipAddress.empty() || ipAddress.size() != 15) {
        Logger::warning("Invalid IP address format");
        return;
    }

    m_ipAddress = ipAddress;

    // Call callback if provided
    if (m_onIpChanged) {
        m_onIpChanged(m_ipAddress);
    }
}

// Check if editing
bool IPSelector::isEditing() const
{
    return (m_cursorMode || m_digitEditMode);
}
