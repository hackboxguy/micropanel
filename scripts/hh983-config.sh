#!/bin/sh
# Read or set hh983-serializer config_mode via /etc/modprobe.d/hh983.conf
#
# Usage:
#   hh983-config.sh                    # Output current deserializer type
#   hh983-config.sh --type=988         # Set config_mode=1 (983+988)
#   hh983-config.sh --type=984         # Set config_mode=0 (983+984)
#   hh983-config.sh --type=988-video   # Set config_mode=2 (983+988, video only)
#
# Writing only ever changes config_mode; every other option on the
# "options hh983-serializer" line is left where it is. See set_config_mode().
#
# HH983_CONF may be overridden in the environment, which is what the host-side
# test of this script does.

HH983_CONF="${HH983_CONF:-/etc/modprobe.d/hh983.conf}"

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

# Set config_mode without disturbing anything else in the file.
#
# config_mode is the only option this script owns.  The others on that line were
# put there for the panel that is attached: ots_touch=1 for the OTS-OLED, and
# wedge_recovery=1 for the 15.6" 2K5, whose DP guard has to recover a wedged DTG
# by 984 digital reset instead of the DTG reset pulse -- on that panel the pulse
# drops the picture into the TDDI self test whenever it interrupts a live
# stream.  See br-wrapper/docs/hh983-984-black-screen-2026-09-13/.
#
# This used to rewrite the whole file, which dropped those options silently and
# put the panel straight back on the behaviour that blacked it out.  Now only
# the config_mode= token is rewritten; the rest of the line, and every other
# line (the OLED's "softdep himax_oled pre: hh983-serializer"), is left alone.
#
# The one thing deliberately removed is an "install hh983-serializer /bin/true"
# stanza, which pi-config-txt.sh writes for edid-hdmi to keep the driver out of
# the way.  Choosing a deserializer type here means the driver is wanted, and
# the old whole-file rewrite had that effect too -- preserving it would silently
# leave the driver disabled.
set_config_mode() {
    _mode="$1"

    if [ ! -f "$HH983_CONF" ]; then
        echo "options hh983-serializer config_mode=$_mode" > "$HH983_CONF"
        return
    fi

    sed -i '/^[[:space:]]*install[[:space:]][[:space:]]*hh983-serializer/d' "$HH983_CONF"

    if grep -q '^[[:space:]]*options[[:space:]][[:space:]]*hh983-serializer' "$HH983_CONF"; then
        if grep -q '^[[:space:]]*options[[:space:]][[:space:]]*hh983-serializer.*config_mode=' "$HH983_CONF"; then
            sed -i "/^[[:space:]]*options[[:space:]][[:space:]]*hh983-serializer/s/config_mode=[0-9]*/config_mode=$_mode/" "$HH983_CONF"
        else
            sed -i "/^[[:space:]]*options[[:space:]][[:space:]]*hh983-serializer/s/\$/ config_mode=$_mode/" "$HH983_CONF"
        fi
    else
        echo "options hh983-serializer config_mode=$_mode" >> "$HH983_CONF"
    fi
}

# Write mode: update hh983.conf and reboot
case "$DESER_TYPE" in
    988)
        set_config_mode 1
        ;;
    984)
        set_config_mode 0
        ;;
    988-video)
        # 983+988 without touch: mode 2 only enables pass-through and leaves
        # the 983 target-alias slot the RH850 uses for the 988 untouched.
        set_config_mode 2
        ;;
    *)
        echo "Error: Unknown type '$DESER_TYPE'. Use 988, 984 or 988-video." >&2
        exit 1
        ;;
esac

reboot
