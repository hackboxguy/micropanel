cmake_minimum_required(VERSION 3.10)
project(MicroPanel VERSION 2.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Find required packages
find_package(Threads REQUIRED)
find_library(UDEV_LIBRARY udev REQUIRED)

# Find libcurl for SpeedTestScreen
find_package(CURL QUIET)
if(NOT CURL_FOUND)
    message(STATUS "libcurl not found!")
    message(FATAL_ERROR "Error!!! Missing libcurl library, try sudo apt-get install libcurl4-openssl-dev")
endif()
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CURL_INCLUDE_DIRS}
)

# Include directories
include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(nlohmann_json 3.7.3 QUIET)
if(nlohmann_json_FOUND)
    message(STATUS "Using system-installed nlohmann/json")
else()
#message(STATUS "nlohmann/json not found in system, using bundled version")
    # Option 2: Include the single header file
    # This assumes you've placed json.hpp in a "vendor/nlohmann" folder in your project
    #nclude_directories(${CMAKE_CURRENT_SOURCE_DIR}/vendor)
    message(STATUS "nlohmann/json not found!!")
    message( FATAL_ERROR "Error!!! Missing nlohmann json library, try sudo apt-get install nlohmann-json3-dev" )
endif()

set(SOURCES_PERSISTENCE
    src/PersistentStorage.cpp
    src/ModuleDependency.cpp
)

# Define source files by directory
set(SOURCES_DEVICES
    src/devices/DisplayDevice.cpp
    src/devices/InputDevice.cpp
    src/devices/DeviceManager.cpp
)

set(SOURCES_MENU
    src/menu/Display.cpp
    src/menu/Menu.cpp
)

set(SOURCES_MODULES
    src/modules/ScreenModule.cpp
    src/modules/SystemStatsScreen.cpp
    src/modules/NetworkInfoScreen.cpp
    src/modules/NetSettingsScreen.cpp
    src/modules/NetInfoScreen.cpp
    src/modules/BrightnessScreen.cpp
    src/modules/InternetTestScreen.cpp
    src/modules/WiFiSettingsScreen.cpp
    src/modules/HelloCounterScreens.cpp
    src/modules/IPSelector.cpp
    src/modules/IPSelectorScreen.cpp
    src/modules/IPPingScreen.cpp
    src/modules/MenuScreenModule.cpp
    src/modules/SpeedTestScreen.cpp
    src/modules/ThroughputServerScreen.cpp
    src/modules/ThroughputClientScreen.cpp
    src/modules/GenericListScreen.cpp
)

set(SOURCES_MAIN
    src/Logger.cpp
    src/MicroPanel.cpp
)

# Combine all sources
set(SOURCES
    ${SOURCES_DEVICES}
    ${SOURCES_MENU}
    ${SOURCES_MODULES}
    ${SOURCES_PERSISTENCE}
    ${SOURCES_MAIN}
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Os -ffunction-sections -fdata-sections)
    # Link-time optimization can significantly reduce size
    add_link_options(-Wl,--gc-sections)
endif()

# Define executable
add_executable(micropanel ${SOURCES})

# Link libraries
target_link_libraries(micropanel
    PRIVATE
    Threads::Threads
    ${UDEV_LIBRARY}
    ${CURL_LIBRARIES}
)

# Install target
install(TARGETS micropanel
    RUNTIME DESTINATION bin
)

# Strip the binary to reduce size
add_custom_command(TARGET micropanel POST_BUILD
    COMMAND ${CMAKE_STRIP} micropanel
    COMMENT "Stripping binary for reduced size"
)

# Create a systemd service file
#configure_file(
#    ${CMAKE_CURRENT_SOURCE_DIR}/micropanel.service.in
#    ${CMAKE_CURRENT_BINARY_DIR}/micropanel.service
#    @ONLY
#)

# Install systemd service if requested
#option(INSTALL_SYSTEMD_SERVICE "Install systemd service file" OFF)
#if(INSTALL_SYSTEMD_SERVICE)
#    install(FILES
#        ${CMAKE_CURRENT_BINARY_DIR}/micropanel.service
#        DESTINATION /etc/systemd/system
#    )
#endif()

# Add uninstall target
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    @ONLY
)
add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
)
