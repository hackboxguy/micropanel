#!/bin/bash

# Check if a filename was provided
if [ -z "$1" ]; then
    echo "Error: No video file specified"
    exit 1
fi

# Set video directory (use default if not provided)
VIDEO_DIR="${2:-/media/files/path}"

# Find the full path to the video
VIDEO_PATH="$VIDEO_DIR/$1"

# Check if file exists
if [ ! -f "$VIDEO_PATH" ]; then
    echo "Error: Video file '$VIDEO_PATH' not found"
    exit 1
fi

# Simulate playing the video
echo "Playing video: $VIDEO_PATH"
# In a real system, you would use a player like:
# mplayer "$VIDEO_PATH" or vlc "$VIDEO_PATH" etc.

# Store the current video as the last played
echo "$1" > /tmp/last_played_video

exit 0
