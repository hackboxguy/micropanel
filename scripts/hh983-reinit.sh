#!/bin/sh
# Re-initialize hh983 serializer pipeline via micropanel menu
#
# Reads current config_mode from /etc/modprobe.d/hh983.conf and calls
# re-init-983-pipeline.sh with the matching --mode flag.
#
# Usage:
#   hh983-reinit.sh           # Run re-init with auto-detected mode

HH983_CONF="/etc/modprobe.d/hh983.conf"

# Determine current mode from hh983.conf
if [ -f "$HH983_CONF" ]; then
    current_mode=$(grep -o 'config_mode=[0-9]*' "$HH983_CONF" | head -1 | cut -d= -f2)
    case "$current_mode" in
        0) MODE="984" ;;
        *) MODE="988" ;;
    esac
else
    MODE="988"
fi

echo "Re-init mode: $MODE"
/usr/bin/re-init-983-pipeline.sh --skip-hdmi-toggle --mode="$MODE"
ret=$?

if [ $ret -eq 0 ]; then
    echo "[SUCCESS] Re-init complete"
else
    echo "[ERROR] Re-init failed (exit code $ret)"
fi

exit $ret
