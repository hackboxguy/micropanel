#!/bin/sh
# POSIX-compliant shell script to work with dash/sh
# Optimized version using external templates and configuration data

# Use proper system paths for config files
CONFIG_DIR="/usr/share/micropanel/configs"
CONFIG_DATA_FILE="$CONFIG_DIR/display-configs.conf"
BASE_TEMPLATE="$CONFIG_DIR/config-base.txt.in"

# Function to display usage
usage() {
    echo "Usage:"
    echo "  Write config: $0 --input=/boot/firmware/config.txt --type=<12.3/14.6-fhd/14.6/15.6/27/edid>"
    echo "  Read config: $0 --input=/boot/firmware/config.txt [--query-config] [-v]"
    echo "  Query display: $0 --query-display [-v]"
    echo ""
    echo "Options:"
    echo "  --input=FILE      Specify the configuration file path"
    echo "  --type=TYPE       Configure for display type (12.3, 14.6-fhd, 14.6, 15.6, 27, edid)"
    echo "  --configspath=DIR Override config directory path (default: /usr/share/micropanel/configs)"
    echo "  --query-config    Read and output configured display type/resolution"
    echo "  --query-display   Query actual display resolution on HDMI output"
    echo "  -v, --verbose     Show detailed information"
    exit 1
}

# Function to load display configuration data
load_display_config() {
    local config_type="$1"

    if [ ! -f "$CONFIG_DATA_FILE" ]; then
        echo "Error: Configuration data file not found: $CONFIG_DATA_FILE" >&2
        exit 1
    fi

    # Parse the configuration line for the specified type
    config_line=$(grep "^$config_type:" "$CONFIG_DATA_FILE" | head -1)

    if [ -z "$config_line" ]; then
        echo "Error: Unknown display type: $config_type" >&2
        exit 1
    fi

    # Extract fields using shell parameter expansion
    DISPLAY_TYPE="$config_type"
    RESOLUTION=$(echo "$config_line" | cut -d: -f2)
    HDMI_TIMINGS=$(echo "$config_line" | cut -d: -f3)
    PIXEL_FREQ=$(echo "$config_line" | cut -d: -f4)
    FB_WIDTH=$(echo "$config_line" | cut -d: -f5)
    FB_HEIGHT=$(echo "$config_line" | cut -d: -f6)
    DESCRIPTION=$(echo "$config_line" | cut -d: -f7)
}

# Function to get all supported display types
get_supported_types() {
    if [ -f "$CONFIG_DATA_FILE" ]; then
        grep -v "^#" "$CONFIG_DATA_FILE" | grep -v "^$" | cut -d: -f1
    fi
}

# Function to generate display-specific configuration
generate_display_config() {
    local config_type="$1"

    load_display_config "$config_type"

    if [ "$config_type" = "edid" ] || [ "$config_type" = "edid-hdmi" ]; then
        # EDID auto-detection - use full KMS for proper EDID reading
        echo "dtoverlay=vc4-kms-v3d"
        echo "display_auto_detect=1"
    else
        # Custom timing configuration - use fake KMS for custom hdmi_timings
        echo "dtoverlay=vc4-fkms-v3d"
        echo "hdmi_group=2"
        echo "hdmi_mode=87"
        echo "hdmi_timings=$HDMI_TIMINGS"
        echo "hdmi_pixel_freq_limit=$PIXEL_FREQ"
        echo "framebuffer_width=$FB_WIDTH"
        echo "framebuffer_height=$FB_HEIGHT"
        echo "max_framebuffer_width=$FB_WIDTH"
        echo "max_framebuffer_height=$FB_HEIGHT"
        echo "config_hdmi_boost=4"
    fi
}

# Function to configure HH983 serializer module
configure_hh983_serializer() {
    local config_type="$1"
    local hh983_conf="/etc/modprobe.d/hh983.conf"

    # edid-hdmi: disable both hh983-serializer and himax_mmi for direct HDMI output
    if [ "$config_type" = "edid-hdmi" ]; then
        printf "install hh983-serializer /bin/true\ninstall himax_mmi /bin/true\n" > "$hh983_conf"
        if [ $VERBOSE -eq 1 ]; then
            echo "HH983 serializer and himax touch disabled (for $config_type)"
        fi
    # 15.6-2k5 and 12.3-nq1 require config_mode=0 (983+984), all others require config_mode=1 (983+988)
    elif [ "$config_type" = "15.6-2k5" ] || [ "$config_type" = "12.3-nq1" ]; then
        echo "options hh983-serializer config_mode=0" > "$hh983_conf"
        if [ $VERBOSE -eq 1 ]; then
            echo "HH983 serializer configured: config_mode=0 (for $config_type)"
        fi
    else
        echo "options hh983-serializer config_mode=1" > "$hh983_conf"
        if [ $VERBOSE -eq 1 ]; then
            echo "HH983 serializer configured: config_mode=1 (default)"
        fi
    fi
}

# Function to configure als-dimmer symlink for displays that use dimmer2048
configure_als_dimmer() {
    local config_type="$1"
    local als_dir="/home/pi/als-dimmer/etc/als-dimmer"
    local als_link="$als_dir/config.json"
    local als_target="$als_dir/config_fpga_opti4001_dimmer2048.json"

    # Only 15.6-2k5 and 12.3-nq1 need the dimmer2048 config
    if [ "$config_type" = "15.6-2k5" ] || [ "$config_type" = "12.3-nq1" ]; then
        if [ -f "$als_target" ]; then
            ln -sf "$als_target" "$als_link"
            if [ $VERBOSE -eq 1 ]; then
                echo "als-dimmer config linked to $(basename "$als_target") (for $config_type)"
            fi
        else
            if [ $VERBOSE -eq 1 ]; then
                echo "Warning: als-dimmer config not found: $als_target"
            fi
        fi
    fi
}

# Function to create complete configuration
create_config() {
    local config_type="$1"
    local output_file="$2"
    local temp_file=$(mktemp)

    if [ ! -f "$BASE_TEMPLATE" ]; then
        echo "Error: Base template not found: $BASE_TEMPLATE" >&2
        exit 1
    fi

    # Copy base template
    cat "$BASE_TEMPLATE" > "$temp_file"

    # Insert display-specific configuration after the base section
    sed -i '/# Display-specific section will be inserted here by the script/r /dev/stdin' "$temp_file" << EOF
$(generate_display_config "$config_type")
EOF

    # Remove the placeholder comment
    sed -i '/# Display-specific section will be inserted here by the script/d' "$temp_file"

    # For edid-hdmi, remove himax-touch overlay (not needed for direct HDMI)
    if [ "$config_type" = "edid-hdmi" ]; then
        sed -i '/dtoverlay=himax-touch/d' "$temp_file"
    fi

    # Copy to final destination
    cp "$temp_file" "$output_file"
    rm "$temp_file"

    # Configure HH983 serializer based on display type
    configure_hh983_serializer "$config_type"

    # Configure als-dimmer symlink based on display type
    configure_als_dimmer "$config_type"
}

# Function to extract config content for comparison
extract_config_content() {
    local file="$1"
    grep -E "^[^#].*" "$file" | sed '/^$/d' | sort
}

# Function to get reference config content for comparison
get_reference_config() {
    local config_type="$1"
    local temp_file=$(mktemp)

    create_config "$config_type" "$temp_file"
    extract_config_content "$temp_file"
    rm "$temp_file"
}

# Function to get configured resolution from config type
get_config_resolution() {
    local config_type="$1"

    if [ -f "$CONFIG_DATA_FILE" ]; then
        config_line=$(grep "^$config_type:" "$CONFIG_DATA_FILE" | head -1)
        if [ -n "$config_line" ]; then
            echo "$config_line" | cut -d: -f2
            return 0
        fi
    fi

    echo "unknown"
}

# Function to get expected hh983 config_mode for a display type
get_expected_hh983_mode() {
    local config_type="$1"
    if [ "$config_type" = "15.6-2k5" ] || [ "$config_type" = "12.3-nq1" ]; then
        echo "0"
    else
        echo "1"
    fi
}

# Function to read and identify current configuration
read_current_config() {
    local current_content=$(extract_config_content "$INPUT_FILE")
    local hh983_conf="/etc/modprobe.d/hh983.conf"
    local current_hh983_mode="1"
    if [ -f "$hh983_conf" ]; then
        current_hh983_mode=$(grep -o 'config_mode=[0-9]*' "$hh983_conf" | head -1 | cut -d= -f2)
        current_hh983_mode="${current_hh983_mode:-1}"
    fi

    # Check against all known configurations.
    # Multiple display types can produce identical config.txt (e.g. 12.3 and
    # 12.3-nq1 share the same timings).  When config.txt matches, also verify
    # the hh983 config_mode to pick the correct type.
    for type in $(get_supported_types); do
        local reference_content=$(get_reference_config "$type")

        if [ "$current_content" = "$reference_content" ]; then
            local expected_mode=$(get_expected_hh983_mode "$type")
            if [ "$expected_mode" = "$current_hh983_mode" ]; then
                echo "$type"
                return 0
            fi
        fi
    done

    echo "unknown"
}

# Function to create backup of the current config file
create_backup() {
    TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    BACKUP_FILE="${INPUT_FILE}.backup.${TIMESTAMP}"
    cp "$INPUT_FILE" "$BACKUP_FILE"
    if [ $VERBOSE -eq 1 ]; then
        echo "Backup created: $BACKUP_FILE"
    fi
}

# Function to find and ensure config.txt is accessible
find_config_file() {
    local input_file="$1"

    # If user specified a file, try to use it directly first
    if [ -n "$input_file" ]; then
        if [ -f "$input_file" ]; then
            echo "$input_file"
            return 0
        fi
    fi

    # Standard locations to check
    local config_paths="/boot/firmware/config.txt /boot/config.txt"

    # Check if any standard location exists
    for path in $config_paths; do
        if [ -f "$path" ]; then
            echo "$path"
            return 0
        fi
    done

    # If we get here, try to mount /boot for buildroot environments
    if [ ! -f "/boot/config.txt" ] && [ -b "/dev/mmcblk0p1" ]; then
        if [ "$(id -u)" -eq 0 ]; then
            # Check if /boot is already mounted before attempting to mount
            if ! mountpoint -q /boot 2>/dev/null; then
                # Try to mount the boot partition
                mkdir -p /boot 2>/dev/null || true
                mount /dev/mmcblk0p1 /boot 2>/dev/null

                if [ $VERBOSE -eq 1 ]; then
                    echo "Mounted /dev/mmcblk0p1 to /boot"
                fi
            fi

            if [ -f "/boot/config.txt" ]; then
                echo "/boot/config.txt"
                return 0
            fi
        else
            echo "Error: Boot partition not mounted and not running as root to mount it" >&2
            echo "Try: sudo mount /dev/mmcblk0p1 /boot" >&2
            exit 1
        fi
    fi

    # If user specified a file but it doesn't exist, return it anyway for proper error handling
    if [ -n "$input_file" ]; then
        echo "$input_file"
        return 1
    fi

    # Default fallback
    echo "/boot/firmware/config.txt"
    return 1
}

# Parse command line arguments
INPUT_FILE=""
DISPLAY_TYPE=""
QUERY_CONFIG=0
QUERY_DISPLAY=0
VERBOSE=0

for arg in "$@"; do
    case $arg in
        --input=*)
            INPUT_FILE="${arg#*=}"
            shift
            ;;
        --type=*)
            DISPLAY_TYPE="${arg#*=}"
            shift
            ;;
        --configspath=*)
            CONFIG_DIR="${arg#*=}"
            CONFIG_DATA_FILE="$CONFIG_DIR/display-configs.conf"
            BASE_TEMPLATE="$CONFIG_DIR/config-base.txt.in"
            shift
            ;;
        --query-config)
            QUERY_CONFIG=1
            shift
            ;;
        --query-display)
            QUERY_DISPLAY=1
            shift
            ;;
        -v|--verbose)
            VERBOSE=1
            shift
            ;;
        *)
            usage
            ;;
    esac
done

# Check for valid command mode
if [ -z "$INPUT_FILE" ] && [ -z "$DISPLAY_TYPE" ] && [ $QUERY_CONFIG -eq 0 ] && [ $QUERY_DISPLAY -eq 0 ]; then
    echo "Error: No operation specified"
    usage
fi

# Auto-detect config file if not specified
if [ -z "$INPUT_FILE" ] && ([ -n "$DISPLAY_TYPE" ] || [ $QUERY_CONFIG -eq 1 ]); then
    INPUT_FILE=$(find_config_file "")
fi

# If --input is provided without --query-config, enable query-config mode by default
if [ -n "$INPUT_FILE" ] && [ -z "$DISPLAY_TYPE" ] && [ $QUERY_CONFIG -eq 0 ] && [ $QUERY_DISPLAY -eq 0 ]; then
    QUERY_CONFIG=1
fi

# Resolve and validate input file
if [ -n "$INPUT_FILE" ]; then
    RESOLVED_INPUT_FILE=$(find_config_file "$INPUT_FILE")
    if [ ! -f "$RESOLVED_INPUT_FILE" ]; then
        echo "Error: Input file $RESOLVED_INPUT_FILE does not exist"
        exit 1
    fi
    INPUT_FILE="$RESOLVED_INPUT_FILE"
fi

# Include the original get_current_resolution function (unchanged for compatibility)
get_current_resolution() {
    # [The original 200+ line function stays exactly the same]
    # Method 1: Try tvservice first (most reliable on Pi without X)
    if command -v tvservice > /dev/null 2>&1; then
        # Get status and check if HDMI is connected and powered
        tvstatus=$(tvservice -s 2>/dev/null)
        tvstatus_rc=$?
        if [ $tvstatus_rc -eq 0 ] && echo "$tvstatus" | grep -q "HDMI" && ! echo "$tvstatus" | grep -q "off"; then
            # Extract resolution
            resolution=$(echo "$tvstatus" | grep -o '[0-9]\+x[0-9]\+')
            if [ -n "$resolution" ]; then
                if [ $VERBOSE -eq 1 ]; then
                    # Try to extract refresh rate
                    refresh=$(echo "$tvstatus" | grep -o '@[ ]*[0-9]\+' | grep -o '[0-9]\+')
                    if [ -n "$refresh" ]; then
                        echo "${resolution}@${refresh}Hz"
                    else
                        echo "${resolution}"
                    fi
                else
                    echo "${resolution}"
                fi
                return 0
            fi
        fi

        # If tvservice reports connected but without resolution, try getting more details
        if [ $tvstatus_rc -eq 0 ] && echo "$tvstatus" | grep -q "HDMI" ]; then
            tvdetails=$(tvservice -v 2>/dev/null)
            if [ $? -eq 0 ]; then
                # Look for DMT/CEA mode details that contain resolution
                resolution=$(echo "$tvdetails" | grep -o '[0-9]\+x[0-9]\+')
                if [ -n "$resolution" ]; then
                    echo "${resolution}"
                    return 0
                fi
            fi

            # Try to get modes directly
            tvmodes=$(tvservice -m DMT 2>/dev/null)
            if [ $? -eq 0 ]; then
                # Get the first preferred mode
                preferred=$(echo "$tvmodes" | grep "preferred" | head -n 1)
                if [ -n "$preferred" ]; then
                    resolution=$(echo "$preferred" | grep -o '[0-9]\+x[0-9]\+')
                    if [ -n "$resolution" ]; then
                        echo "${resolution}"
                        return 0
                    fi
                fi
            fi
        fi
    fi

    # Method 2: Check config.txt values directly
    if [ -f "/boot/firmware/config.txt" ]; then
        # Look for framebuffer settings
        fbw=$(grep -E "^framebuffer_width=" "/boot/firmware/config.txt" | cut -d'=' -f2)
        fbh=$(grep -E "^framebuffer_height=" "/boot/firmware/config.txt" | cut -d'=' -f2)
        if [ -n "$fbw" ] && [ -n "$fbh" ] && [ "$fbw" != "0" ] && [ "$fbh" != "0" ]; then
            echo "${fbw}x${fbh}"
            return 0
        fi

        # Look for hdmi_timings and extract resolution
        hdmi_timings=$(grep -E "^hdmi_timings=" "/boot/firmware/config.txt")
        if [ -n "$hdmi_timings" ]; then
            timings_values=$(echo "$hdmi_timings" | cut -d'=' -f2)
            width=$(echo "$timings_values" | awk '{print $1}')
            height=$(echo "$timings_values" | awk '{print $5}')
            if [ -n "$width" ] && [ -n "$height" ]; then
                echo "${width}x${height}"
                return 0
            fi
        fi
    fi

    # Method 3: Try vcgencmd methods
    if command -v vcgencmd > /dev/null 2>&1; then
        # Try direct hdmi_timings query
        timings=$(vcgencmd get_config hdmi_timings 2>/dev/null)
        if [ $? -eq 0 ] && [ -n "$timings" ] && ! echo "$timings" | grep -q "error"; then
            timing_val=$(echo "$timings" | cut -d'=' -f2)
            if [ -n "$timing_val" ]; then
                width=$(echo "$timing_val" | awk '{print $1}')
                height=$(echo "$timing_val" | awk '{print $5}')
                if [ -n "$width" ] && [ -n "$height" ] && [ "$width" != "0" ] && [ "$height" != "0" ]; then
                    echo "${width}x${height}"
                    return 0
                fi
            fi
        fi

        # Try to get display dimensions from framebuffer settings
        fbwidth=$(vcgencmd get_config framebuffer_width 2>/dev/null)
        if [ $? -eq 0 ] && [ -n "$fbwidth" ] && ! echo "$fbwidth" | grep -q "error"; then
            fbwidth=$(echo "$fbwidth" | cut -d'=' -f2)
            fbheight=$(vcgencmd get_config framebuffer_height 2>/dev/null)
            if [ $? -eq 0 ] && [ -n "$fbheight" ] && ! echo "$fbheight" | grep -q "error"; then
                fbheight=$(echo "$fbheight" | cut -d'=' -f2)
                if [ -n "$fbwidth" ] && [ -n "$fbheight" ] && [ "$fbwidth" != "0" ] && [ "$fbheight" != "0" ]; then
                    echo "${fbwidth}x${fbheight}"
                    return 0
                fi
            fi
        fi
    fi

    # Method 4: Check for DRM info in sysfs
    for drm_path in /sys/class/drm/card*-HDMI-A-1 /sys/class/drm/card*-HDMI-A-0; do
        if [ -e "$drm_path/status" ]; then
            status=$(cat "$drm_path/status" 2>/dev/null)
            if [ "$status" = "connected" ] && [ -e "$drm_path/modes" ]; then
                mode=$(head -n 1 "$drm_path/modes" 2>/dev/null)
                if [ -n "$mode" ]; then
                    echo "$mode"
                    return 0
                fi
            fi
        fi
    done

    # Method 5: Try reading directly from /dev/fb0
    if [ -e "/dev/fb0" ] && command -v fbset > /dev/null 2>&1; then
        fbinfo=$(fbset -i 2>/dev/null)
        if [ $? -eq 0 ]; then
            geometry=$(echo "$fbinfo" | grep "geometry" | head -n 1)
            if [ -n "$geometry" ]; then
                width=$(echo "$geometry" | awk '{print $2}')
                height=$(echo "$geometry" | awk '{print $3}')
                if [ -n "$width" ] && [ -n "$height" ] && [ "$width" != "0" ] && [ "$height" != "0" ]; then
                    echo "${width}x${height}"
                    return 0
                fi
            fi
        fi
    fi

    echo "unknown"
    return 1
}

# Main execution follows the same pattern as the original
# Query Display Mode
if [ $QUERY_DISPLAY -eq 1 ]; then
    current_resolution=$(get_current_resolution)

    if [ $VERBOSE -eq 1 ]; then
        echo "Current display resolution: $current_resolution"
    else
        echo "$current_resolution"
    fi
    exit 0
fi

# Query Config Mode
if [ $QUERY_CONFIG -eq 1 ]; then
    current_config=$(read_current_config)

    if [ $VERBOSE -eq 1 ]; then
        echo "Current configuration: $current_config"

        config_resolution=$(get_config_resolution "$current_config")
        echo "Configured resolution: $config_resolution"

        if [ $QUERY_DISPLAY -eq 1 ]; then
            current_resolution=$(get_current_resolution)
            echo "Actual display resolution: $current_resolution"

            if [ "$config_resolution" = "$current_resolution" ] ||
               ([ "$config_resolution" = "edid" ] && [ "$current_resolution" != "unknown" ]); then
                echo "Status: Configuration matches display output"
            else
                echo "Status: Configuration does NOT match display output"
            fi
        fi
    else
        echo "$current_config"
    fi
    exit 0
fi

# Write Config Mode
if [ -n "$DISPLAY_TYPE" ] && [ -n "$INPUT_FILE" ]; then
    # Check if running as root or with sudo
    if [ "$(id -u)" -ne 0 ]; then
        echo "Error: This script must be run as root or with sudo for write operations"
        exit 1
    fi

    # Check if the script has write access to the config file
    if [ ! -w "$INPUT_FILE" ]; then
        echo "Error: No write access to $INPUT_FILE"
        exit 1
    fi

    # Validate display type against supported types
    supported_types=$(get_supported_types | tr '\n' '|' | sed 's/|$//')
    if ! echo "$supported_types" | grep -q "\\b$DISPLAY_TYPE\\b"; then
        echo "Error: Invalid display type. Valid types are: $(echo "$supported_types" | tr '|' ', ')"
        usage
    fi

    if [ $VERBOSE -eq 1 ]; then
        load_display_config "$DISPLAY_TYPE"
        echo "Updating $INPUT_FILE for display type: $DISPLAY_TYPE ($DESCRIPTION)"
    fi

    create_backup
    create_config "$DISPLAY_TYPE" "$INPUT_FILE"

    if [ $VERBOSE -eq 1 ]; then
        echo "Configuration update completed successfully"
        echo "A reboot may be required for changes to take effect"
    fi

    reboot  # trigger reboot so that changed timing can take effect
    exit 0
fi

# If we get here, no valid operation was specified
echo "Error: Invalid combination of arguments"
usage
