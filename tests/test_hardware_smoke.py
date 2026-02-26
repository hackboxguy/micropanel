#!/usr/bin/env python3
"""
Tier 2: Hardware smoke test for micropanel USB HID Display dongle.

Requires:
  - USB HID Display dongle connected (VID:1209 PID:0001)
  - pyserial: pip install pyserial
  - User in 'dialout' group for /dev/ttyACM* access
  - User in 'input' group for /dev/input/event* access (input injection tests)

Usage:
  python3 tests/test_hardware_smoke.py                    # Auto-detect device
  python3 tests/test_hardware_smoke.py /dev/ttyACM0       # Specify device
  python3 tests/test_hardware_smoke.py --check-only       # Just detect, don't send commands
"""

import sys
import os
import time
import glob
import struct
import select

# Protocol constants (must match include/Config.h)
CMD_CLEAR        = 0x01
CMD_DRAW_TEXT    = 0x02
CMD_SET_CURSOR   = 0x03
CMD_INVERT       = 0x04
CMD_BRIGHTNESS   = 0x05
CMD_PROGRESS_BAR = 0x06
CMD_POWER_MODE   = 0x07

# Test command (firmware built with -DENABLE_TEST_COMMANDS=ON)
CMD_TEST         = 0xF0
TEST_PING        = 0x00
TEST_ROTATE_CW   = 0x01
TEST_ROTATE_CCW  = 0x02
TEST_BUTTON      = 0x03
TEST_NAV_UP      = 0x04
TEST_NAV_DOWN    = 0x05
TEST_NAV_LEFT    = 0x06
TEST_NAV_RIGHT   = 0x07

# Linux input event constants
EV_KEY = 0x01
EV_REL = 0x02
EV_MSC = 0x04
REL_X  = 0x00
REL_Y  = 0x01
BTN_LEFT = 0x110

# Firmware limits
MAX_CMD_SIZE = 128
CMD_DRAW_TEXT_MAX_LEN = 124  # max text bytes (128 - 4 byte header: cmd+x+y+len)
INTER_CMD_DELAY = 0.015  # 15ms between commands (slightly above 10ms spec)
CLEAR_DELAY = 0.060      # 60ms after clear (slightly above 50ms spec)

# Input injection timing (from hw-auto-test-commands-assessment.txt)
TEST_CMD_NAV_DELAY = 0.065     # 65ms between nav test commands (>60ms per assessment)
TEST_CMD_BUTTON_DELAY = 0.085  # 85ms after button press test command (>80ms per assessment)
HID_EVENT_TIMEOUT = 0.200      # 200ms max wait for HID events

# input_event struct size (64-bit Linux: 8+8+2+2+4 = 24 bytes)
INPUT_EVENT_SIZE = 24

# Test results
passed = 0
failed = 0
skipped = 0
failures = []


def log_pass(name):
    global passed
    passed += 1
    print(f"  PASS  {name}")


def log_fail(name, reason):
    global failed
    failed += 1
    failures.append((name, reason))
    print(f"  FAIL  {name}: {reason}")


def log_skip(name, reason):
    global skipped
    skipped += 1
    print(f"  SKIP  {name}: {reason}")


def make_draw_text_cmd(x, y, text_bytes):
    """Build a CMD_DRAW_TEXT command with length byte: [0x02][x][y][len][text...]"""
    if isinstance(text_bytes, str):
        text_bytes = text_bytes.encode()
    text_len = min(len(text_bytes), CMD_DRAW_TEXT_MAX_LEN)
    return bytes([CMD_DRAW_TEXT, x, y, text_len]) + text_bytes[:text_len]


def find_dongle():
    """Auto-detect micropanel dongle by VID:PID via sysfs."""
    for tty in sorted(glob.glob("/dev/ttyACM*")):
        tty_name = os.path.basename(tty)
        sysfs_path = f"/sys/class/tty/{tty_name}/device"
        if not os.path.islink(sysfs_path):
            continue

        real_path = os.path.realpath(sysfs_path)
        check_path = real_path
        for _ in range(10):
            vid_file = os.path.join(check_path, "idVendor")
            pid_file = os.path.join(check_path, "idProduct")
            if os.path.exists(vid_file) and os.path.exists(pid_file):
                with open(vid_file) as f:
                    vid = f.read().strip()
                with open(pid_file) as f:
                    pid = f.read().strip()
                if vid == "1209" and pid == "0001":
                    product = ""
                    bcd = ""
                    prod_file = os.path.join(check_path, "product")
                    bcd_file = os.path.join(check_path, "bcdDevice")
                    if os.path.exists(prod_file):
                        with open(prod_file) as f:
                            product = f.read().strip()
                    if os.path.exists(bcd_file):
                        with open(bcd_file) as f:
                            bcd = f.read().strip()
                    return tty, product, bcd
            check_path = os.path.dirname(check_path)
            if check_path == "/":
                break

    return None, None, None


def find_hid_input():
    """Find the HID input event device for the dongle."""
    try:
        with open("/proc/bus/input/devices") as f:
            content = f.read()
        for block in content.split("\n\n"):
            if "1209" in block and "0001" in block:
                for line in block.split("\n"):
                    if line.startswith("H: Handlers="):
                        handlers = line.split("=", 1)[1].split()
                        for h in handlers:
                            if h.startswith("event"):
                                return f"/dev/input/{h}"
    except (IOError, OSError):
        pass
    return None


def open_serial(device_path):
    """Open serial port with correct settings for the dongle."""
    import serial
    try:
        ser = serial.Serial(
            port=device_path,
            baudrate=115200,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=1,
            write_timeout=2
        )
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        return ser
    except serial.SerialException as e:
        return None


def send_cmd(ser, data, name="cmd"):
    """Send a binary command and check for write errors."""
    try:
        written = ser.write(data)
        ser.flush()
        if written != len(data):
            return False, f"partial write: {written}/{len(data)} bytes"
        return True, ""
    except Exception as e:
        return False, str(e)


def open_hid_input(event_dev):
    """Open HID input device for reading events. Returns fd or -1."""
    try:
        fd = os.open(event_dev, os.O_RDONLY | os.O_NONBLOCK)
        return fd
    except (OSError, PermissionError):
        return -1


def drain_hid_events(fd):
    """Read and discard any pending HID events."""
    while True:
        r, _, _ = select.select([fd], [], [], 0.01)
        if not r:
            break
        try:
            os.read(fd, 4096)
        except OSError:
            break


def read_hid_events(fd, wait_sec=0.15):
    """Read HID events after a wait period. Returns list of (type, code, value)."""
    time.sleep(wait_sec)
    events = []
    while True:
        r, _, _ = select.select([fd], [], [], 0.05)
        if not r:
            break
        try:
            data = os.read(fd, 4096)
        except OSError:
            break
        for i in range(0, len(data), INPUT_EVENT_SIZE):
            if i + INPUT_EVENT_SIZE <= len(data):
                tv_sec, tv_usec, typ, code, value = struct.unpack(
                    'llHHi', data[i:i + INPUT_EVENT_SIZE])
                if typ != 0:  # Skip EV_SYN
                    events.append((typ, code, value))
    return events


def check_test_firmware(ser):
    """Send ping to detect test firmware. Returns True if test commands supported."""
    ser.reset_input_buffer()
    time.sleep(0.05)
    ser.write(bytes([CMD_TEST, TEST_PING]))
    ser.flush()
    time.sleep(0.1)
    reply = ser.read(2)
    return reply == bytes([CMD_TEST, TEST_PING])


# ============================================================================
# Display command tests (T0-T13, same as before)
# ============================================================================

def test_device_detection(device_path):
    """T0: Verify dongle is detected and accessible."""
    if device_path is None:
        log_fail("device_detection", "No micropanel dongle found (VID:1209 PID:0001)")
        return False
    if not os.path.exists(device_path):
        log_fail("device_detection", f"{device_path} does not exist")
        return False
    if not os.access(device_path, os.R_OK | os.W_OK):
        log_fail("device_detection", f"{device_path} not accessible (add user to dialout group)")
        return False
    log_pass("device_detection")
    return True


def test_serial_open(ser):
    """T1: Verify serial port opens with correct parameters."""
    if ser is None:
        log_fail("serial_open", "Failed to open serial port")
        return False
    log_pass("serial_open")
    return True


def test_clear_display(ser):
    """T2: Send clear command."""
    ok, err = send_cmd(ser, bytes([CMD_CLEAR]), "clear")
    if ok:
        log_pass("clear_display")
        time.sleep(CLEAR_DELAY)
    else:
        log_fail("clear_display", err)
    return ok


def test_draw_text_origin(ser):
    """T3: Draw text at (0,0)."""
    text = b"Smoke Test"
    cmd = make_draw_text_cmd(0, 0, text)
    ok, err = send_cmd(ser, cmd, "draw_text_origin")
    if ok:
        log_pass("draw_text_origin")
        time.sleep(INTER_CMD_DELAY)
    else:
        log_fail("draw_text_origin", err)
    return ok


def test_draw_text_multiline(ser):
    """T4: Draw text on multiple display lines."""
    lines = [
        (0, 0,  "Line 0 (y=0)"),
        (0, 8,  "Line 1 (y=8)"),
        (0, 16, "Line 2 (y=16)"),
        (0, 24, "Line 3 (y=24)"),
        (0, 32, "Line 4 (y=32)"),
        (0, 40, "Line 5 (y=40)"),
        (0, 48, "Line 6 (y=48)"),
        (0, 56, "Line 7 (y=56)"),
    ]
    all_ok = True
    for x, y, text in lines:
        cmd = make_draw_text_cmd(x, y, text)
        ok, err = send_cmd(ser, cmd, f"draw_text_y{y}")
        if not ok:
            all_ok = False
            break
        time.sleep(INTER_CMD_DELAY)

    if all_ok:
        log_pass("draw_text_multiline")
    else:
        log_fail("draw_text_multiline", err)
    return all_ok


def test_draw_text_crlf_coords(ser):
    """T5: Draw text at coordinates that are CR/LF byte values (H0b validation)."""
    ser.write(bytes([CMD_CLEAR]))
    time.sleep(CLEAR_DELAY)

    text = b"CR/LF coord"
    cmd = make_draw_text_cmd(10, 13, text)
    ok, err = send_cmd(ser, cmd, "draw_text_crlf_coords")
    if ok:
        log_pass("draw_text_crlf_coords")
        time.sleep(INTER_CMD_DELAY)
    else:
        log_fail("draw_text_crlf_coords", err)
    return ok


def test_brightness_range(ser):
    """T6: Cycle brightness through min, mid, max."""
    all_ok = True
    for brightness in [0, 128, 255]:
        cmd = bytes([CMD_BRIGHTNESS, brightness])
        ok, err = send_cmd(ser, cmd, f"brightness_{brightness}")
        if not ok:
            all_ok = False
            break
        time.sleep(INTER_CMD_DELAY)

    send_cmd(ser, bytes([CMD_BRIGHTNESS, 200]), "brightness_restore")
    time.sleep(INTER_CMD_DELAY)

    if all_ok:
        log_pass("brightness_range")
    else:
        log_fail("brightness_range", err)
    return all_ok


def test_invert_toggle(ser):
    """T7: Toggle display inversion on and off."""
    ok1, err1 = send_cmd(ser, bytes([CMD_INVERT, 1]), "invert_on")
    time.sleep(0.3)
    ok2, err2 = send_cmd(ser, bytes([CMD_INVERT, 0]), "invert_off")
    time.sleep(INTER_CMD_DELAY)

    if ok1 and ok2:
        log_pass("invert_toggle")
    else:
        log_fail("invert_toggle", err1 or err2)
    return ok1 and ok2


def test_progress_bar(ser):
    """T8: Draw progress bar at 0%, 50%, 100%."""
    ser.write(bytes([CMD_CLEAR]))
    time.sleep(CLEAR_DELAY)

    all_ok = True
    for pct in [0, 50, 100]:
        cmd = bytes([CMD_PROGRESS_BAR, 10, 30, 108, 12, pct])
        ok, err = send_cmd(ser, cmd, f"progress_{pct}")
        if not ok:
            all_ok = False
            break
        time.sleep(0.2)

    if all_ok:
        log_pass("progress_bar")
    else:
        log_fail("progress_bar", err)
    return all_ok


def test_power_cycle(ser):
    """T9: Power off then on."""
    ok1, err1 = send_cmd(ser, bytes([CMD_POWER_MODE, 0]), "power_off")
    time.sleep(0.5)
    ok2, err2 = send_cmd(ser, bytes([CMD_POWER_MODE, 1]), "power_on")
    time.sleep(INTER_CMD_DELAY)

    if ok1 and ok2:
        log_pass("power_cycle")
    else:
        log_fail("power_cycle", err1 or err2)
    return ok1 and ok2


def test_rapid_commands(ser):
    """T10: Send 50 rapid commands to stress-test buffering."""
    ser.write(bytes([CMD_CLEAR]))
    time.sleep(CLEAR_DELAY)

    all_ok = True
    for i in range(50):
        text = f"Rapid #{i:03d}".encode()
        cmd = make_draw_text_cmd(0, 0, text)
        ok, err = send_cmd(ser, cmd, f"rapid_{i}")
        if not ok:
            all_ok = False
            break
        time.sleep(INTER_CMD_DELAY)

    if all_ok:
        log_pass("rapid_commands (50 writes)")
    else:
        log_fail("rapid_commands", f"failed at iteration: {err}")
    return all_ok


def test_max_text_length(ser):
    """T11: Send maximum-length text command (close to 128-byte firmware buffer)."""
    ser.write(bytes([CMD_CLEAR]))
    time.sleep(CLEAR_DELAY)

    max_text = b"A" * 120
    cmd = make_draw_text_cmd(0, 0, max_text)
    ok, err = send_cmd(ser, cmd, "max_text_length")
    time.sleep(INTER_CMD_DELAY)

    if ok:
        log_pass(f"max_text_length ({len(cmd)} bytes)")
    else:
        log_fail("max_text_length", err)
    return ok


def test_hid_input_device():
    """T12: Check that HID input event device exists."""
    event_dev = find_hid_input()
    if event_dev and os.path.exists(event_dev):
        log_pass(f"hid_input_device ({event_dev})")
        return True
    else:
        log_skip("hid_input_device", "HID input device not found")
        return True


# ============================================================================
# Input injection tests (T14-T21, require test firmware)
# ============================================================================

def test_ping(ser):
    """T14: Ping test firmware to detect test command support."""
    if check_test_firmware(ser):
        log_pass("test_firmware_ping")
        return True
    else:
        log_skip("test_firmware_ping", "Production firmware (no test commands)")
        return False


def test_inject_rotate_cw(ser, fd):
    """T15: Inject clockwise rotation, verify HID event."""
    drain_hid_events(fd)
    time.sleep(TEST_CMD_NAV_DELAY)
    ser.write(bytes([CMD_TEST, TEST_ROTATE_CW]))
    ser.flush()
    events = read_hid_events(fd)

    # Expect: one EV_REL REL_X=-5
    rel_x_events = [(t, c, v) for t, c, v in events if t == EV_REL and c == REL_X]
    if len(rel_x_events) >= 1 and rel_x_events[0][2] == -5:
        log_pass("inject_rotate_cw (REL_X=-5)")
        return True
    else:
        log_fail("inject_rotate_cw",
                 f"expected REL_X=-5, got {rel_x_events if rel_x_events else 'no REL_X events'}")
        return False


def test_inject_rotate_ccw(ser, fd):
    """T16: Inject counter-clockwise rotation, verify HID event."""
    drain_hid_events(fd)
    time.sleep(TEST_CMD_NAV_DELAY)
    ser.write(bytes([CMD_TEST, TEST_ROTATE_CCW]))
    ser.flush()
    events = read_hid_events(fd)

    rel_x_events = [(t, c, v) for t, c, v in events if t == EV_REL and c == REL_X]
    if len(rel_x_events) >= 1 and rel_x_events[0][2] == 5:
        log_pass("inject_rotate_ccw (REL_X=+5)")
        return True
    else:
        log_fail("inject_rotate_ccw",
                 f"expected REL_X=+5, got {rel_x_events if rel_x_events else 'no REL_X events'}")
        return False


def test_inject_button_press(ser, fd):
    """T17: Inject button press, verify press+release HID events."""
    drain_hid_events(fd)
    time.sleep(TEST_CMD_BUTTON_DELAY)
    ser.write(bytes([CMD_TEST, TEST_BUTTON]))
    ser.flush()
    events = read_hid_events(fd)

    # Expect: BTN_LEFT=1 (press) then BTN_LEFT=0 (release)
    btn_events = [(t, c, v) for t, c, v in events if t == EV_KEY and c == BTN_LEFT]
    if len(btn_events) >= 2 and btn_events[0][2] == 1 and btn_events[1][2] == 0:
        log_pass("inject_button_press (BTN_LEFT press+release)")
        return True
    else:
        log_fail("inject_button_press",
                 f"expected BTN_LEFT 1,0 got {[(v) for _, _, v in btn_events] if btn_events else 'no BTN events'}")
        return False


def test_inject_nav(ser, fd, subcmd, name, expect_axis, expect_value):
    """Generic nav injection test. Expects two HID events with same axis/value."""
    drain_hid_events(fd)
    time.sleep(TEST_CMD_NAV_DELAY)
    ser.write(bytes([CMD_TEST, subcmd]))
    ser.flush()
    events = read_hid_events(fd)

    axis_events = [(t, c, v) for t, c, v in events if t == EV_REL and c == expect_axis]
    axis_name = "REL_X" if expect_axis == REL_X else "REL_Y"
    sign = "+" if expect_value > 0 else ""

    if len(axis_events) >= 2 and axis_events[0][2] == expect_value and axis_events[1][2] == expect_value:
        log_pass(f"inject_nav_{name} ({axis_name}={sign}{expect_value} x2)")
        return True
    else:
        got = [(v) for _, _, v in axis_events] if axis_events else "no events"
        log_fail(f"inject_nav_{name}",
                 f"expected {axis_name}={sign}{expect_value} x2, got {got}")
        return False


def test_inject_nav_up(ser, fd):
    """T18: Inject nav up, verify two REL_Y=-5 events."""
    return test_inject_nav(ser, fd, TEST_NAV_UP, "up", REL_Y, -5)


def test_inject_nav_down(ser, fd):
    """T19: Inject nav down, verify two REL_Y=+5 events."""
    return test_inject_nav(ser, fd, TEST_NAV_DOWN, "down", REL_Y, 5)


def test_inject_nav_left(ser, fd):
    """T20: Inject nav left, verify two REL_X=-5 events."""
    return test_inject_nav(ser, fd, TEST_NAV_LEFT, "left", REL_X, -5)


def test_inject_nav_right(ser, fd):
    """T21: Inject nav right, verify two REL_X=+5 events."""
    return test_inject_nav(ser, fd, TEST_NAV_RIGHT, "right", REL_X, 5)


def test_final_display(ser):
    """T13/T22: Leave a clean final message on display."""
    ser.write(bytes([CMD_CLEAR]))
    time.sleep(CLEAR_DELAY)

    lines = [
        (0, 0,  "Smoke Test"),
        (0, 16, f"PASS: {passed}"),
        (0, 24, f"FAIL: {failed}"),
        (0, 32, f"SKIP: {skipped}"),
        (0, 48, "Done!"),
    ]
    for x, y, text in lines:
        cmd = make_draw_text_cmd(x, y, text)
        ser.write(cmd)
        time.sleep(INTER_CMD_DELAY)

    log_pass("final_display")
    return True


# ============================================================================
# Main
# ============================================================================

def main():
    global passed, failed, skipped

    check_only = "--check-only" in sys.argv
    device_arg = None
    for arg in sys.argv[1:]:
        if not arg.startswith("--"):
            device_arg = arg

    print("=" * 50)
    print("micropanel Hardware Smoke Test")
    print("=" * 50)

    # Detect dongle
    print("\nDetecting USB HID Display dongle...")
    if device_arg:
        device_path = device_arg
        product = "(manual)"
        bcd = ""
    else:
        device_path, product, bcd = find_dongle()

    if device_path:
        print(f"  Device: {device_path}")
        if product:
            print(f"  Product: {product}")
        if bcd:
            print(f"  Firmware: bcdDevice={bcd}")
    else:
        print("  No dongle detected!")

    event_dev = find_hid_input()
    if event_dev:
        print(f"  HID Input: {event_dev}")

    print()

    if check_only:
        if device_path:
            print("Dongle detected. Use without --check-only to run full test suite.")
            return 0
        else:
            print("No dongle detected.")
            return 1

    # Run tests
    print("Running hardware smoke tests...\n")

    # T0: Device detection
    if not test_device_detection(device_path):
        print(f"\nResults: {passed} passed, {failed} failed, {skipped} skipped")
        return 1

    # T1: Serial open
    try:
        import serial
    except ImportError:
        print("ERROR: pyserial not installed. Run: pip install pyserial")
        return 1

    ser = open_serial(device_path)
    if not test_serial_open(ser):
        print(f"\nResults: {passed} passed, {failed} failed, {skipped} skipped")
        return 1

    hid_fd = -1
    try:
        # === Display command tests ===
        test_clear_display(ser)
        time.sleep(0.1)
        test_draw_text_origin(ser)
        time.sleep(0.5)
        test_draw_text_multiline(ser)
        time.sleep(1.0)
        test_draw_text_crlf_coords(ser)
        time.sleep(0.5)
        test_brightness_range(ser)
        test_invert_toggle(ser)
        test_progress_bar(ser)
        time.sleep(0.5)
        test_power_cycle(ser)
        time.sleep(0.3)
        test_rapid_commands(ser)
        test_max_text_length(ser)
        time.sleep(0.3)

        # HID input device check
        test_hid_input_device()

        # === Input injection tests (require test firmware + HID access) ===
        has_test_fw = test_ping(ser)

        if has_test_fw and event_dev:
            hid_fd = open_hid_input(event_dev)
            if hid_fd >= 0:
                print()
                print("  --- Input injection tests ---")
                test_inject_rotate_cw(ser, hid_fd)
                test_inject_rotate_ccw(ser, hid_fd)
                test_inject_button_press(ser, hid_fd)
                test_inject_nav_up(ser, hid_fd)
                test_inject_nav_down(ser, hid_fd)
                test_inject_nav_left(ser, hid_fd)
                test_inject_nav_right(ser, hid_fd)
                print("  --- End input injection ---")
            else:
                log_skip("input_injection",
                         f"Cannot open {event_dev} (add user to 'input' group)")
        elif has_test_fw and not event_dev:
            log_skip("input_injection", "HID input device not found")

        # Final display
        test_final_display(ser)

    finally:
        if hid_fd >= 0:
            os.close(hid_fd)
        if ser and ser.is_open:
            ser.close()

    # Summary
    print()
    if failures:
        print("Failures:")
        for name, reason in failures:
            print(f"  {name}: {reason}")
        print()

    total = passed + failed + skipped
    print(f"Results: {passed} passed, {failed} failed, {skipped} skipped, {total} total")

    return 1 if failed > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
