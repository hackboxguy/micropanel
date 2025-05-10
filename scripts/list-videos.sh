#!/bin/bash

# Path to directory containing video files
VIDEO_DIR="${1:-/media/files/path}"
LOCK_FILE="/tmp/micropanel_video.lock"
LAST_PLAYED_FILE="/tmp/last_played_video"

# Check if any arguments were provided
if [ $# -eq 0 ]; then
    # If called with no arguments, return the currently playing video
    if [ -f "$LAST_PLAYED_FILE" ]; then
        if [ -f "$LOCK_FILE" ]; then
            # Check if there's actually a video playing
            PLAYING_PID=$(cat "$LOCK_FILE" 2>/dev/null)
            if ps -p "$PLAYING_PID" >/dev/null 2>&1; then
                echo "$(cat "$LAST_PLAYED_FILE")"
            else
                # Stale lock file detected
                echo "No video is currently playing (last played: $(cat "$LAST_PLAYED_FILE"))"
                # Clean up stale lock
                rm -f "$LOCK_FILE"
            fi
        else
            echo "No video is currently playing (last played: $(cat "$LAST_PLAYED_FILE"))"
        fi
    else
        echo "No video has been played yet."
    fi
    exit 0
fi

# Check if directory exists
if [ ! -d "$VIDEO_DIR" ]; then
    echo "Error: Directory $VIDEO_DIR not found" >&2
    exit 1
fi

# List all video files in the directory (include more formats)
find "$VIDEO_DIR" -type f \( -name "*.mp4" -o -name "*.mkv" -o -name "*.avi" -o -name "*.mov" -o -name "*.h264" \) -printf "%f\n" | sort

exit 0
