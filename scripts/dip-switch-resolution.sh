#!/bin/sh
# Auto-detect display resolution from DIP switch (PCF8574 at 0x20 on i2c-3)
# and reconfigure HDMI timing if it doesn't match current config.
#
# DIP switch mapping:
#   0xf7  -> 12.3
#   0x7a  -> 12.3-nq1
#   0xfb  -> 14.6-fhd
#   0xfd  -> 14.6-2k5
#   0xfe  -> 15.6-2k5
#   0xdf  -> 17.3-3k
#   0x79  -> ots-oled-17 (DIP setting 01100001, active-low)
#   0x76  -> 3x-qvue (DIP setting 10010001, active-low)
#   0xef  -> 27
#
# Reboot loop guard: at most one auto-reboot per mismatch.
# Flag file is created before rebooting; if it exists on next boot
# and there's still a mismatch, no reboot is triggered.
#
# Usage:
#   dip-switch-resolution.sh [--configspath=DIR] [--input=FILE] [-v]

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
I2C_BUS=3
DIP_ADDR=0x20
DIP_WAIT_S=15          # how long to wait for i2c-3 and the PCF8574 at boot
REBOOT_FLAG="/var/lib/micropanel/dip-reboot-pending"
VERBOSE=0

# Default paths (overridable via args)
CONFIGS_PATH=""
INPUT_FILE=""

# Parse arguments
for arg in "$@"; do
    case "$arg" in
        --configspath=*) CONFIGS_PATH="${arg#*=}" ;;
        --input=*) INPUT_FILE="${arg#*=}" ;;
        -v|--verbose) VERBOSE=1 ;;
    esac
done

# Build pi-config-txt.sh argument string
build_config_args() {
    local args=""
    if [ -n "$CONFIGS_PATH" ]; then
        args="$args --configspath=$CONFIGS_PATH"
    fi
    if [ -n "$INPUT_FILE" ]; then
        args="$args --input=$INPUT_FILE"
    fi
    echo "$args"
}

log() {
    if [ $VERBOSE -eq 1 ]; then
        echo "[dip-switch] $1"
    fi
}

# --- Driver handling around the post-reconfigure RH850 reset -------------------
#
# The reset below (GPIO 22) restarts the 983HH RH850, which re-runs its whole
# bit-bang I2C init on the same bus the Pi is using. Leaving the serializer
# driver loaded across that means two masters on one bus; it killed the RH850
# init outright once (0x67 never answered afterwards). It also overwrites the
# driver's touch routing, so the touch driver has to come back afterwards too.
# Which modules those are is whatever this display type configured, not a
# hard-coded list: a video-only type loads the serializer alone.
MODULES_CONF="/etc/modules-load.d/custom-drivers.conf"

# lsmod prints module names with underscores; the conf file may use hyphens.
norm_mod() {
    echo "$1" | tr '-' '_'
}

module_is_loaded() {
    lsmod 2>/dev/null | awk '{print $1}' | grep -qx "$(norm_mod "$1")"
}

# Modules this display type wants, in load order.
configured_modules() {
    if [ -f "$MODULES_CONF" ]; then
        grep -v '^[[:space:]]*#' "$MODULES_CONF" 2>/dev/null | grep -v '^[[:space:]]*$'
    fi
}

# Unload in reverse order: touch first, then the serializer it depends on.
stop_drivers() {
    _rev=""
    for _m in $(configured_modules); do
        _rev="$_m $_rev"
    done
    for _m in $_rev; do
        if module_is_loaded "$_m"; then
            log "  unloading $_m"
            rmmod "$(norm_mod "$_m")" 2>/dev/null || log "  WARNING: rmmod $_m failed, continuing"
        fi
    done
}

start_drivers() {
    for _m in $(configured_modules); do
        log "  loading $_m"
        modprobe "$_m" 2>/dev/null || log "  WARNING: modprobe $_m failed"
        sleep 1
    done
}

# The RH850 takes a few seconds to run its profile after a reset; 0x67 only
# appears once it has finished and handed the bus to its status slave.
wait_for_rh850() {
    _t=0
    while [ "$_t" -lt 20 ]; do
        if i2ctransfer -y -f 1 w2@0x67 0x00 0x00 r8@0x67 >/dev/null 2>&1; then
            log "  RH850 answered at 0x67 after ${_t}s"
            return 0
        fi
        sleep 1
        _t=$((_t + 1))
    done
    log "  WARNING: RH850 did not answer at 0x67 within ${_t}s"
    return 1
}

# 983 APB LINK_ENABLE (0x000) through the indirect window.
apb_link_enable() {
    i2cset -y -f 1 0x18 0x48 0x01
    i2cset -y -f 1 0x18 0x49 0x00
    i2cset -y -f 1 0x18 0x4a 0x00
    i2cset -y -f 1 0x18 0x4b "$1"
    i2cset -y -f 1 0x18 0x4c 0x00
    i2cset -y -f 1 0x18 0x4d 0x00
    i2cset -y -f 1 0x18 0x4e 0x00
}

# A fresh probe deliberately does no HPD toggle (it would tear down a DP link
# that is already up at boot), so after a reload the DP input stays down until
# something asks the source to retrain.
hpd_toggle() {
    log "  HPD toggle (983 APB LINK_ENABLE 0 -> 1)"
    apb_link_enable 0x00
    sleep 1
    apb_link_enable 0x01
    sleep 2
}

# Map DIP switch value to display type
map_dip_to_type() {
    case "$1" in
        0xf7) echo "12.3" ;;
        0x7a) echo "12.3-nq1" ;;
        0xfb) echo "14.6-fhd" ;;
        0xfd) echo "14.6-2k5" ;;
        0xfe) echo "15.6-2k5" ;;
        0xdf) echo "17.3-3k" ;;
        0x79) echo "ots-oled-17" ;;
        0x76) echo "3x-qvue" ;;
        0xef) echo "27" ;;
        *)    echo "" ;;
    esac
}

# Read the DIP switch, waiting for the bus if it is not up yet.
#
# This unit starts After=local-fs.target, which can be before i2c-3 is
# registered. A single probe then fails and the type change is silently not
# applied - seen twice, once leaving a 12.3" panel running a 14.6" timing until
# someone noticed. So wait for the device node and retry the read, and say how
# long it took, rather than treating "not there yet" as "not fitted".
#
# The probe is a direct read rather than `i2cdetect | grep "20"`: it is the same
# read the script needs anyway, and a grep for "20" also matches the address
# column and any other device whose address ends in 0x20 on that row.
read_dip() {
    _t=0
    while [ "$_t" -lt "$DIP_WAIT_S" ]; do
        if [ -e "/dev/i2c-$I2C_BUS" ]; then
            _v=$(i2cget -y "$I2C_BUS" "$DIP_ADDR" 2>/dev/null)
            if [ -n "$_v" ]; then
                # log() writes to stdout and this function's stdout is the
                # value, so send the note to stderr or it lands in dip_value.
                [ "$_t" -gt 0 ] && log "DIP switch answered after ${_t}s" >&2
                echo "$_v"
                return 0
            fi
        fi
        sleep 1
        _t=$((_t + 1))
    done
    log "No DIP switch (PCF8574) found at $DIP_ADDR on i2c-$I2C_BUS after ${_t}s, skipping" >&2
    return 1
}

dip_value=$(read_dip) || exit 0

log "DIP switch value: $dip_value"

# Map to display type
expected_type=$(map_dip_to_type "$dip_value")
if [ -z "$expected_type" ]; then
    log "Unknown DIP switch value $dip_value, skipping"
    exit 0
fi

log "DIP switch selects display type: $expected_type"

# Read current config
config_args=$(build_config_args)
current_type=$("$SCRIPT_DIR/pi-config-txt.sh" $config_args 2>/dev/null)
if [ -z "$current_type" ]; then
    log "Failed to read current config, skipping"
    exit 0
fi

log "Current config type: $current_type"

# Compare
if [ "$current_type" = "$expected_type" ]; then
    log "Config matches DIP switch, no action needed"
    # After a DIP-switch-triggered reboot, power-cycle the display to restore sync
    if [ -f "$REBOOT_FLAG" ]; then
        prev_type=$(cat "$REBOOT_FLAG" 2>/dev/null)
        if [ "$prev_type" != "edid-hdmi" ]; then
            log "Post-reconfig reboot detected, power-cycling display..."
            DISPTOOL="/home/pi/micropanel/bin/disptool"
            if [ -x "$DISPTOOL" ]; then
                # Get the Pi off the bus before the RH850 reset: it re-runs its
                # bit-bang init on the same wires, and a second master there has
                # killed that init. This also means the touch routing the driver
                # installed is about to be overwritten, hence the reload after.
                log "Stopping drivers before the RH850 reset"
                stop_drivers
                "$DISPTOOL" --device=983 --command=disppower --value=off
                sleep 1
                "$DISPTOOL" --device=983 --command=disppower --value=on
                sleep 1
                gpioset 0 22=0
                sleep 2
                gpioset 0 22=1
                log "RH850 reset released, waiting for it to finish its profile"
                wait_for_rh850
                log "Restarting drivers"
                start_drivers
                hpd_toggle
                log "Display power-cycle complete"
            else
                log "Warning: disptool not found at $DISPTOOL, skipping power-cycle"
            fi
        else
            log "edid-hdmi config, skipping display power-cycle"
        fi
        rm -f "$REBOOT_FLAG"
        log "Cleared reboot flag"
    fi
    exit 0
fi

# Mismatch detected
log "Mismatch: config=$current_type, DIP=$expected_type"

# Reboot loop guard
if [ -f "$REBOOT_FLAG" ]; then
    echo "[dip-switch] WARNING: Mismatch persists after reboot (config=$current_type, DIP=$expected_type). Skipping to prevent reboot loop."
    exit 1
fi

# Create reboot flag directory and file
mkdir -p "$(dirname "$REBOOT_FLAG")"
echo "$expected_type" > "$REBOOT_FLAG"

echo "[dip-switch] Reconfiguring HDMI timing to $expected_type (was $current_type)"

# Call pi-config-txt.sh with the correct type (this triggers a reboot)
"$SCRIPT_DIR/pi-config-txt.sh" $config_args --type="$expected_type"
