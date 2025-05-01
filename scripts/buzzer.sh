#!/bin/bash

# Path to store the buzzer state
STATE_FILE="/tmp/buzzer_state"

# Initialize state file if it doesn't exist
if [ ! -f "$STATE_FILE" ]; then
    echo "On" > "$STATE_FILE"
fi

# Function to get current state
get_state() {
    cat "$STATE_FILE"
}

# Function to set state
set_state() {
    echo "$1" > "$STATE_FILE"
}

# Check command line arguments
if [ $# -eq 0 ]; then
    # No arguments, just return current state
    get_state
elif [ "$1" = "On" ]; then
    # Enable buzzer
    set_state "On"
    echo "Buzzer enabled"
elif [ "$1" = "Off" ]; then
    # Disable buzzer
    set_state "Off"
    echo "Buzzer disabled"
else
    # Invalid argument
    echo "Usage: $0 [On|Off]"
    exit 1
fi

exit 0
