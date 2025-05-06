#!/bin/sh
# POSIX-compliant shell script to work with dash/sh

# Script to update or read /boot/firmware/config.txt for specified display types
# Usage: 
#   - Write mode: /usr/bin/pi-config-txt.sh --input=/boot/firmware/config.txt --type=<14.6/15.6/27/edid>
#   - Read config: /usr/bin/pi-config-txt.sh --input=/boot/firmware/config.txt [--query-config] [-v]
#   - Query display: /usr/bin/pi-config-txt.sh --query-display [-v]

# Function to display usage
usage() {
    echo "Usage:"
    echo "  Write config: $0 --input=/boot/firmware/config.txt --type=<14.6/15.6/27/edid>"
    echo "  Read config: $0 --input=/boot/firmware/config.txt [--query-config] [-v]"
    echo "  Query display: $0 --query-display [-v]"
    echo ""
    echo "Options:"
    echo "  --input=FILE    Specify the configuration file path"
    echo "  --type=TYPE     Configure for display type (14.6, 15.6, 27, edid)"
    echo "  --query-config  Read and output configured display type/resolution"
    echo "  --query-display Query actual display resolution on HDMI output"
    echo "  -v, --verbose   Show detailed information"
    exit 1
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

# If --input is provided without --query-config, enable query-config mode by default
if [ -n "$INPUT_FILE" ] && [ -z "$DISPLAY_TYPE" ] && [ $QUERY_CONFIG -eq 0 ] && [ $QUERY_DISPLAY -eq 0 ]; then
    QUERY_CONFIG=1
fi

# Check if input file exists when needed
if [ -n "$INPUT_FILE" ] && [ ! -f "$INPUT_FILE" ]; then
    echo "Error: Input file $INPUT_FILE does not exist"
    exit 1
fi

# Function to extract config content into a format for comparison
extract_config_content() {
    local file="$1"
    # Remove comments and empty lines, sort for consistent comparison
    grep -E "^[^#].*" "$file" | sed '/^$/d' | sort
}

# Function to get reference config content for each type
get_reference_config() {
    local config_type="$1"
    local temp_file=$(mktemp)
    
    case $config_type in
        "14.6")
            cat > "$temp_file" << 'EOF'
dtparam=audio=on
camera_auto_detect=1
auto_initramfs=1
disable_overscan=1
arm_64bit=1
arm_boost=1
dtoverlay=i2c1=on
dtparam=i2c_arm=on
dtoverlay=vc4-fkms-v3d
disable_fw_kms_setup=1
max_framebuffers=2
hdmi_group=2
hdmi_mode=87
hdmi_timings=2560 0 10 18 216 1440 0 10 4 330 0 0 0 60 0 300000000 4
hdmi_pixel_freq_limit=300000000
framebuffer_width=2560
framebuffer_height=1440
max_framebuffer_width=2560
max_framebuffer_height=1440
config_hdmi_boost=4
[cm4]
otg_mode=1
[cm5]
dtoverlay=dwc2,dr_mode=host
[all]
EOF
            ;;
        "15.6")
            cat > "$temp_file" << 'EOF'
dtparam=audio=on
camera_auto_detect=1
auto_initramfs=1
disable_overscan=1
arm_64bit=1
arm_boost=1
dtoverlay=i2c1=on
dtparam=i2c_arm=on
dtoverlay=vc4-fkms-v3d
disable_fw_kms_setup=1
max_framebuffers=2
hdmi_group=2
hdmi_mode=87
hdmi_timings=2560 0 10 24 222 1440 0 11 3 38 0 0 0 62 0 261888000 4
hdmi_pixel_freq_limit=261888000
framebuffer_width=2560
framebuffer_height=1440
max_framebuffer_width=2560
max_framebuffer_height=1440
config_hdmi_boost=4
[cm4]
otg_mode=1
[cm5]
dtoverlay=dwc2,dr_mode=host
[all]
EOF
            ;;
        "27")
            cat > "$temp_file" << 'EOF'
dtparam=audio=on
camera_auto_detect=1
auto_initramfs=1
disable_overscan=1
arm_64bit=1
arm_boost=1
dtoverlay=i2c1=on
dtparam=i2c_arm=on
dtoverlay=vc4-fkms-v3d
disable_fw_kms_setup=1
max_framebuffers=2
hdmi_group=2
hdmi_mode=87
hdmi_timings=4032 0 72 72 72 756 0 12 2 16 0 0 0 62 0 207000000 4
hdmi_pixel_freq_limit=207000000
framebuffer_width=4032
framebuffer_height=756
max_framebuffer_width=4032
max_framebuffer_height=756
config_hdmi_boost=4
[cm4]
otg_mode=1
[cm5]
dtoverlay=dwc2,dr_mode=host
[all]
EOF
            ;;
        "edid")
            cat > "$temp_file" << 'EOF'
dtparam=audio=on
camera_auto_detect=1
auto_initramfs=1
disable_overscan=1
arm_64bit=1
arm_boost=1
dtoverlay=i2c1=on
dtparam=i2c_arm=on
dtoverlay=vc4-fkms-v3d
disable_fw_kms_setup=1
max_framebuffers=2
display_auto_detect=1
[cm4]
otg_mode=1
[cm5]
dtoverlay=dwc2,dr_mode=host
[all]
EOF
            ;;
    esac
    
    extract_config_content "$temp_file"
    rm "$temp_file"
}

# Function to get configured resolution from config type
get_config_resolution() {
    local config_type="$1"
    
    case $config_type in
        "14.6")
            echo "2560x1440"
            ;;
        "15.6")
            echo "2560x1440"
            ;;
        "27")
            echo "4032x756"
            ;;
        "edid")
            echo "edid"
            ;;
        *)
            echo "unknown"
            ;;
    esac
}

# Function to read and identify current configuration
read_current_config() {
    local current_content=$(extract_config_content "$INPUT_FILE")
    
    for type in "14.6" "15.6" "27" "edid"; do
        local reference_content=$(get_reference_config "$type")
        
        if [ "$current_content" = "$reference_content" ]; then
            echo "$type"
            return 0
        fi
    done
    
    echo "unknown"
}

# Function to query current display resolution - POSIX shell compatible
get_current_resolution() {
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
                    if [ $VERBOSE -eq 1 ]; then
                        echo "${resolution}"
                    else
                        echo "${resolution}"
                    fi
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
    
    # Method 2: Check config.txt values directly in a different way
    if [ -f "/boot/firmware/config.txt" ]; then
        # Look for framebuffer settings
        fbw=$(grep -E "^framebuffer_width=" "/boot/firmware/config.txt" | cut -d'=' -f2)
        fbh=$(grep -E "^framebuffer_height=" "/boot/firmware/config.txt" | cut -d'=' -f2)
        if [ -n "$fbw" ] && [ -n "$fbh" ] && [ "$fbw" != "0" ] && [ "$fbh" != "0" ]; then
            echo "${fbw}x${fbh}"
            return 0
        fi
        
        # Look for hdmi_mode and decode it
        hdmi_group=$(grep -E "^hdmi_group=" "/boot/firmware/config.txt" | cut -d'=' -f2)
        hdmi_mode=$(grep -E "^hdmi_mode=" "/boot/firmware/config.txt" | cut -d'=' -f2)
        if [ -n "$hdmi_group" ] && [ -n "$hdmi_mode" ]; then
            # This is a simplified lookup - in a real implementation we would 
            # have complete tables for CEA and DMT modes
            if [ "$hdmi_group" = "1" ] && [ "$hdmi_mode" = "16" ]; then
                echo "1920x1080"
                return 0
            elif [ "$hdmi_group" = "1" ] && [ "$hdmi_mode" = "4" ]; then
                echo "1280x720"
                return 0
            elif [ "$hdmi_group" = "2" ] && [ "$hdmi_mode" = "35" ]; then
                echo "1280x1024"
                return 0
            elif [ "$hdmi_group" = "2" ] && [ "$hdmi_mode" = "16" ]; then
                echo "1024x768"
                return 0
            fi
        fi
        
        # Look for hdmi_timings which contains resolution info
        hdmi_timings=$(grep -E "^hdmi_timings=" "/boot/firmware/config.txt")
        if [ -n "$hdmi_timings" ]; then
            # Extract first two numbers from timings which are width and height
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
            # Get first and fifth values which are width and height
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
                # Get first (highest) mode
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
    
    # As a fallback, detect if we're using a custom display from config.txt
    if [ -f "/boot/firmware/config.txt" ]; then
        if grep -q "hdmi_timings=2560 0 10 18 216 1440 0 10 4 330 0 0 0 60 0 300000000 4" "/boot/firmware/config.txt"; then
            echo "2560x1440"
            return 0
        elif grep -q "hdmi_timings=2560 0 10 24 222 1440 0 11 3 38 0 0 0 62 0 261888000 4" "/boot/firmware/config.txt"; then
            echo "2560x1440"
            return 0
        elif grep -q "hdmi_timings=4032 0 72 72 72 756 0 12 2 16 0 0 0 62 0 207000000 4" "/boot/firmware/config.txt"; then
            echo "4032x756"
            return 0
        fi
    fi
    
    echo "unknown"
    return 1
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

# Function to write config based on display type
write_config() {
    local config_type="$1"
    local output_file="$2"
    
    case $config_type in
        "14.6")
            cat > "$output_file" << 'EOF'
dtparam=audio=on
camera_auto_detect=1
auto_initramfs=1
disable_overscan=1
arm_64bit=1
arm_boost=1
dtoverlay=i2c1=on
dtparam=i2c_arm=on
dtoverlay=vc4-fkms-v3d
disable_fw_kms_setup=1
max_framebuffers=2
hdmi_group=2
hdmi_mode=87
hdmi_timings=2560 0 10 18 216 1440 0 10 4 330 0 0 0 60 0 300000000 4
hdmi_pixel_freq_limit=300000000
framebuffer_width=2560
framebuffer_height=1440
max_framebuffer_width=2560
max_framebuffer_height=1440
config_hdmi_boost=4
[cm4]
otg_mode=1
[cm5]
dtoverlay=dwc2,dr_mode=host
[all]
EOF
            if [ $VERBOSE -eq 1 ]; then
                echo "Updated config for 14.6\" display"
            fi
            ;;
        "15.6")
            cat > "$output_file" << 'EOF'
dtparam=audio=on
camera_auto_detect=1
auto_initramfs=1
disable_overscan=1
arm_64bit=1
arm_boost=1
dtoverlay=i2c1=on
dtparam=i2c_arm=on
dtoverlay=vc4-fkms-v3d
disable_fw_kms_setup=1
max_framebuffers=2
hdmi_group=2
hdmi_mode=87
hdmi_timings=2560 0 10 24 222 1440 0 11 3 38 0 0 0 62 0 261888000 4
hdmi_pixel_freq_limit=261888000
framebuffer_width=2560
framebuffer_height=1440
max_framebuffer_width=2560
max_framebuffer_height=1440
config_hdmi_boost=4
[cm4]
otg_mode=1
[cm5]
dtoverlay=dwc2,dr_mode=host
[all]
EOF
            if [ $VERBOSE -eq 1 ]; then
                echo "Updated config for 15.6\" display"
            fi
            ;;
        "27")
            cat > "$output_file" << 'EOF'
dtparam=audio=on
camera_auto_detect=1
auto_initramfs=1
disable_overscan=1
arm_64bit=1
arm_boost=1
dtoverlay=i2c1=on
dtparam=i2c_arm=on
dtoverlay=vc4-fkms-v3d
disable_fw_kms_setup=1
max_framebuffers=2
hdmi_group=2
hdmi_mode=87
hdmi_timings=4032 0 72 72 72 756 0 12 2 16 0 0 0 62 0 207000000 4
hdmi_pixel_freq_limit=207000000
framebuffer_width=4032
framebuffer_height=756
max_framebuffer_width=4032
max_framebuffer_height=756
config_hdmi_boost=4
[cm4]
otg_mode=1
[cm5]
dtoverlay=dwc2,dr_mode=host
[all]
EOF
            if [ $VERBOSE -eq 1 ]; then
                echo "Updated config for 27\" display"
            fi
            ;;
        "edid")
            cat > "$output_file" << 'EOF'
dtparam=audio=on
camera_auto_detect=1
auto_initramfs=1
disable_overscan=1
arm_64bit=1
arm_boost=1
dtoverlay=i2c1=on
dtparam=i2c_arm=on
dtoverlay=vc4-fkms-v3d
disable_fw_kms_setup=1
max_framebuffers=2
display_auto_detect=1
[cm4]
otg_mode=1
[cm5]
dtoverlay=dwc2,dr_mode=host
[all]
EOF
            if [ $VERBOSE -eq 1 ]; then
                echo "Updated config for EDID auto-detection display"
            fi
            ;;
        *)
            echo "Error: Invalid display type. Valid types are: 14.6, 15.6, 27, edid"
            exit 1
            ;;
    esac
}

# Main execution
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
        # Just output the display type identifier (not the resolution)
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
    
    case $DISPLAY_TYPE in
        "14.6"|"15.6"|"27"|"edid")
            if [ $VERBOSE -eq 1 ]; then
                echo "Updating $INPUT_FILE for display type: $DISPLAY_TYPE"
            fi
            create_backup
            write_config "$DISPLAY_TYPE" "$INPUT_FILE"
            if [ $VERBOSE -eq 1 ]; then
                echo "Configuration update completed successfully"
                echo "A reboot may be required for changes to take effect"
            fi
            reboot #trigger reboot so that changed timing can take effect
            ;;
        *)
            echo "Error: Invalid display type. Valid types are: 14.6, 15.6, 27, edid"
            usage
            ;;
    esac
    exit 0
fi

# If we get here, no valid operation was specified
echo "Error: Invalid combination of arguments"
usage
