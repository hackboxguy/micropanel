#!/bin/bash

# This script stops any currently playing image
# In a real system, you would use commands like:
# killall mplayer
# killall vlc
# etc.

echo "Stopping image playback"
sudo dd if=/dev/zero of=/dev/fb0 > /dev/null 2>&1
# Clear the last played video file
echo "" > /tmp/last_played_image

exit 0
