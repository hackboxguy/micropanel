#!/bin/bash

# Path to directory containing image files
IMAGE_DIR="${1:-/media/files/path}"

# Check if any arguments were provided
if [ $# -eq 0 ]; then
    # If called with no arguments, return the currently playing image
    if [ -f "/tmp/last_played_image" ]; then
        cat "/tmp/last_played_image"
    fi
    exit 0
fi

# Check if directory exists
if [ ! -d "$IMAGE_DIR" ]; then
    echo "Error: Directory $IMAGE_DIR not found"
    exit 1
fi

# List all .mkv files in the directory
find "$IMAGE_DIR" -type f \( -name "*.jpg" -o -name "*.jpeg" -o -name "*.png" -o -name "*.bmp" \) -printf "%f\n" | sort
exit 0
