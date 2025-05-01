#!/bin/bash

# Path to directory containing video files
VIDEO_DIR="${1:-/media/files/path}"

# Check if any arguments were provided
if [ $# -eq 0 ]; then
    # If called with no arguments, return the currently playing video
    if [ -f "/tmp/last_played_video" ]; then
        cat "/tmp/last_played_video"
    fi
    exit 0
fi

# Check if directory exists
if [ ! -d "$VIDEO_DIR" ]; then
    echo "Error: Directory $VIDEO_DIR not found"
    exit 1
fi

# List all .mkv files in the directory
find "$VIDEO_DIR" -type f -name "*.mkv" -printf "%f\n" | sort

exit 0
