#!/bin/bash

# This script stops any currently playing video
# In a real system, you would use commands like:
# killall mplayer
# killall vlc
# etc.

echo "Stopping video playback"

# Clear the last played video file
echo "" > /tmp/last_played_video

exit 0
