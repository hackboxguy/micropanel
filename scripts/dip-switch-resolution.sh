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

# Map DIP switch value to display type
map_dip_to_type() {
    case "$1" in
        0xf7) echo "12.3" ;;
        0x7a) echo "12.3-nq1" ;;
        0xfb) echo "14.6-fhd" ;;
        0xfd) echo "14.6-2k5" ;;
        0xfe) echo "15.6-2k5" ;;
        0xdf) echo "17.3-3k" ;;
        0xef) echo "27" ;;
        *)    echo "" ;;
    esac
}

# Check if DIP switch device is present on i2c bus
if ! i2cdetect -y "$I2C_BUS" 2>/dev/null | grep -q "20"; then
    log "No DIP switch (PCF8574) found at $DIP_ADDR on i2c-$I2C_BUS, skipping"
    exit 0
fi

# Read DIP switch value
dip_value=$(i2cget -y "$I2C_BUS" "$DIP_ADDR" 2>/dev/null)
if [ -z "$dip_value" ]; then
    log "Failed to read DIP switch, skipping"
    exit 0
fi

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
    # Clear reboot flag if it exists (successful boot after reconfig)
    if [ -f "$REBOOT_FLAG" ]; then
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
