#include "ScreenModules.h"
#include "MenuSystem.h"
#include "DeviceInterfaces.h"
#include "Config.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/sysinfo.h>

SystemStatsScreen::SystemStatsScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input)
    : ScreenModule(display, input)
{
    // Initialize last update time to 0
    m_lastUpdate = {0, 0};
}

void SystemStatsScreen::enter()
{
    // Clear display and show initial system info
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 3);
    
    // Draw title
    m_display->drawText(0, 0, "  System Stats");
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Draw separator
    m_display->drawText(0, 8, "----------------");
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Reset last update time to force immediate update
    m_lastUpdate = {0, 0};
    
    // Initial update of stats
    update();
}

void SystemStatsScreen::update()
{
    // Configure Y positions for the display elements
    const int CPU_LABEL_Y = 16;
    const int CPU_BAR_Y = 25;
    const int MEM_LABEL_Y = 42;
    const int MEM_BAR_Y = 51;
    
    // Update stats every STAT_UPDATE_SEC seconds
    struct timeval now;
    gettimeofday(&now, nullptr);
    long timeDiffSec = (now.tv_sec - m_lastUpdate.tv_sec);
    
    if (timeDiffSec >= Config::STAT_UPDATE_SEC || m_lastUpdate.tv_sec == 0) {
        // Get current system info
        std::string cpuStr;
        std::string memTotalStr;
        std::string memFreeStr;
        int cpuPercentage = 0;
        int memPercentage = 0;
        
        // Get system info
        getSystemInfo(cpuStr, memTotalStr, memFreeStr, cpuPercentage, memPercentage);
        
        // Update CPU label and value on the same line
        m_display->drawText(0, CPU_LABEL_Y, "CPU:");
        m_display->drawText(40, CPU_LABEL_Y, "    ");  // Clear old value
        m_display->drawText(40, CPU_LABEL_Y, cpuStr);
        
        // CPU progress bar on the next line (full width)
        m_display->drawProgressBar(0, CPU_BAR_Y, 128, 10, cpuPercentage);
        
        // Update Memory label and value on the same line
        m_display->drawText(0, MEM_LABEL_Y, "Memory:");
        m_display->drawText(55, MEM_LABEL_Y, "    ");  // Clear old value
        m_display->drawText(55, MEM_LABEL_Y, memTotalStr);
        
        // Memory progress bar on the next line (full width)
        m_display->drawProgressBar(0, MEM_BAR_Y, 128, 10, memPercentage);
        
        // Store current time as last update time
        m_lastUpdate = now;
    }
}

void SystemStatsScreen::exit()
{
    // Nothing special to clean up
}

bool SystemStatsScreen::handleInput()
{
    // Check for button press to exit
    if (m_input->waitForEvents(100) > 0) {
        bool buttonPressed = false;
        
        m_input->processEvents(
            [](int direction) {
                // Ignore rotation in this screen
            },
            [&]() {
                // Button press exits
                buttonPressed = true;
            }
        );
        
        if (buttonPressed) {
            m_display->updateActivityTimestamp();
            return false; // Exit module
        }
    }
    
    return true; // Continue running
}

void SystemStatsScreen::getSystemInfo(std::string& cpu, std::string& memTotal, 
                                     std::string& memFree, int& cpuPercentage, 
                                     int& memPercentage)
{
    // Get CPU usage - this is a simple approximation from /proc/stat
    static long prevIdle = 0;
    static long prevTotal = 0;
    
    std::ifstream statFile("/proc/stat");
    if (statFile) {
        std::string line;
        if (std::getline(statFile, line)) {
            std::istringstream iss(line);
            std::string cpu_label;
            long user, nice, system, idle, iowait, irq, softirq;
            
            // Read CPU statistics
            if (iss >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >> softirq) {
                long total = user + nice + system + idle + iowait + irq + softirq;
                long totalDiff = total - prevTotal;
                long idleDiff = idle - prevIdle;
                
                // Calculate CPU usage percentage
                cpuPercentage = 0;
                if (totalDiff > 0) {
                    cpuPercentage = static_cast<int>(100 * (totalDiff - idleDiff) / totalDiff);
                }
                
                // Update previous values for next calculation
                prevIdle = idle;
                prevTotal = total;
                
                // Format CPU usage string
                cpu = std::to_string(cpuPercentage) + "%";
            } else {
                cpu = "Err";
                cpuPercentage = 0;
            }
        } else {
            cpu = "Err";
            cpuPercentage = 0;
        }
    } else {
        cpu = "Err";
        cpuPercentage = 0;
    }
    
    // Get memory usage from sysinfo
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        // Calculate memory usage
        long totalRam = info.totalram * info.mem_unit;
        long freeRam = info.freeram * info.mem_unit;
        long usedRam = totalRam - freeRam;
        memPercentage = static_cast<int>(100 * usedRam / totalRam);
        
        // Format strings
        memTotal = std::to_string(memPercentage) + "%";
        memFree = std::to_string(100 - memPercentage) + "%";
    } else {
        memTotal = "Err";
        memFree = "Err";
        memPercentage = 0;
    }
}
