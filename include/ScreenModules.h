#pragma once

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <vector>
#include <time.h>
#include <chrono>
#include <sys/time.h>
#include "IPSelector.h"

// Forward declarations
class Display;
class InputDevice;
//class IPSelector;

/**
 * Base class for all screen modules
 */
class ScreenModule {
public:
    ScreenModule(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input)
        : m_display(display), m_input(input), m_running(false) {}

    virtual ~ScreenModule() = default;

    // Lifecycle methods
    virtual void enter() = 0;
    virtual void update() = 0;
    virtual void exit() = 0;

    // Input handling
    virtual bool handleInput() = 0;

    // Main run loop
    void run();

    // Control functions
    void stop() { m_running = false; }
    bool isRunning() const { return m_running; }

    // Added for module identification
    virtual std::string getModuleId() const = 0;

protected:
    std::shared_ptr<Display> m_display;
    std::shared_ptr<InputDevice> m_input;
    std::atomic<bool> m_running{false};
};

// Forward declaration for MenuScreenModule
class MenuScreenModule;

/**
 * Network information screen
 */
class NetworkInfoScreen : public ScreenModule {
public:
    NetworkInfoScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input);

    void enter() override;
    void update() override;
    void exit() override;
    bool handleInput() override;
    std::string getModuleId() const override { return "network"; }

private:
    void getNetworkInfo(std::string& ip, std::string& mac, std::string& iface);
};

/**
 * System stats screen
 */
class SystemStatsScreen : public ScreenModule {
public:
    SystemStatsScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input);

    void enter() override;
    void update() override;
    void exit() override;
    bool handleInput() override;
    std::string getModuleId() const override { return "system"; }

private:
    void getSystemInfo(std::string& cpu, std::string& memTotal, std::string& memFree,
                       int& cpuPercentage, int& memPercentage);

    struct timeval m_lastUpdate = {0, 0};
};

/**
 * Brightness control screen
 */
class BrightnessScreen : public ScreenModule {
public:
    BrightnessScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input);

    void enter() override;
    void update() override;
    void exit() override;
    bool handleInput() override;
    std::string getModuleId() const override { return "brightness"; }

private:
    void updateBrightnessValue(int brightness);
    void setupScreen();

    int m_previousBrightness = 0;
};

/**
 * Internet connectivity test screen
 */
class InternetTestScreen : public ScreenModule {
public:
    InternetTestScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input);

    void enter() override;
    void update() override;
    void exit() override;
    bool handleInput() override;
    std::string getModuleId() const override { return "internet"; }

private:
    static int pingServer(const std::string& server, int timeoutSec);
    void startTest();

    std::thread m_testThread;
    std::atomic<bool> m_testCompleted{false};
    std::atomic<int> m_testResult{-1};
    std::atomic<int> m_progress{0};
    std::chrono::steady_clock::time_point m_startTime;
    int64_t m_progressLastUpdated = 0;
    int64_t m_animationLastUpdated = 0;
    bool m_resultDisplayed = false;
    const int m_timeoutSec = 5;
    const std::string m_testServer = "8.8.8.8";
};

/**
 * WiFi settings screen
 */
class WiFiSettingsScreen : public ScreenModule {
public:
    WiFiSettingsScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input);

    void enter() override;
    void update() override;
    void exit() override;
    bool handleInput() override;
    std::string getModuleId() const override { return "wifi"; }

private:
    void setWiFiStatus(bool enabled);
    bool getWiFiStatus() const;
    void renderOptions();

    std::vector<std::string> m_options = {"Turn On", "Turn Off", "Back"};
    int m_selectedOption = 0;
    mutable bool m_currentWiFiState = false;
};

// Simple example screens for demo menu items

/**
 * Hello World screen
 */
class HelloWorldScreen : public ScreenModule {
public:
    HelloWorldScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input);

    void enter() override;
    void update() override;
    void exit() override;
    bool handleInput() override;
    std::string getModuleId() const override { return "hello"; }

private:
    time_t m_startTime = 0;
    const int m_displayTime = 2; // seconds
};

/**
 * Counter screen
 */
class CounterScreen : public ScreenModule {
public:
    CounterScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input);

    void enter() override;
    void update() override;
    void exit() override;
    bool handleInput() override;
    std::string getModuleId() const override { return "counter"; }

private:
    static int s_counter;
    time_t m_startTime = 0;
    const int m_displayTime = 2; // seconds
};

// Menu states for IP Ping screen
enum class IPPingMenuState {
    MENU_STATE_IP,       // IP address edit
    MENU_STATE_PING,     // PING action
    MENU_STATE_EXIT      // Exit menu
};

/**
 * IP Ping Test screen
 */
class IPPingScreen : public ScreenModule {
public:
    IPPingScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input);

    void enter() override;
    void update() override;
    void exit() override;
    bool handleInput() override;
    std::string getModuleId() const override { return "ping"; }

    const std::string& getSelectedIp() const;

private:
    void showIpSelector();
    void startPing();
    void renderMenu(bool fullRedraw);
    void updateStatusLine();
    void checkPingStatus();

    std::string m_targetIp;
    std::unique_ptr<IPSelector> m_ipSelector;
    std::atomic<pid_t> m_pingPid{-1};
    std::atomic<bool> m_pingInProgress{false};
    std::atomic<int> m_pingResult{-1};
    std::string m_statusMessage;
    std::string m_lastStatusText;
    bool m_statusChanged = false;
    double m_pingTimeMs = 0.0;

    IPPingMenuState m_state{IPPingMenuState::MENU_STATE_IP};
    bool m_shouldExit{false};
};

/**
 * Network interfaces screen
 * Shows a list of all network interfaces and details
 */
class NetInfoScreen : public ScreenModule {
public:
    NetInfoScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input);
    ~NetInfoScreen(); // Add destructor

    void enter() override;
    void update() override;
    void exit() override;
    bool handleInput() override;
    std::string getModuleId() const override { return "netinfo"; }

private:
    // Internal implementation
    class Impl;  // Use PIMPL idiom
    std::unique_ptr<Impl> m_pImpl;
};

/**
 * Network Settings screen
 * Allows configuring static/DHCP IP settings
 */
class NetSettingsScreen : public ScreenModule {
public:
    NetSettingsScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input);
    ~NetSettingsScreen();

    void enter() override;
    void update() override;
    void exit() override;
    bool handleInput() override;
    std::string getModuleId() const override { return "netsettings"; }

private:
    // Internal implementation
    class Impl;
    std::unique_ptr<Impl> m_pImpl;
};

/**
 * Network Speed Test screen
 * Performs download speed test and optionally upload speed test if configured
 */
class SpeedTestScreen : public ScreenModule {
public:
    SpeedTestScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input);

    void enter() override;
    void update() override;
    void exit() override;
    bool handleInput() override;
    std::string getModuleId() const override { return "speedtest"; }

private:
    static double calculateSpeed(size_t bytes, std::chrono::milliseconds duration);
    void startDownloadTest();
    void startUploadTest();
    void renderScreen();
    void updateStatusLine();
    bool checkConfiguration();
    void displayFinalResults();

    // Configuration
    std::string m_downloadUrl;
    std::string m_uploadScript;
    bool m_uploadEnabled;

    // Test state
    std::thread m_testThread;
    std::atomic<bool> m_downloadInProgress{false};
    std::atomic<bool> m_uploadInProgress{false};
    std::atomic<bool> m_testCompleted{false};
    std::atomic<int> m_progress{0};
    std::atomic<double> m_downloadSpeed{0.0};
    std::atomic<double> m_uploadSpeed{0.0};
    std::atomic<int> m_testResult{-1};  // -1: not started, 0: success, 1: failure
    std::chrono::steady_clock::time_point m_startTime;
    int64_t m_progressLastUpdated = 0;
    int64_t m_animationLastUpdated = 0;
    std::string m_statusMessage;
    std::string m_lastStatusText;
    bool m_statusChanged = false;
    bool m_shouldExit = false;
};

/**
 * Throughput Test Server Screen
 * Runs an iperf3 server for network throughput testing
 */
class ThroughputServerScreen : public ScreenModule {
public:
    ThroughputServerScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input);
    ~ThroughputServerScreen();

    void enter() override;
    void update() override;
    void exit() override;
    bool handleInput() override;
    std::string getModuleId() const override { return "throughputserver"; }

private:
    void renderOptions();
    void startServer();
    void stopServer();
    bool isServerRunning() const;
    std::string getIperf3Path();
    void getLocalIpAddress();
    void refreshSettings();
    inline bool isAvahiAvailable() const {
        return (system("which avahi-publish > /dev/null 2>&1") == 0);
    };
    std::vector<std::string> m_options = {"Start", "Stop", "Back"};
    int m_selectedOption = 0;
    int m_port = 5201;             // Default port
    std::string m_localIp;         // Local IP address
    pid_t m_serverPid = -1;        // PID of the iperf3 server process
    std::thread m_serverThread;    // Thread for server operation
    pid_t m_avahiPid = -1;  // PID for the Avahi announcement process
};
// Add these enum declarations:

// Enum for ThroughputClientScreen menu states
enum class ThroughputClientState {
    MENU_STATE_START,
    MENU_STATE_START_REVERSE,
    MENU_STATE_PROTOCOL,
    MENU_STATE_DURATION,
    MENU_STATE_BANDWIDTH,
    MENU_STATE_PARALLEL,
    MENU_STATE_SERVER_IP,
    MENU_STATE_BACK,
    MENU_STATE_TESTING, 
    MENU_STATE_RESULTS,
    // Submenu states
    SUBMENU_STATE_PROTOCOL,
    SUBMENU_STATE_DURATION,
    SUBMENU_STATE_BANDWIDTH,
    SUBMENU_STATE_PARALLEL,
    SUBMENU_STATE_SERVER_IP,
    SUBMENU_STATE_AUTO_DISCOVER
};
struct UDPTestResult {
    double bandwidth_mbps = 0.0;
    double jitter_ms = 0.0;
    int lost_packets = 0;
    double lost_percent = 0.0;
    int total_packets = 0;
    bool valid = false;  // To indicate whether parsing was successful
};

// Add the function declaration in header file
UDPTestResult parseUDPTestResults(const std::string& output);
// Forward declare the IPSelector class if not already included
class IPSelector;

// ThroughputClientScreen class declaration
class ThroughputClientScreen : public ScreenModule {
public:
    ThroughputClientScreen(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input);
    virtual ~ThroughputClientScreen();

    void enter() override;
    void update() override;
    void exit() override;
    bool handleInput() override;
    std::string getModuleId() const override { return "throughputclient"; }
private:
    // Menu state and rendering
    ThroughputClientState m_state;
    int m_submenuSelection;
    bool m_editingIp;
    bool m_shouldExit;
    std::string m_statusMessage;
    bool m_statusChanged;
    bool m_testCancellationPrompt = false;
    bool m_reverseMode = false;

    // Test parameters
    std::string m_serverIp;
    int m_serverPort;
    std::string m_protocol;
    int m_duration;
    int m_bandwidth;
    int m_parallel;

    // Test execution
    bool m_testInProgress;
    pid_t m_testPid;
    int m_testResult;
    std::string m_testOutput;
    double m_bandwidth_result = 0.0;
    double m_jitter_result = 0.0;
    double m_loss_result = 0.0;
    int m_retransmits_result = 0;
    bool m_waitingForButtonPress = false;

    // Auto-discovery
    bool m_discoveryInProgress;
    pid_t m_discoveryPid;
    std::vector<std::pair<std::string, int>> m_discoveredServers;  // IP and port pairs
    std::vector<std::string> m_discoveredServerNames;              // Service names

    // UI Components
    std::unique_ptr<IPSelector> m_ipSelector;

    // Menu options
    std::vector<std::string> m_protocolOptions;
    std::vector<int> m_durationOptions;
    std::vector<int> m_bandwidthOptions;
    std::vector<int> m_parallelOptions;
    std::chrono::steady_clock::time_point m_resultsShownTime;

    // Rendering methods
    void renderMainMenu(bool fullRedraw = false);
    void renderProtocolSubmenu(bool fullRedraw = false);
    void renderDurationSubmenu(bool fullRedraw = false);
    void renderBandwidthSubmenu(bool fullRedraw = false);
    void renderParallelSubmenu(bool fullRedraw = false);
    void renderServerIPSubmenu(bool fullRedraw = false);
    void renderAutoDiscoverScreen(bool fullRedraw = false);
    void updateStatusLine();
    void renderResultsScreen();
    void renderTestingScreen();
    void showResultsAndWait();//int durationMs);

    // Action methods
    void startTest();
    void checkTestStatus();
    void startDiscovery();
    void checkDiscoveryStatus();
    void parseDiscoveryResults();
    void parseTestResults();
    void selectServer(int index);

    // Helper methods
    std::string getIperf3Path() const;
    bool isIperf3Available() const;
    bool isAvahiAvailable() const;
    void refreshSettings();
    std::string getBandwidthString(int value) const;
    std::string formatBandwidth(double value) const;
    std::string normalizeIp(const std::string& ip);
    UDPTestResult parseUDPTestResults(const std::string& output);
    void showResultsScreen();
};
