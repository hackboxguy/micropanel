#!/bin/sh
# micropanel.service's ExecCondition: is the SSD1306 there?
#
# Usage: micropanel-oled-present.sh [micropanel arguments...]
# Takes the same arguments as micropanel and reads the display device from
# `-s <device>`, so the bus is whatever the unit was built with.
#
# Without an SSD1306 the board is not the hand-held flashing tool but runs on
# the 983HH adapter as an automotive head unit, and there micropanel.service is
# not wanted until the next boot, which probes again. Exit 1 then: systemd
# skips ExecStart and leaves the unit inactive - not failed, and never
# restarted, since Restart= does not apply to a condition. A crash with the
# panel present still restarts (Restart=on-failure).
#
# Exit 0 (run micropanel) when the display is not on I2C (a serial display),
# with -a (hybrid) when the USB dongle (1209:0001, which micropanel prefers)
# is plugged in, when i2cdetect is missing (cannot tell), or when 0x3c answers
# or is claimed by a kernel driver (UU). Exit 1 when nothing answers at 0x3c, including on a
# bus that does not exist (i2cdetect fails).
#
# MICROPANEL_I2CDETECT replaces i2cdetect (tests); a path that is not
# executable counts as i2cdetect missing. MICROPANEL_USB_SYSFS replaces
# /sys/bus/usb/devices (tests).

device=""
auto=0
while [ $# -gt 0 ]; do
    case "$1" in
        -s) device=${2:-}; shift ;;
        -s*) device=${1#-s} ;;
        -a) auto=1 ;;
    esac
    shift
done

if [ "$auto" = 1 ]; then
    for usb in "${MICROPANEL_USB_SYSFS:-/sys/bus/usb/devices}"/*; do
        [ "$(cat "$usb/idVendor" 2>/dev/null)" = 1209 ] &&
            [ "$(cat "$usb/idProduct" 2>/dev/null)" = 0001 ] && exit 0
    done
fi

case "$device" in
    /dev/i2c-[0-9]*) ;;
    *) exit 0 ;;
esac
bus=${device#/dev/i2c-}
case "$bus" in *[!0-9]*) exit 0 ;; esac

i2cdetect=${MICROPANEL_I2CDETECT:-}
if [ -z "$i2cdetect" ]; then
    for candidate in /usr/sbin/i2cdetect /sbin/i2cdetect /usr/bin/i2cdetect; do
        [ -x "$candidate" ] && { i2cdetect=$candidate; break; }
    done
fi
if [ -z "$i2cdetect" ] || [ ! -x "$i2cdetect" ]; then
    echo "micropanel: i2cdetect not found; cannot probe for the SSD1306, starting anyway" >&2
    exit 0
fi

# Probe the one address. Its cell reads "3c" when a device acknowledges and
# "UU" when a kernel driver holds it; "--" is nothing there.
if "$i2cdetect" -y "$bus" 0x3c 0x3c 2>/dev/null | grep -Eq '^30:.*[[:space:]](3c|UU)([[:space:]]|$)'; then
    exit 0
fi
echo "micropanel: no SSD1306 at 0x3c on $device; standing down until the next boot (head-unit board)" >&2
exit 1
