#!/bin/sh
# Re-initialize hh983 serializer pipeline via micropanel menu
#
# Reads current config_mode from /etc/modprobe.d/hh983.conf and calls
# re-init-983-pipeline.sh with the matching --mode flag:
#   config_mode=0 -> --mode=984
#   config_mode=1 -> --mode=988        (also the fallback for a missing file)
#   config_mode=2 -> --mode=988-video  (983+988, no touch driver steps)
#
# Usage:
#   hh983-reinit.sh           # Run re-init with auto-detected mode

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
HH983_CONF="/etc/modprobe.d/hh983.conf"
PIPELINE="re-init-983-pipeline.sh"

# Locate the pipeline script. It ships in the hh983-serializer Buildroot package
# and lands in /usr/bin on an image built from it, so on such an image it sits
# beside this script. A relocated tree instead keeps the br-wrapper helpers in
# <root>/bin while these micropanel scripts live in <root>/usr/bin, which is
# two levels up and back down. Try the layouts, then $PATH, rather than
# assuming one.
find_pipeline() {
    for _c in "$SCRIPT_DIR/$PIPELINE" \
              "$SCRIPT_DIR/../bin/$PIPELINE" \
              "$SCRIPT_DIR/../../bin/$PIPELINE"; do
        if [ -x "$_c" ]; then
            (cd "$(dirname "$_c")" && pwd -P | sed "s#\$#/$PIPELINE#")
            return 0
        fi
    done
    _c=$(command -v "$PIPELINE" 2>/dev/null)
    if [ -n "$_c" ]; then
        echo "$_c"
        return 0
    fi
    return 1
}

# Determine current mode from hh983.conf
if [ -f "$HH983_CONF" ]; then
    current_mode=$(grep -o 'config_mode=[0-9]*' "$HH983_CONF" | head -1 | cut -d= -f2)
    case "$current_mode" in
        0) MODE="984" ;;
        2) MODE="988-video" ;;
        *) MODE="988" ;;
    esac
else
    MODE="988"
fi

PIPELINE_PATH=$(find_pipeline)
if [ -z "$PIPELINE_PATH" ]; then
    echo "[ERROR] $PIPELINE not found. Looked in:" >&2
    echo "          $SCRIPT_DIR/$PIPELINE" >&2
    echo "          $SCRIPT_DIR/../bin/$PIPELINE" >&2
    echo "          $SCRIPT_DIR/../../bin/$PIPELINE" >&2
    echo "          \$PATH ($PATH)" >&2
    echo "        It is installed by the hh983-serializer Buildroot package." >&2
    exit 1
fi

echo "Re-init mode: $MODE"
echo "Pipeline: $PIPELINE_PATH"
"$PIPELINE_PATH" --skip-hdmi-toggle --mode="$MODE"
ret=$?

if [ $ret -eq 0 ]; then
    echo "[SUCCESS] Re-init complete"
else
    echo "[ERROR] Re-init failed (exit code $ret)"
fi

exit $ret
