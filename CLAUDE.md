# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

µPanel (micropanel) is a C++ daemon that provides a USB-based Human-Machine Interface for embedded Linux devices using an RP2040-based USB HID display dongle (VID:1209 PID:0001). The dongle has a SSD1306 128x64 OLED display, rotary encoder with push button, optional directional buttons, and buzzer. It appears as `/dev/ttyACM0` (serial display) and `/dev/input/eventX` (input events) on the host. Raspberry Pi deployments can alternatively use I2C display (`/dev/i2c-1` or `/dev/i2c-3`) and GPIO buttons.

## Build Commands

```bash
# Clean build
rm -rf build && mkdir build && cd build && cmake .. && make -j$(nproc)
# Binary: build/micropanel

# Run with verbose output (no automated tests - requires hardware)
./build/micropanel -v -c screens/config-debian.json
./build/micropanel -a -i gpio -s /dev/i2c-3 -c screens/config-pios.json -v  # Pi hybrid mode

# Install with systemd service
cmake -DINSTALL_SYSTEMD_SERVICE=ON -DINSTALL_SCREEN=config-pios.json -DSYSTEMD_UNITFILE_ARGS="-a -i gpio -s /dev/i2c-3" ..
make && sudo make install
```

**Dependencies:** `cmake`, `libudev-dev`, `libi2c-dev`, `i2c-tools`, `nlohmann-json3-dev`, `libcurl4-openssl-dev`, `iperf3`

**Compiler:** C++14, `-Wall -Wextra -Wpedantic`, `-Os` with binary stripping.

## Architecture

### Startup Flow
1. Parse CLI args → 2. Detect/init display and input devices → 3. Create `Display` wrapper → 4. `initializeModules()` creates all screen modules → 5. `loadConfigFromJson()` registers enabled modules to menu and builds menu hierarchy → 6. Main event loop

### Core Classes
- **MicroPanel** (`src/MicroPanel.cpp`, ~1220 lines): Main orchestrator. Initializes devices, loads JSON config, creates modules, runs event loop. The two key methods are `initializeModules()` (hardcoded module creation) and `loadConfigFromJson()` (JSON-driven registration and menu hierarchy).
- **DeviceManager** (`src/devices/DeviceManager.cpp`): USB device detection via udev. `detectDevicesWithFallback()` implements hybrid detection (USB first, GPIO/I2C fallback).
- **DisplayDevice / I2CDisplayDevice** (`src/devices/`): Display communication. `DisplayDevice` uses serial binary protocol; `I2CDisplayDevice` drives SSD1306 directly over I2C.
- **InputDevice / MultiInputDevice** (`src/devices/`): `InputDevice` reads `/dev/input/eventX` for USB rotary/buttons. `MultiInputDevice` reads GPIO buttons on Pi.
- **Menu / Display** (`src/menu/`): Menu rendering and navigation. Display wrapper manages brightness, inversion, power save.
- **MenuScreenModule** (`src/modules/MenuScreenModule.cpp`): Hierarchical submenu container. Holds a registry of all modules and launches child screens. Implements `ScreenCallback` for child-to-parent communication (e.g., "exit_to_main_menu" action triggers `navigateToMainMenu()`).

### Screen Modules (`src/modules/`)
All inherit from `ScreenModule` (virtual `enter()`, `update()`, `exit()`, `handleInput()`, `getModuleId()`). Key modules: `NetInfoScreen`, `NetSettingsScreen`, `IPPingScreen`, `SpeedTestScreen`, `ThroughputServerScreen/ClientScreen`, `WiFiSettingsScreen`, `BrightnessScreen`, `SystemStatsScreen`, `GenericListScreen`, `TextBoxScreen`.

### JSON Configuration System (`screens/`)
Configs define which modules are enabled, their menu hierarchy, and dependencies. Module types in JSON:
- **`"menu"`**: Submenu container with `"submenus"` array
- **`"GenericList"`**: Configurable list with static `"list_items"` or dynamic `"items_source"` script
- **`"textbox"`**: Runs a script and displays output (supports `"refresh_sec"` for auto-refresh)
- **`"action"`**: One-off action (e.g., invert display)
- **(no type)**: References a module created by `initializeModules()` with matching `"id"`

Multiple instances of GenericListScreen or TextBoxScreen use the `"type"` field with unique `"id"` values. Dependencies are read from the `"depends"` object via `ModuleDependency`.

### Serial Command Protocol
Commands to `/dev/ttyACM0`: `0x01` clear, `0x02 [X][Y][Len][Text...]` draw text (length-prefixed, max 124 bytes), `0x03 [X][Y]` cursor, `0x04 [0/1]` invert, `0x05 [0-255]` brightness, `0x06` progress bar, `0x07 [0/1]` display on/off, `0x08 [0/1][Hz]` buzzer. Protocol is pure binary — no CR/LF terminators. Minimum 10ms between commands, 50ms after clear.

### Display Constraints
128x64 pixels with 6x8 font = 21 chars wide, 8 lines. Menu shows 6 items per page with scroll indicators. All text should be padded to 16 characters to avoid rendering artifacts.

## Code Patterns

### Anti-Flicker Rendering (follow GenericListScreen)
- Use `render*Method(bool fullRedraw)` overloads
- On minimal updates: clear only selection markers with `m_display->drawText(0, yPos, " ")`
- Pad text to 16 chars: `while (text.length() < 16) text += " ";`
- Draw scroll indicators only on `fullRedraw=true`

### Bounded Navigation (no wraparound)
```cpp
if (direction < 0) {
    if (currentIndex > 0) currentIndex--;
} else {
    if (currentIndex < maxIndex) currentIndex++;
}
```

### Smooth Scrolling
```cpp
if (selectedIndex < firstVisibleItem)
    firstVisibleItem = selectedIndex;
else if (selectedIndex >= firstVisibleItem + MAX_VISIBLE_ITEMS)
    firstVisibleItem = selectedIndex - MAX_VISIBLE_ITEMS + 1;
```

### ScreenCallback Pattern (child-to-parent communication)
Child modules call `notifyCallback("exit_to_main_menu", "")` which `MenuScreenModule::onScreenAction()` handles by calling `navigateToMainMenu()`. To add this to a new module: add `setCallback(ScreenCallback*)` and `notifyCallback()` methods, wire up in `MenuScreenModule::executeSubmenuAction()`, and handle the action in `onScreenAction()`. See `GenericListScreen` and `NetInfoScreen` for examples.

### Adding GPIO Support to Screen Modules
1. Add `handleGPIORotation(int direction)` and `handleGPIOButtonPress()` to the class in `include/ScreenModules.h`
2. Implement both in the .cpp file (`handleGPIOButtonPress()` returns `false` to exit)
3. Add to `MicroPanel::simulateRotationForModule()` in `src/MicroPanel.cpp`
4. Add to button press handling in `MicroPanel::runModuleWithGPIOInput()`
5. For PIMPL modules (like NetSettingsScreen), delegate from public methods to Impl class

### Multiple Module Instances (type-based pattern)
To support multiple instances of a module class: add `setId()` / dynamic `getModuleId()` to the class, add type handling in `MicroPanel::loadConfigFromJson()`, and use `"type": "your_type"` with unique `"id"` values in JSON config. See `TextBoxScreen` and `GenericListScreen` for reference.

### Dependency Error Handling (MenuScreenModule)
When a module fails its dependency check, `executeSubmenuAction()` shows an error screen and waits for any key press before returning to the menu. The pattern drains pending input events first (to avoid instant dismissal from the triggering button press), then polls `m_input->waitForEvents()` in a loop. This is the standard approach for "press any key to continue" screens.

### TextBoxScreen Periodic Refresh
Set `"refresh_sec"` in the `"depends"` object (minimum 0.5s). Uses selective line updates - compares old vs new output and only redraws changed lines. Automatic UTF-8 to ASCII conversion for SSD1306 compatibility (e.g., `°` → `*`).

## Command Line Options

```bash
./micropanel [OPTIONS]
  -i DEVICE   Input: /dev/input/eventX or "gpio" for Pi GPIO buttons
  -s DEVICE   Display: /dev/ttyACM0 or /dev/i2c-1 for I2C
  -c FILE     JSON config file (screens/config-*.json)
  -a          Auto-detect USB dongle via VID:PID
  -v          Verbose debug output
  -p          Power save mode (display timeout)
```

Three device modes: **USB** (`-a`), **GPIO** (`-i gpio -s /dev/i2c-X`), **Hybrid** (`-a -i gpio -s /dev/i2c-X`, recommended for Pi — tries USB first, falls back to GPIO/I2C).

## Testing

### Tier 1: Offline Protocol Unit Tests (no hardware)
```bash
cd build && cmake -DBUILD_TESTS=ON .. && make test_protocol
./test_protocol    # 28 tests, runs in <1s
```
Tests protocol byte sequences for all serial commands (current + v2 with length byte), CR/LF absence verification, string safety patterns (substr, strncpy), Config.h constant validation, and display dimension sanity checks.

### Tier 2: Hardware Smoke Test (dongle connected)
```bash
python3 tests/test_hardware_smoke.py              # Auto-detect, full suite (22 tests)
python3 tests/test_hardware_smoke.py --check-only  # Just detect dongle
```
Tests all display commands (clear, draw text, brightness, invert, progress bar, power cycle, rapid writes, max text length) plus input injection tests if test firmware is present (rotate CW/CCW, button press, nav up/down/left/right — verified by reading HID events from `/dev/input/eventX`).

**Requirements:** pyserial (`pip install pyserial`), user in `dialout` group (serial) and `input` group (HID events). Input injection tests require firmware built with `-DENABLE_TEST_COMMANDS=ON`; gracefully skipped on production firmware.

### Test Firmware Commands
The dongle firmware supports a test command (`0xF0`) when built with `-DENABLE_TEST_COMMANDS=ON`. Subcommands: `0x00` ping/echo, `0x01` rotate CW, `0x02` rotate CCW, `0x03` button press, `0x04-0x07` nav up/down/left/right. Ping returns `[0xF0][0x00]` over CDC serial; all others inject HID events. See `hw-auto-test-commands.txt` for full spec.

## Important Constraints

- Root privileges needed for device communication
- Different configs for Debian (`config-debian.json`) vs Raspberry Pi OS (`config-pios.json`)
- Scripts in `scripts/` must be POSIX-compatible (busybox support required, no GNU-specific flags like `grep -oP`)
- Shell scripts for Pi display configuration use template system: `configs/config-base.txt.in` + `configs/display-configs.conf`
