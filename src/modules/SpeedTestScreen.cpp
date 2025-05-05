#include "ScreenModules.h"
#include "MenuSystem.h"
#include "DeviceInterfaces.h"
#include "Config.h"
#include "Logger.h"
#include "ModuleDependency.h"
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <curl/curl.h>

// Callback function for CURL to write downloaded data
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    (void)contents; 
    size_t realsize = size * nmemb;
    // We don't actually need to store the data, just count it
    (*(size_t*)userp) += realsize;
    return realsize;
}

SpeedTestScreen::SpeedTestScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input)
    : ScreenModule(display, input),
      m_downloadUrl(""),
      m_uploadScript(""),
      m_uploadEnabled(false),
      m_downloadInProgress(false),
      m_uploadInProgress(false),
      m_testCompleted(false),
      m_progress(0),
      m_downloadSpeed(0.0),
      m_uploadSpeed(0.0),
      m_testResult(-1),
      m_progressLastUpdated(0),
      m_animationLastUpdated(0),
      m_statusMessage(""),
      m_lastStatusText(""),
      m_statusChanged(false),
      m_shouldExit(false)
{
    // Initialize configuration
    checkConfiguration();
    
    // Initialize CURL (will be done only once)
    static bool curlInitialized = false;
    if (!curlInitialized) {
        curl_global_init(CURL_GLOBAL_ALL);
        curlInitialized = true;
    }
}

bool SpeedTestScreen::checkConfiguration() {
    // Get dependency manager instance
    auto& dependencies = ModuleDependency::getInstance();
    
    // Default URL is only used as a fallback if JSON config doesn't specify one
    const std::string defaultUrl = "https://cachefly.cachefly.net/10mb.test";
    
    // Check for download_url in dependencies
    std::string url = dependencies.getDependencyPath("speedtest", "download_url");
    
    if (!url.empty()) {
        m_downloadUrl = url;
        Logger::debug("SpeedTestScreen: Using configured download URL from JSON: " + m_downloadUrl);
    } else {
        Logger::warning("SpeedTestScreen: No download_url in JSON config, using default: " + defaultUrl);
        m_downloadUrl = defaultUrl;
    }
    
    // Check for upload script configuration
    m_uploadScript = dependencies.getDependencyPath("speedtest", "upload_script");
    if (!m_uploadScript.empty()) {
        Logger::debug("SpeedTestScreen: Upload script configured: " + m_uploadScript);
        if (access(m_uploadScript.c_str(), X_OK) == 0) {
            m_uploadEnabled = true;
            Logger::debug("SpeedTestScreen: Upload testing enabled");
        } else {
            Logger::warning("SpeedTestScreen: Upload script not executable: " + m_uploadScript);
            m_uploadEnabled = false;
        }
    }
    
    return true;
}

void SpeedTestScreen::enter() {
    Logger::debug("SpeedTestScreen: Entered");
    m_running = true;
    
    // Reset state
    m_downloadInProgress = false;
    m_uploadInProgress = false;
    m_testCompleted = false;
    m_progress = 0;
    m_downloadSpeed = 0.0;
    m_uploadSpeed = 0.0;
    m_testResult = -1;
    m_shouldExit = false;
    m_statusMessage.clear();
    m_lastStatusText = "";
    m_statusChanged = true;
    m_progressLastUpdated = 0;
    m_animationLastUpdated = 0;
    
    // Clear display
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 3);
    
    // Render initial screen
    renderScreen();
    
    // Start download test automatically after a brief delay
    usleep(500000);  // 500ms delay
    startDownloadTest();
}

void SpeedTestScreen::update() {
    auto currentTime = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        currentTime - m_startTime).count();
    
    // Update animation every 250ms if test is in progress
    bool shouldUpdateAnimation = (elapsedMs - m_animationLastUpdated >= 250);
    if (shouldUpdateAnimation && (m_downloadInProgress || m_uploadInProgress) && !m_testCompleted) {
        m_animationLastUpdated = elapsedMs;
        
        // Update progress animation
        std::string statusText;
        if (m_downloadInProgress) {
            int dots = ((elapsedMs / 250) % 4);
            statusText = "Download Test";
            for (int i = 0; i < dots; i++) {
                statusText += ".";
            }
            for (int i = dots; i < 3; i++) {
                statusText += " ";
            }
        } else if (m_uploadInProgress) {
            int dots = ((elapsedMs / 250) % 4);
            statusText = "Upload Test";
            for (int i = 0; i < dots; i++) {
                statusText += ".";
            }
            for (int i = dots; i < 3; i++) {
                statusText += " ";
            }
        }
        
        m_display->drawText(0, 20, "                ");
        m_display->drawText(0, 20, statusText);
        usleep(Config::DISPLAY_CMD_DELAY);
    }
    
    // Update progress bar every 100ms during tests
    bool shouldUpdateProgress = (elapsedMs - m_progressLastUpdated >= 100);
    if (shouldUpdateProgress && (m_downloadInProgress || m_uploadInProgress) && !m_testCompleted) {
        m_progressLastUpdated = elapsedMs;
        
        // Progress is a percentage of expected test duration (15 seconds)
        int progress = static_cast<int>((elapsedMs * 100) / 15000);
        if (progress > 95) progress = 95;  // Cap at 95% until complete
        
        if (progress > m_progress) {
            m_progress = progress;
            m_display->drawProgressBar(10, 35, 108, 15, progress);
            usleep(Config::DISPLAY_CMD_DELAY);
        }
    }
    
    // Update status line if status has changed
    if (m_statusChanged) {
        updateStatusLine();
        m_statusChanged = false;
    }
    
    // If test completes, update status
    if (m_testCompleted && m_progress < 100) {
        // Update progress bar to 100%
        m_display->drawProgressBar(10, 35, 108, 15, 100);
        m_progress = 100;
        
        // Update status line
        m_statusChanged = true;
        
        // If this was the download test and upload is enabled, start upload test
        if (m_downloadInProgress && m_uploadEnabled) {
            // Delay a bit before starting upload test
            usleep(1000000);  // 1 second delay
            m_downloadInProgress = false;
            startUploadTest();
        }
        else {
            displayFinalResults();
        }
    }
}

void SpeedTestScreen::exit() {
    Logger::debug("SpeedTestScreen: Exiting");
    
    // Cancel any ongoing test
    m_downloadInProgress = false;
    m_uploadInProgress = false;
    
    // If test thread is joinable, wait for it to complete
    if (m_testThread.joinable()) {
        m_testThread.join();
    }
    
    // Clear display
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 3);
}

bool SpeedTestScreen::handleInput() {
    // Process input events
    if (m_input->waitForEvents(100) > 0) {
        bool buttonPressed = false;
        
        m_input->processEvents(
            [this](int direction) {
                // Ignore rotation for now
                (void)direction; 
	        m_display->updateActivityTimestamp();
            },
            [&buttonPressed, this]() {
                // Button press
                buttonPressed = true;
                m_display->updateActivityTimestamp();
                Logger::debug("SpeedTestScreen: Button pressed");
            }
        );
        
        if (buttonPressed) {
            if (m_testCompleted || (!m_downloadInProgress && !m_uploadInProgress)) {
                // If test is complete or not running, exit on button press
                Logger::debug("SpeedTestScreen: Test completed, exiting on button press");
                m_shouldExit = true;
            }
            else {
                // If test is in progress, cancel it
                Logger::debug("SpeedTestScreen: Test in progress, canceling");
                m_downloadInProgress = false;
                m_uploadInProgress = false;
                m_testCompleted = true;
                m_testResult = 1;  // Mark as failed/canceled
                m_statusChanged = true;
            }
        }
    }
    
    return !m_shouldExit;
}

void SpeedTestScreen::renderScreen() {
    // Clear the screen
    m_display->clear();
    usleep(Config::DISPLAY_CMD_DELAY * 3);
    
    // Draw header
    m_display->drawText(0, 0, "   Speed Test");
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Draw separator
    m_display->drawText(0, 8, Config::MENU_SEPARATOR);
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Draw initial message
    m_display->drawText(0, 20, "Initializing...");
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Initial progress bar (0%)
    m_display->drawProgressBar(10, 35, 108, 15, 0);
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Draw status line
    updateStatusLine();
}

void SpeedTestScreen::updateStatusLine() {
    std::string statusText;
    
    if (m_downloadInProgress) {
        statusText = "Testing download...";
    } else if (m_uploadInProgress) {
        statusText = "Testing upload...";
    } else if (m_testCompleted) {
        if (m_testResult == 0) {
            statusText = "Test completed";
        } else {
            statusText = "Test failed";
        }
    } else {
        statusText = "Testing..";
    }
    
    // Only update if status text has changed
    if (statusText != m_lastStatusText) {
        m_display->drawText(0, 60, "                ");
        if (!statusText.empty()) {
            m_display->drawText(0, 60, statusText);
        }
        usleep(Config::DISPLAY_CMD_DELAY);
        
        // Save current status text
        m_lastStatusText = statusText;
    }
}

void SpeedTestScreen::startDownloadTest() {
    if (m_downloadInProgress || m_uploadInProgress) {
        return;  // Test already in progress
    }
    
    Logger::debug("SpeedTestScreen: Starting download test to " + m_downloadUrl);
    
    // Reset state
    m_testCompleted = false;
    m_testResult = -1;
    m_progress = 0;
    m_downloadSpeed = 0.0;
    m_statusChanged = true;
    m_downloadInProgress = true;
    
    // Record start time
    m_startTime = std::chrono::steady_clock::now();
    
    // Start test in separate thread
    m_testThread = std::thread([this]() {
        // Initialize curl
        CURL *curl;
        CURLcode res;
        size_t downloadedBytes = 0;
        
        curl = curl_easy_init();
        if (curl) {
            // Set the URL
            curl_easy_setopt(curl, CURLOPT_URL, m_downloadUrl.c_str());
            
            // Set write callback function
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &downloadedBytes);
            
            // Follow redirects
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            
            // Set maximum redirects
            curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
            
            // Set a reasonable timeout (30 seconds)
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
            
            // Disable progress meter
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
            
            // Set user agent
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "MicroPanel SpeedTest/1.0");
            
            // Don't verify SSL certificates for HTTP URLs (avoids SSL issues)
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
            
            // Fail on HTTP error (like 404, 500, etc.)
            curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
            
            // Set connection timeout
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
            
            // Add some HTTP headers to avoid caching
            struct curl_slist *headers = NULL;
            headers = curl_slist_append(headers, "Cache-Control: no-cache, no-store");
            headers = curl_slist_append(headers, "Pragma: no-cache");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            
            // Start the download
            auto startTime = std::chrono::steady_clock::now();
            res = curl_easy_perform(curl);
            auto endTime = std::chrono::steady_clock::now();
            
            // Calculate time taken
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            
            // Get more info about the transfer
            double contentLength = 0;
            curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &contentLength);
            double speedDownload = 0;
            curl_easy_getinfo(curl, CURLINFO_SPEED_DOWNLOAD, &speedDownload);
            long httpCode = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
            
            // Clean up headers
            curl_slist_free_all(headers);
            
            // Log more details about the download
            Logger::debug("SpeedTestScreen: HTTP Response Code: " + std::to_string(httpCode));
            Logger::debug("SpeedTestScreen: Content length: " + std::to_string(contentLength) + " bytes");
            Logger::debug("SpeedTestScreen: CURL reported speed: " + std::to_string(speedDownload) + " bytes/sec");
            Logger::debug("SpeedTestScreen: Actual downloaded: " + std::to_string(downloadedBytes) + " bytes");
            Logger::debug("SpeedTestScreen: Download duration: " + std::to_string(duration.count()) + " ms");
            
            // Clean up
            curl_easy_cleanup(curl);
            
            // Consider the test successful if we downloaded more than 100KB
            // and the HTTP response code is 200 OK
            if (res == CURLE_OK && downloadedBytes > 100000 && (httpCode >= 200 && httpCode < 300)) {
                // Calculate speed in Mbps
                m_downloadSpeed = calculateSpeed(downloadedBytes, duration);
                Logger::debug("SpeedTestScreen: Download completed successfully - " + 
                             std::to_string(downloadedBytes) + " bytes in " + 
                             std::to_string(duration.count()) + "ms = " + 
                             std::to_string(m_downloadSpeed) + " Mbps");
                m_testResult = 0; // Success
            } else {
                Logger::error("SpeedTestScreen: Download failed or too small - " + 
                             std::string(curl_easy_strerror(res)) +
                             " (HTTP " + std::to_string(httpCode) + ")");
                m_testResult = 1; // Failure
            }
        } else {
            Logger::error("SpeedTestScreen: Failed to initialize CURL");
            m_testResult = 1;
        }
        
        // Mark test as completed
        m_testCompleted = true;
    });
    
    // Detach thread to run independently
    m_testThread.detach();
}

void SpeedTestScreen::startUploadTest() {
    if (!m_uploadEnabled || m_uploadInProgress || m_downloadInProgress) {
        return;  // Upload not enabled or test already in progress
    }
    
    Logger::debug("SpeedTestScreen: Starting upload test using script: " + m_uploadScript);
    
    // Reset state for upload test
    m_testCompleted = false;
    m_testResult = -1;
    m_progress = 0;
    m_statusChanged = true;
    m_uploadInProgress = true;
    
    // Record start time
    m_startTime = std::chrono::steady_clock::now();
    
    // Start test in separate thread
    m_testThread = std::thread([this]() {
        // Create a temporary file to capture the script output
        std::string tempFile = "/tmp/micropanel_upload_result.txt";
        
        // Execute the upload script
        std::string command = m_uploadScript + " > " + tempFile + " 2>&1";
        int result = system(command.c_str());
        
        if (result == 0) {
            // Try to read the upload speed from the script output
            std::ifstream resultFile(tempFile);
            if (resultFile.is_open()) {
                std::string line;
                if (std::getline(resultFile, line)) {
                    try {
                        m_uploadSpeed = std::stod(line);
                        Logger::debug("SpeedTestScreen: Upload completed - " + 
                                    std::to_string(m_uploadSpeed) + " Mbps");
                        m_testResult = 0;
                    } catch (...) {
                        Logger::error("SpeedTestScreen: Failed to parse upload speed");
                        m_testResult = 1;
                    }
                }
                resultFile.close();
            } else {
                Logger::error("SpeedTestScreen: Failed to read upload result file");
                m_testResult = 1;
            }
        } else {
            Logger::error("SpeedTestScreen: Upload script failed with error code: " + 
                         std::to_string(result));
            m_testResult = 1;
        }
        
        // Clean up the temporary file
        unlink(tempFile.c_str());
        
        // Mark test as completed
        m_testCompleted = true;
    });
    
    // Detach thread to run independently
    m_testThread.detach();
}

double SpeedTestScreen::calculateSpeed(size_t bytes, std::chrono::milliseconds duration) {
    // Convert bytes to bits
    double bits = bytes * 8.0;
    
    // Convert to megabits
    double megabits = bits / 1000000.0;
    
    // Calculate seconds
    double seconds = duration.count() / 1000.0;
    
    // Calculate speed in Mbps
    double speed = megabits / seconds;
    
    return speed;
}

// method to display final results with proper formatting
void SpeedTestScreen::displayFinalResults() {
    // Clear prior test info 
    m_display->drawText(0, 20, "                ");
    m_display->drawText(0, 48, "                ");
    m_display->drawText(0, 60, "                ");
    usleep(Config::DISPLAY_CMD_DELAY);
    
    // Show final results
    if (m_testResult == 0) {
        // Format download speed with limited precision
        std::ostringstream speedStream;
        speedStream << std::fixed << std::setprecision(1);
        
        if (m_downloadSpeed > 0) {
            // Make sure we don't exceed 16 chars
            speedStream.str("");
            if (m_downloadSpeed < 100) {
                // For smaller speeds, show more precision
                speedStream << std::fixed << std::setprecision(1);
            } else {
                // For larger speeds, show as integer
                speedStream << std::fixed << std::setprecision(0);
            }
            speedStream << "Down: " << m_downloadSpeed << " Mbps";
            
            // Ensure it fits on screen (max 16 chars)
            std::string speedStr = speedStream.str();
            if (speedStr.length() > 16) {
                speedStr = speedStr.substr(0, 16);
            }
            
            m_display->drawText(0, 20, speedStr);
            usleep(Config::DISPLAY_CMD_DELAY);
        }
        
        // Format upload speed if available
        if (m_uploadSpeed > 0) {
            speedStream.str("");
            if (m_uploadSpeed < 100) {
                speedStream << std::fixed << std::setprecision(1);
            } else {
                speedStream << std::fixed << std::setprecision(0);
            }
            speedStream << "Up: " << m_uploadSpeed << " Mbps";
            
            // Ensure it fits on screen
            std::string speedStr = speedStream.str();
            if (speedStr.length() > 16) {
                speedStr = speedStr.substr(0, 16);
            }
            
            m_display->drawText(0, 48, speedStr);
            usleep(Config::DISPLAY_CMD_DELAY);
        }
        
        // Show press button message (on line 60, below the progress bar)
        m_display->drawText(0, 60, "Press to exit");
        usleep(Config::DISPLAY_CMD_DELAY);
    }
    else {
        m_display->drawText(0, 20, "Test Failed!");
        usleep(Config::DISPLAY_CMD_DELAY);
        m_display->drawText(0, 60, "Press to exit");
        usleep(Config::DISPLAY_CMD_DELAY);
    }
}
