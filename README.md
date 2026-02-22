# µPanel: A Tiny USB based HMI dongle for Embedded Linux Devices

## Overview

**µPanel** (micropanel) is a linux daemon to control a low-cost RP2040 based [**usb-hid-display**](https://github.com/hackboxguy/usb-hid-display) that adds a **tiny Human-Machine Interface (HMI)** to any Linux device.

**usb-hid-display** dongle combines :

- A small **SSD1306 128x64 OLED** display
- A **rotary encoder** with push button
- Optional **Directional push buttons** support for up/down/left/right/enter menu-browse
- A **buzzer** for audible feedback

When plugged into a Linux machine, [**usb-hid-display dongle**](https://github.com/hackboxguy/usb-hid-display) appears as two devices in the Linux space:
- `/dev/ttyACM0`: Serial interface for text/graphic rendering
- `/dev/input/eventX`: Input interface for rotary movements and button presses

This companion linux daemon **micropanel** handles USB detection, menu navigation, and interaction with the Linux system.

µPanel is not just a "fun DIY" project — it solves a missing HMI interface to the headless tiny embedded linux devices using standard USB interface:
- **IT and AV professionals** often struggle to find IP addresses of newly deployed devices in large networks.
- **Network administrators** need portable, fast ways to diagnose Ethernet port issues without heavy gear.
- **Embedded developers** want minimal local displays for headless devices without monitors.

µPanel uniquely addresses these challenges with a minimal, low-cost, open-source solution.

---

## Features

- **Auto-detects** when µPanel is plugged/unplugged
- **Interactive rotary menu** to control Linux functions
- **Configurable navigational menu buttons**
- **Idle timeout** to sleep and wakeup of the  OLED display
- **Dynamic menu system** (configured via JSON file)
- **Network status and configuration**
- **Internet disruption monitoring**

---

## Supported Hardware Modes

micropanel works with two different types of HMI hardware interfaces. The same daemon binary handles both.

**1. USB HMI Dongle** — for PCs, mini-PCs, and devices with only USB connectivity
- Designed for hardware that does not expose GPIO/I2C pins (x86 PCs, NUCs, routers, network appliances)
- Uses an RP2040-based [usb-hid-display](https://github.com/hackboxguy/usb-hid-display) dongle that plugs into any USB port
- Input: USB HID exposed as `/dev/input/eventX`
- Display: USB CDC serial exposed as `/dev/ttyACM*`
- Auto-detected by VID:PID (`1209:0001`)

**2. Direct GPIO + I2C** — for Raspberry Pi and SBCs with exposed pins
- Designed for single-board computers that expose GPIO and I2C pins directly
- SSD1306 OLED and navigation buttons are wired directly to the board's GPIO/I2C
- No USB dongle needed — the display and buttons are part of the device itself
- Input: GPIO-backed directional buttons (via linux input event nodes)
- Display: I2C SSD1306 (`/dev/i2c-1` or `/dev/i2c-3`)

**3. Hybrid** (recommended for Pi)
- For SBCs that have both GPIO/I2C pins and USB ports
- Tries USB dongle first, falls back to GPIO/I2C if not found
- Allows hot-plugging the USB dongle while GPIO/I2C is available

---

## Build Dependencies

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake pkg-config \
  libudev-dev libi2c-dev nlohmann-json3-dev libcurl4-openssl-dev
```

Optional for throughput testing: `iperf3`

---

## Build and Run

```bash
git clone https://github.com/hackboxguy/micropanel.git
cd micropanel
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

Run with your configuration:
```bash
# USB dongle auto-detect (Debian/Ubuntu)
./build/micropanel -a -c screens/config-debian.json

# Direct GPIO + I2C only (Raspberry Pi, no USB dongle)
./build/micropanel -i gpio -s /dev/i2c-3 -c screens/config-pios.json

# Hybrid mode (Pi: tries USB dongle first, falls back to GPIO/I2C)
./build/micropanel -a -i gpio -s /dev/i2c-3 -c screens/config-pios.json

# Explicit USB nodes (debug/manual)
./build/micropanel -i /dev/input/event0 -s /dev/ttyACM0 -c screens/config-debian.json -v
```

### Command Line Options

```
./micropanel [OPTIONS]
  -i DEVICE   Input: /dev/input/eventX or "gpio" for Pi GPIO buttons
  -s DEVICE   Display: /dev/ttyACM0 or /dev/i2c-1 for I2C
  -c FILE     JSON config file (screens/config-*.json)
  -a          Auto-detect USB dongle via VID:PID
  -v          Verbose debug output
  -p          Power save mode (display timeout)
```

### Install as System Service

```bash
cmake -DINSTALL_SYSTEMD_SERVICE=ON \
      -DINSTALL_SCREEN=config-pios.json \
      -DSYSTEMD_UNITFILE_ARGS="-a -i gpio -s /dev/i2c-3" \
      -S . -B build
cmake --build build
sudo cmake --install build
```

---

## Testing

### Tier 1: Offline Protocol Tests (no hardware needed)

```bash
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build
./build/test_protocol    # 28 tests, runs in <1s
```

### Tier 2: Hardware Smoke Tests (USB dongle connected)

```bash
python3 tests/test_hardware_smoke.py --check-only  # Just detect dongle
python3 tests/test_hardware_smoke.py               # Full suite (22 tests)
```

Requires: `pip install pyserial`, user in `dialout` and `input` groups.

---

## Concept Diagram

```
[µPanel RP2040-USB-Dongle-Hardware]
+--------------------------------------------+
| SSD1306 OLED Display (128x64)              |
| Rotary Encoder + Button                    |
| Up/Down/Left/Right/Enter buttons           |
| Buzzer                                     |
| Optional Sensors (Temp/Humidity/Gas)       |
| RP2040 MCU (acting as USB composite device)|
+--------------------------------------------+
             | USB Connection
             v
[Linux System]
+------------------------------------------+
| /dev/ttyACM0 -> Send display commands    |
| /dev/input/eventX -> Receive input events|
+------------------------------------------+
             |
             v
[micropanel Daemon]
+------------------------------------------+
| - Detects and monitors µPanel USB device |
| - Renders menus on OLED                  |
| - Loads dynamic menu from JSON config    |
| - Listens for rotary/button input        |
| - Executes Linux system commands         |
| - Triggers beeps, sleep mode, etc.       |
| - Monitors external data (stocks, sensors)|
+------------------------------------------+
```

---

## JSON Menu Configuration

Menu screens are configured via JSON files in `screens/`. Each module can be enabled/disabled and may declare dependencies:

```json
{
  "modules": [
    { "id": "netinfo", "title": "Network Info", "enabled": false },
    { "id": "ping", "title": "Ping Tool", "enabled": false },
    { "id": "internet", "title": "Internet Test", "enabled": false },
    { "id": "speedtest", "title": "Speed Test", "enabled": false, "depends": { "download_url": "https://cachefly.cachefly.net/50mb.test" } },
    { "id": "wifi", "title": "WiFi Settings", "enabled": false, "depends": { "daemon_script": "/etc/init.d/networking" } }
  ]
}
```

Different configs are provided for Debian (`config-debian.json`) vs Raspberry Pi OS (`config-pios.json`).

---

## micropanel Daemon Behavior

- **Startup**: Detects µPanel device. Loads JSON menu. Renders navigation.
- **Input Event Loop**: Listens to rotary and button events.
- **Action Execution**: Executes associated system commands.
- **Idle Management**: After 60 seconds of inactivity, sends sleep command to OLED; wakes on user input.
- **Auto-Reconnect**: If µPanel is unplugged, micropanel waits and reconnects automatically.

---

## Serial Command Protocol (USB mode: /dev/ttyACM0)

| Command | Purpose               | Format              |
| ------- | --------------------- | ------------------- |
| `0x01`  | Clear display         | `\x01`              |
| `0x02`  | Display text line     | `\x02 [X] [Y] [Len] Text` |
| `0x03`  | Set cursor position   | `\x03 [X] [Y]`      |
| `0x04`  | Invert display ON/OFF | `\x04 [0 or 1]`        |
| `0x05`  | Set brightness        | `\x05 [0-255]`      |
| `0x06`  | Draw progress bar     | `\x06 params`       |
| `0x07`  | Display OFF/ON        | `\x07 [0 or 1]`        |

Example Bash commands for sending:
```bash
# Clear the display
echo -ne "\x01" > /dev/ttyACM0

# Display text on first line (length-prefixed: \x06 = 6 bytes for "Line 1")
echo -ne "\x02\x00\x00\x06Line 1" > /dev/ttyACM0

# Set cursor position
echo -ne "\x03\x00\x20" > /dev/ttyACM0

# Invert off
echo -ne "\x04\x00" > /dev/ttyACM0

# Set brightness maximum
echo -ne "\x05\xFF" > /dev/ttyACM0

# Display off/on
echo -ne "\x07\x00" > /dev/ttyACM0
echo -ne "\x07\x01" > /dev/ttyACM0
```

---

## Troubleshooting

**USB dongle not detected?**
```bash
ls -l /dev/ttyACM*           # Check serial device
ls -l /dev/input/by-id/      # Check input device
```

**Common issues:**
- Missing permissions: add user to `dialout` (serial) and `input` (HID events) groups
- Wrong event node: use `-v` flag to see which device micropanel selects
- I2C bus mismatch on Pi: check `/dev/i2c-1` vs `/dev/i2c-3` (varies by Pi model)
- Missing build deps: `cmake` will report which library is not found

---

## Why µPanel?

- Works on any Linux system with USB host support
- No complex setup or drivers needed
- Extremely lightweight (<300kB daemon)
- Adds local control and feedback to headless devices
- Suitable for OpenWRT routers, Raspberry Pi, BeagleBone, pocket servers, mini-PCs, and 10G link testers
- Solves real-world deployment, debugging, and diagnostics problems

---
