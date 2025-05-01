#!/bin/bash

# Path to store the HDMI patch state
STATE_FILE="/tmp/hdmi_patch_state"

# Available patch modes
VALID_MODES=("Off" "Red" "Green" "Blue" "Cyan" "Magenta" "Yellow" "White")

# Initialize state file if it doesn't exist
if [ ! -f "$STATE_FILE" ]; then
    echo "Off" > "$STATE_FILE"
fi

# Function to get current state
get_state() {
    cat "$STATE_FILE"
}

# Function to set state
set_state() {
    echo "$1" > "$STATE_FILE"
}

# Function to validate mode
is_valid_mode() {
    for mode in "${VALID_MODES[@]}"; do
        if [ "$mode" = "$1" ]; then
            return 0
        fi
    done
    return 1
}

# Check command line arguments
if [ $# -eq 0 ]; then
    # No arguments, just return current state
    get_state
else
    # Check if the mode is valid
    if is_valid_mode "$1"; then
        # Set the HDMI patch mode
        set_state "$1"
        echo "HDMI patch set to $1"
    else
        # Invalid mode
        echo "Invalid mode. Valid modes are: ${VALID_MODES[*]}"
        exit 1
    fi
fi

exit 0
