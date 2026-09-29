#!/bin/sh
# micropanel.service stands down on a board without an SSD1306.
#
# No root and no hardware: the unit is rendered the way CMake renders it
# (configure_file @ONLY, with cmake when it is installed), and a fake
# i2cdetect stands in for the bus. Run: sh tests/test_micropanel_service.sh
# (also a CTest with BUILD_TESTS)
#
# What it proves:
#  1. the rendered unit runs the probe as ExecCondition with micropanel's own
#     arguments, before ExecStart, and keeps Restart=on-failure;
#  2. the probe exits 1 (a skipped condition, not a failure) when nothing
#     answers at 0x3c, including on a bus that does not exist, and 0 when the
#     panel answers, when a driver holds it, for a serial display, with -a
#     when the USB dongle is plugged in, and when i2cdetect is missing.
set -eu

repo=$(cd "$(dirname "$0")/.." && pwd)
probe="$repo/scripts/micropanel-oled-present.sh"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
failures=0
fail() { echo "FAIL: $*" >&2; failures=$((failures + 1)); }
ok() { echo "  ok  $*"; }

# --- 1. the rendered unit ---------------------------------------------------
prefix=/home/pi/micropanel
args='-a -i gpio -s /dev/i2c-3'
unit="$work/micropanel.service"
if command -v cmake >/dev/null 2>&1; then
    printf '%s\n' \
        "set(CMAKE_INSTALL_PREFIX \"$prefix\")" \
        'set(SERVICE_USER root)' \
        "set(SYSTEMD_UNITFILE_ARGS \"$args\")" \
        "configure_file(\"$repo/micropanel.service.in\" \"$unit\" @ONLY)" > "$work/render.cmake"
    cmake -P "$work/render.cmake"
else
    sed -e "s#@CMAKE_INSTALL_PREFIX@#$prefix#g" -e 's#@SERVICE_USER@#root#g' \
        -e "s#@SYSTEMD_UNITFILE_ARGS@#$args#g" "$repo/micropanel.service.in" > "$unit"
fi
unit_failures=$failures
if grep -q '@[A-Z_]*@' "$unit"; then fail "unrendered placeholder in the unit"; fi
condition=$(grep '^ExecCondition=' "$unit" || true)
[ "$condition" = "ExecCondition=$prefix/usr/bin/micropanel-oled-present.sh $args" ] ||
    fail "ExecCondition is '$condition'"
condition_line=$(grep -n '^ExecCondition=' "$unit" | cut -d: -f1)
start_line=$(grep -n '^ExecStart=' "$unit" | cut -d: -f1)
if [ -z "$condition_line" ] || [ -z "$start_line" ] || [ "$condition_line" -gt "$start_line" ]; then
    fail "ExecCondition does not precede ExecStart"
fi
grep -qx 'Restart=on-failure' "$unit" || fail "Restart=on-failure is gone"
grep -q "micropanel $args " "$unit" || fail "ExecStart no longer carries the same arguments"
grep -q 'scripts/micropanel-oled-present.sh' "$repo/CMakeLists.txt" || fail "the probe is not installed"
[ "$failures" -ne "$unit_failures" ] || ok "rendered unit: probe as ExecCondition before ExecStart, Restart=on-failure kept"

# --- 2. the probe -------------------------------------------------------------
# Real i2cdetect output for `-y N 0x3c 0x3c` (captured on the head-unit rig):
# only the 0x3c cell is filled. A bus other than FAKE_BUS does not exist.
# USB devices: a hub and a keyboard, no dongle (added below).
mkdir -p "$work/usb/1-1" "$work/usb/1-1.2"
printf '%s\n' 2109 > "$work/usb/1-1/idVendor"; printf '%s\n' 3431 > "$work/usb/1-1/idProduct"
printf '%s\n' 046d > "$work/usb/1-1.2/idVendor"; printf '%s\n' c31c > "$work/usb/1-1.2/idProduct"
fake="$work/i2cdetect"
printf '%s\n' \
    '#!/bin/sh' \
    'printf "%s\n" "$*" >> "$FAKE_LOG"' \
    '[ "$2" = "$FAKE_BUS" ] || { echo "Error: Could not open file /dev/i2c-$2: No such file or directory" >&2; exit 1; }' \
    'printf "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n"' \
    'for row in 00 10 20; do printf "%s:                                                 \n" "$row"; done' \
    'printf "30:                                     %s          \n" "$FAKE_CELL"' \
    'for row in 40 50 60 70; do printf "%s:                                                 \n" "$row"; done' \
    > "$fake"
chmod 0755 "$fake"

probe_case() { # $1=label $2=expected exit $3=cell $4=i2cdetect; remaining = arguments
    label=$1 expected=$2 cell=$3 detect=$4
    shift 4
    rc=0
    : > "$work/fake.log"
    FAKE_LOG="$work/fake.log" FAKE_CELL="$cell" FAKE_BUS=3 MICROPANEL_I2CDETECT="$detect" \
        MICROPANEL_USB_SYSFS="$work/usb" \
        sh "$probe" "$@" 2>"$work/probe.err" || rc=$?
    if [ "$rc" = "$expected" ]; then ok "$label -> exit $rc"; else fail "$label: exit $rc, expected $expected"; fi
}

probe_case 'nothing at 0x3c' 1 '--' "$fake" -a -i gpio -s /dev/i2c-3
grep -qx -- '-y 3 0x3c 0x3c' "$work/fake.log" || fail "probed '$(cat "$work/fake.log")', not bus 3 address 0x3c"
grep -q 'no SSD1306 at 0x3c on /dev/i2c-3; standing down' "$work/probe.err" || fail "no reason for the journal"
probe_case 'SSD1306 answers' 0 '3c' "$fake" -a -i gpio -s /dev/i2c-3
probe_case 'held by a kernel driver' 0 'UU' "$fake" -a -i gpio -s /dev/i2c-3
probe_case 'bus does not exist' 1 '3c' "$fake" -a -i gpio -s /dev/i2c-7
probe_case 'attached form -s/dev/i2c-3' 0 '3c' "$fake" -a -s/dev/i2c-3
probe_case 'serial display, not probed' 0 '--' "$fake" -a -s /dev/ttyACM0
[ ! -s "$work/fake.log" ] || fail "a serial display was probed on I2C"
probe_case 'no -s argument' 0 '--' "$fake" -a -i gpio
probe_case 'i2cdetect missing, start anyway' 0 '--' "$work/absent" -a -i gpio -s /dev/i2c-3
# The USB dongle, which hybrid mode (-a) uses first.
mkdir -p "$work/usb/1-1.3"
printf '%s\n' 1209 > "$work/usb/1-1.3/idVendor"; printf '%s\n' 0001 > "$work/usb/1-1.3/idProduct"
probe_case 'no OLED, USB dongle with -a' 0 '--' "$fake" -a -i gpio -s /dev/i2c-3
[ ! -s "$work/fake.log" ] || fail "the bus was probed although the dongle is there"
probe_case 'no OLED, USB dongle without -a' 1 '--' "$fake" -i gpio -s /dev/i2c-3

if [ "$failures" -ne 0 ]; then
    echo "micropanel-service: $failures failure(s)" >&2
    exit 1
fi
echo "micropanel-service: PASS"
