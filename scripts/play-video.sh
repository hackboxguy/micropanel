#!/bin/bash

# Define constants
LOCK_FILE="/tmp/micropanel_video.lock"
LAST_PLAYED_FILE="/tmp/last_played_video"

# Check if a filename was provided
if [ -z "$1" ]; then
    echo "Error: No video file specified" >&2
    exit 1
fi

# Set video directory (use default if not provided)
VIDEO_DIR="${2:-/media/files/path}"

# Find the full path to the video
VIDEO_PATH="$VIDEO_DIR/$1"

# Check if file exists
if [ ! -f "$VIDEO_PATH" ]; then
    echo "Error: Video file '$VIDEO_PATH' not found" >&2
    exit 1
fi

# Check if a video is already playing
if [ -f "$LOCK_FILE" ]; then
    PLAYING_PID=$(cat "$LOCK_FILE" 2>/dev/null)
    
    # Check if process is still running
    if ps -p "$PLAYING_PID" >/dev/null 2>&1; then
        # Kill the current video player process
        kill "$PLAYING_PID" 2>/dev/null || true
        sleep 0.5
        
        # Force kill if still running
        if ps -p "$PLAYING_PID" >/dev/null 2>&1; then
            kill -9 "$PLAYING_PID" 2>/dev/null || true
        fi
    fi
    
    # Remove the lock file
    rm -f "$LOCK_FILE"
fi

# Store the current video as the last played
echo "$1" > "$LAST_PLAYED_FILE"

# Ensure permissions for framebuffer and DRM devices
if [ -c "/dev/fb0" ] && [ ! -w "/dev/fb0" ]; then
    chmod 666 /dev/fb0 2>/dev/null || true
fi

if [ -c "/dev/dri/card0" ] && [ ! -w "/dev/dri/card0" ]; then
    chmod 666 /dev/dri/card0 2>/dev/null || true
fi

# Create a launcher script with proper cleanup
LAUNCHER_SCRIPT="/tmp/mpv_launcher_$$.sh"
cat > "$LAUNCHER_SCRIPT" << 'EOF'
#!/bin/bash
# This file will self-delete after execution

# Arguments passed from main script
VIDEO_PATH="$1"
LOCK_FILE="$2"

# Launch MPV with specified parameters
mpv \
  --vo=gpu \
  --hwdec=v4l2m2m-copy \
  --vd-lavc-threads=4 \
  --profile=low-latency \
  --gpu-context=drm \
  --drm-device=/dev/dri/card0 \
  --framedrop=vo \
  --no-terminal \
  --fullscreen \
  --no-osc \
  --no-osd-bar \
  "$VIDEO_PATH" > /dev/null 2>&1 &

# Store PID in lock file
MPV_PID=$!
echo $MPV_PID > "$LOCK_FILE"

# Wait for MPV process to finish (this won't block the parent script)
wait $MPV_PID

# Clean up after playback is complete
rm -f "$LOCK_FILE"
rm -f "$0"  # Self-delete this launcher script
EOF

# Make the launcher script executable
chmod +x "$LAUNCHER_SCRIPT"

# Launch the helper script completely detached from this process
nohup "$LAUNCHER_SCRIPT" "$VIDEO_PATH" "$LOCK_FILE" > /dev/null 2>&1 &

# No need to wait at all - return immediately
echo "Video playback initiated" >&2
exit 0
