#!/bin/bash

# Define constants
LOCK_FILE="/tmp/micropanel_video.lock"
LAST_PLAYED_FILE="/tmp/last_played_video"

# This script stops any currently playing video
echo "Stopping video playback" >&2

# Check if a video is currently playing
if [ -f "$LOCK_FILE" ]; then
    PLAYING_PID=$(cat "$LOCK_FILE" 2>/dev/null)
    
    # Check if process is still running
    if ps -p "$PLAYING_PID" >/dev/null 2>&1; then
        echo "Stopping video with PID $PLAYING_PID" >&2
        
        # First try graceful termination
        kill "$PLAYING_PID" 2>/dev/null
        # Wait briefly for graceful shutdown
        sleep 1
        
        # Force kill if still running
        if ps -p "$PLAYING_PID" >/dev/null 2>&1; then
            echo "Forcing termination of video process" >&2
            kill -9 "$PLAYING_PID" 2>/dev/null
            sleep 0.5
        fi
        
        # Also kill any other mpv processes that might be running
        pkill -f mpv || true
    else
        echo "No active video process found (stale lock detected)" >&2
    fi
    
    # Remove the lock file
    rm -f "$LOCK_FILE"
else
    # Try to kill any mpv processes anyway
    pkill -f mpv 2>/dev/null || true
    echo "No video lock file found" >&2
fi

# Optional: Reset the framebuffer to ensure clean state
if [ -c /dev/fb0 ]; then
    echo "Clearing framebuffer" >&2
    dd if=/dev/zero of=/dev/fb0 bs=1M count=1 >/dev/null 2>&1 || true
fi

echo "Video playback stopped successfully" >&2
exit 0
