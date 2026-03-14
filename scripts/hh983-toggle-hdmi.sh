#!/bin/sh
# Toggle Pi4 HDMI output (cycle: off -> 1s -> on)
# Wrapper for toggle-pi-hdmi.sh with [SUCCESS]/[ERROR] markers for micropanel

/usr/bin/toggle-pi-hdmi.sh cycle
ret=$?

if [ $ret -eq 0 ]; then
    echo "[SUCCESS] HDMI toggle complete"
else
    echo "[ERROR] HDMI toggle failed (exit code $ret)"
fi

exit $ret
