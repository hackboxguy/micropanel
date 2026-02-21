# µPanel Code Improvement Plan

This document tracks identified code improvements, their priority, and implementation status.

---

## Regression Test Framework

Two-tier testing framework to validate changes before and after each improvement.

### Tier 1: Offline Protocol Unit Tests (no hardware)
- **File:** `tests/test_protocol.cpp`
- **Build:** `cd build && cmake -DBUILD_TESTS=ON .. && make test_protocol`
- **Run:** `./build/test_protocol`
- **Tests (28):** Protocol byte sequences for all commands (current + v2 with length byte), CR/LF terminator absence verification, C3 substr safety pattern, C2 strncpy safety pattern, Config.h constant validation, display dimension sanity checks.
- **When to run:** After every code change. Fast, no dependencies.

### Tier 2: Hardware Smoke Test (dongle connected)
- **File:** `tests/test_hardware_smoke.py`
- **Run:** `python3 tests/test_hardware_smoke.py` (auto-detects dongle)
- **Run (check only):** `python3 tests/test_hardware_smoke.py --check-only`
- **Tests (22):**
  - **Display tests (14):** Device detection, serial open, clear display, draw text (origin, multiline, CR/LF coordinates), brightness range, invert toggle, progress bar, power cycle, rapid commands (50 writes), max text length, HID input device presence, final display message.
  - **Firmware detection (1):** Ping test (0xF0 0x00) — detects test firmware vs production.
  - **Input injection (7):** Rotate CW (REL_X=-5), rotate CCW (REL_X=+5), button press (BTN_LEFT press+release), navigate up (REL_Y=-5 ×2), navigate down (REL_Y=+5 ×2), navigate left (REL_X=-5 ×2), navigate right (REL_X=+5 ×2). Requires test firmware (`-DENABLE_TEST_COMMANDS=ON`). Gracefully skipped on production firmware.
- **Requires:** USB HID Display dongle (VID:1209 PID:0001), pyserial (`pip install pyserial`), user in `dialout` and `input` groups.
- **When to run:** After protocol changes (H0a, H0b, H1) and before releases.

---

## Critical (Crash / Undefined Behavior)

### C1. Detached thread use-after-free in PersistentStorage
- **File:** `src/PersistentStorage.cpp:209-217`
- **Issue:** A detached thread captures `this` and sleeps for 2 seconds. If the object is destroyed during that sleep (e.g., shutdown), it dereferences a dangling pointer causing use-after-free.
- **Fix:** Replace detached thread with a joinable thread managed by the class. Add proper shutdown signaling and join in destructor.
- **Status:** [x] Done — replaced detached thread with joinable worker thread using condition variable and proper shutdown signaling.

### C2. Unsafe strcpy() buffer overflow in NetworkInfoScreen
- **File:** `src/modules/NetworkInfoScreen.cpp:167, 207`
- **Issue:** `strcpy(ifr.ifr_name, ifa->ifa_name)` with no bounds check. Interface names exceeding `IFNAMSIZ` (16 bytes) cause stack buffer overflow.
- **Fix:** Replace with `strncpy(ifr.ifr_name, ifa->ifa_name, sizeof(ifr.ifr_name) - 1)` and null-terminate.
- **Status:** [x] Done — replaced strcpy with strncpy + null-termination at both call sites.

### C3. substr() out-of-bounds in ModuleDependency
- **File:** `src/ModuleDependency.cpp:121`
- **Issue:** `path.substr(0, 7)` throws `std::out_of_range` if path is shorter than 7 characters. Uncaught exception crashes the daemon.
- **Fix:** Check `path.length()` before substr, or use `path.compare(0, 7, "http://") == 0`.
- **Status:** [x] Done — replaced substr() with length-checked compare().

---

## High Priority (Protocol / Silent Failures / Resource Leaks)

### H0a. Add length byte to CMD_DRAW_TEXT serial protocol
- **Files:** `src/devices/DisplayDevice.cpp:218-229`, `include/Config.h:32`
- **Context:** Coordinated protocol upgrade with usb-hid-display RP2040 firmware. Firmware changes happen separately — this is the micropanel daemon side only.
- **Issue:** `CMD_DRAW_TEXT` (0x02) currently sends `[0x02][x][y][text...]` with no length byte and no terminator. The firmware uses a fragile 5ms timeout to decide when the text payload ends. USB packet coalescing or fragmentation can split or merge payloads unpredictably, causing garbled display output.
- **Fix:** Change `DisplayDevice::drawText()` to send `[0x02][x][y][len][text...]` where `len` is a single byte. The firmware max buffer is 128 bytes, so max text length is 124 (128 - 4 byte header: cmd + x + y + len). Clamp text to 124 bytes with a log warning if exceeded. Update the command vector from `textLen + 3` to `textLen + 4` and insert the length byte at `cmd[3]`, shifting the text payload to `cmd.data() + 4`. Add a `CMD_DRAW_TEXT_MAX_LEN = 124` constant to `Config.h`.
- **Status:** [x] Done — added length byte to drawText(), added CMD_DRAW_TEXT_MAX_LEN constant. Firmware updated and verified.

### H0b. Remove CR/LF command terminator dependency
- **Files:** `src/devices/DisplayDevice.cpp` (all sendCommand/flushBuffer paths)
- **Context:** Coordinated with H0a. Once all commands have deterministic framing (fixed-length or length-prefixed), the firmware will stop treating `\r` (0x0D) and `\n` (0x0A) as command terminators.
- **Issue:** The firmware currently treats CR/LF as command terminators for backward compatibility. In a binary protocol, these byte values can appear as legitimate coordinate/parameter values (e.g., x=10 is 0x0A, y=13 is 0x0D), causing premature command finalization and display corruption.
- **Fix:** Audit all serial write paths in `DisplayDevice.cpp` to confirm no `\r` or `\n` bytes are appended after commands. Current code analysis shows no CR/LF terminators are sent — `sendCommand()` and `flushBuffer()` write raw binary only. This item is a verification task: confirm no caller appends terminators, add a code comment documenting that the protocol is pure binary with no line terminators, and ensure any future command additions follow this convention.
- **Status:** [x] Done — verified no CR/LF bytes sent anywhere in DisplayDevice. Protocol is pure binary.

### H1. Partial serial writes silently discarded
- **File:** `src/devices/DisplayDevice.cpp:151-154`
- **Issue:** When `write()` sends fewer bytes than the buffer, the remainder is logged as a warning but discarded when the buffer is cleared. This causes display corruption.
- **Fix:** Retry unwritten bytes in a loop, or keep them in the buffer for the next flush cycle.
- **Status:** [x] Done — added write retry loop with EINTR handling in both flushBuffer() and sendCommand().

### H2. popen() without RAII — pipe leaks on early return/exception
- **Files:**
  - `src/modules/TextBoxScreen.cpp:262`
  - `src/devices/DeviceManager.cpp:561, 730`
  - `src/modules/NetSettingsScreen.cpp:163, 285, 322`
  - `src/modules/GenericListScreen.cpp:427`
- **Issue:** If an early return or exception occurs between `popen()` and `pclose()`, the pipe file descriptor leaks.
- **Fix:** Wrap all `popen()` calls with `std::unique_ptr<FILE, decltype(&pclose)> fp(popen(...), &pclose)`.
- **Status:** [x] Done — wrapped all 5 bare popen() calls with unique_ptr RAII.

### H3. Signal handler race condition
- **File:** `src/MicroPanel.cpp:20-31`
- **Issue:** `s_instance` is a raw `MicroPanel*` accessed in the signal handler without synchronization. Concurrent access from signal and main thread is undefined behavior.
- **Fix:** Change to `static std::atomic<MicroPanel*> s_instance{nullptr}` and use `.store()`/`.load()`. Use `m_running.store(false)` instead of direct assignment.
- **Status:** [x] Done — made s_instance atomic, used .load()/.store() in signal handler. Removed non-async-signal-safe Logger call.

### H4. fcntl() error not checked
- **File:** `src/devices/InputDevice.cpp:106-107`
- **Issue:** `fcntl(m_fd, F_GETFL, 0)` return value not checked. If it returns -1, the subsequent `F_SETFL` call gets garbage flags. Device may stay in blocking mode, hanging the application on reads.
- **Fix:** Check `flags >= 0` before using, log error and return false on failure.
- **Status:** [x] Done — added flags < 0 check with error log and early return.

### H5. Fixed 512-byte buffer for shell command construction
- **File:** `src/modules/NetSettingsScreen.cpp:278-280, 317`
- **Issue:** `snprintf` into `char cmd[512]` concatenating script path, interface name, IP addresses, gateway, netmask. Long paths or values silently truncate the command, causing incorrect system configuration.
- **Fix:** Use `std::string` concatenation or `std::ostringstream` instead of fixed buffer.
- **Status:** [x] Done — replaced char[512] + snprintf with std::string concatenation.

---

## Medium Priority (Error Handling / Robustness)

### M1. I2C writes without error checking
- **File:** `src/devices/I2CDisplayDevice.cpp:164-179`
- **Issue:** `writeCommand()` called repeatedly in `clear()` and other methods with no return value checks. A failed write leaves the display in an inconsistent state with no indication of failure.
- **Fix:** Check return value of `writeCommand()` and bail out or retry on failure.
- **Status:** [ ] Not started

### M2. Unchecked netmask null pointer
- **File:** `src/modules/NetSettingsScreen.cpp:310`
- **Issue:** `ifa->ifa_netmask` cast to `struct sockaddr_in*` without null check. Some interfaces may not have a netmask, causing null pointer dereference.
- **Fix:** Add `if (ifa->ifa_netmask)` guard before the cast.
- **Status:** [ ] Not started

### M3. Logger verbose flag not thread-safe
- **File:** `src/Logger.cpp:4`, `include/Logger.h`
- **Issue:** `static bool m_verbose` read from multiple threads (main thread, signal handler, worker threads) without synchronization.
- **Fix:** Change to `static std::atomic<bool> m_verbose`.
- **Status:** [ ] Not started

### M4. Static curl init without synchronization
- **File:** `src/modules/SpeedTestScreen.cpp:51-55`
- **Issue:** `static bool curlInitialized` checked and set without a mutex. Race condition if multiple SpeedTestScreen instances are created concurrently.
- **Fix:** Use `std::call_once` with `std::once_flag` for thread-safe one-time initialization.
- **Status:** [ ] Not started

### M5. EVIOCGRAB failure silently ignored
- **File:** `src/devices/InputDevice.cpp:50-54`
- **Issue:** If exclusive grab fails, execution continues. Other processes can steal input events, leading to missed or duplicated input.
- **Fix:** Either return false to signal the caller, or document why continuing is acceptable with a more prominent log message.
- **Status:** [ ] Not started

### M6. Integer range not validated for I2C cursor
- **File:** `src/devices/I2CDisplayDevice.cpp:197-198`
- **Issue:** `setCursor(int x, int y)` casts to `uint8_t` without validating range. Negative or out-of-range values silently wrap.
- **Fix:** Clamp values to valid display range (0-127 for x, 0-63 for y) before cast.
- **Status:** [ ] Not started

---

## Low Priority (Code Quality / Deduplication)

### L1. Duplicated GPIO handler setup
- **File:** `src/MicroPanel.cpp:406-413, 604-609`
- **Issue:** Identical GPIO handler configuration code repeated in two places.
- **Fix:** Extract to a helper method like `configureGPIOHandler(MenuScreenModule*, const std::string& id)`.
- **Status:** [ ] Not started

### L2. Duplicated device detection methods
- **File:** `src/devices/DeviceManager.cpp`
- **Issue:** `checkDevicePresent()` and `checkDevicePresentSilent()` are nearly identical, differing only in logging.
- **Fix:** Merge into one method with a `bool silent` parameter, or extract shared logic into a private helper.
- **Status:** [ ] Not started

### L3. Repeated menu rendering patterns across modules
- **Files:** `NetInfoScreen.cpp`, `NetSettingsScreen.cpp`, `ThroughputServerScreen.cpp`, `WiFiSettingsScreen.cpp`
- **Issue:** Very similar `renderOptions()` / `renderMenu()` loops with selection markers, text padding, and scroll indicators duplicated across ~200 lines.
- **Fix:** Create a shared rendering utility function or base class with common menu rendering logic.
- **Status:** [ ] Not started

### L4. Inconsistent navigation (wraparound vs bounded)
- **File:** `src/modules/NetSettingsScreen.cpp` (sub-menus)
- **Issue:** Some NetSettingsScreen sub-menus use wraparound navigation while the rest of the codebase uses bounded navigation (stops at first/last item).
- **Fix:** Standardize on bounded navigation to match the GenericListScreen pattern.
- **Status:** [ ] Not started

### L5. Hardcoded temp file paths
- **File:** `src/modules/IPPingScreen.cpp:90, 287`
- **Issue:** `"/tmp/micropanel_ping_result.txt"` hardcoded in two places. Potential file collision in multi-instance scenarios.
- **Fix:** Use a constant, or generate unique temp paths with `mkstemp()` or pid-based naming.
- **Status:** [ ] Not started

### L6. Magic numbers for display layout
- **Files:** Multiple modules
- **Issue:** `16 + (i * 8)`, `16 + (i * 10)`, buffer sizes like `128`, `512`, `64` scattered throughout without named constants.
- **Fix:** Define constants in `Config.h`: `DISPLAY_HEADER_Y`, `DISPLAY_LINE_HEIGHT`, `CMD_BUFFER_SIZE`, etc.
- **Status:** [ ] Not started
