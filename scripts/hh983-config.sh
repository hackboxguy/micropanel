#!/bin/sh
# Read or set hh983-serializer config_mode via /etc/modprobe.d/hh983.conf
#
# Usage:
#   hh983-config.sh                    # Output current deserializer type
#   hh983-config.sh --type=988         # Set config_mode=1 (983+988)
#   hh983-config.sh --type=984         # Set config_mode=0 (983+984)
#   hh983-config.sh --type=988-video   # Set config_mode=2 (983+988, video only)

HH983_CONF="/etc/modprobe.d/hh983.conf"

# Parse arguments
DESER_TYPE=""
for arg in "$@"; do
    case "$arg" in
        --type=*) DESER_TYPE="${arg#*=}" ;;
    esac
done

# Read mode: output current deserializer type
if [ -z "$DESER_TYPE" ]; then
    if [ -f "$HH983_CONF" ]; then
        current_mode=$(grep -o 'config_mode=[0-9]*' "$HH983_CONF" | head -1 | cut -d= -f2)
        case "$current_mode" in
            0) echo "984" ;;
            2) echo "988-video" ;;
            *) echo "988" ;;
        esac
    else
        echo "988"
    fi
    exit 0
fi

# Write mode: update hh983.conf and reboot
case "$DESER_TYPE" in
    988)
        echo "options hh983-serializer config_mode=1" > "$HH983_CONF"
        ;;
    984)
        echo "options hh983-serializer config_mode=0" > "$HH983_CONF"
        ;;
    988-video)
        # 983+988 without touch: mode 2 only enables pass-through and leaves
        # the 983 target-alias slot the RH850 uses for the 988 untouched.
        echo "options hh983-serializer config_mode=2" > "$HH983_CONF"
        ;;
    *)
        echo "Error: Unknown type '$DESER_TYPE'. Use 988, 984 or 988-video." >&2
        exit 1
        ;;
esac

reboot
