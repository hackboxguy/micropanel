#!/bin/bash

# Script to update or read /boot/firmware/config.txt for specified display types
# Usage: /usr/bin/pi-config-txt.sh --input=/boot/firmware/config.txt --type=<14.6/15.6/27/edid>
#    or: /usr/bin/pi-config-txt.sh --input=/boot/firmware/config.txt (to read current config)

# Function to display usage
usage() {
    echo "Usage: $0 --input=/boot/firmware/config.txt [--type=<14.6/15.6/27/edid>]"
    echo "Types: 14.6 (14.6\" display), 15.6 (15.6\" display), 27 (27\" display), edid (auto-detect)"
    echo "If --type is not specified, the script will read and identify the current configuration"
    exit 1
}

# Parse command line arguments
INPUT_FILE=""
DISPLAY_TYPE=""

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
        *)
            usage
            ;;
    esac
done

# Check if required arguments are provided
if [ -z "$INPUT_FILE" ]; then
    echo "Error: Missing required --input argument"
    usage
fi

# Check if the input file exists
if [ ! -f "$INPUT_FILE" ]; then
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

# Function to create backup of the current config file
create_backup() {
    TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    BACKUP_FILE="${INPUT_FILE}.backup.${TIMESTAMP}"
    cp "$INPUT_FILE" "$BACKUP_FILE"
    #echo "Backup created: $BACKUP_FILE"
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
            #echo "Updated config for 14.6\" display"
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
            #echo "Updated config for 15.6\" display"
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
            #echo "Updated config for 27\" display"
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
            #echo "Updated config for EDID auto-detection display"
            ;;
        *)
            echo "Error: Invalid display type. Valid types are: 14.6, 15.6, 27, edid"
            exit 1
            ;;
    esac
}

# Main execution
if [ -z "$DISPLAY_TYPE" ]; then
    # Read mode - no type specified
    current_config=$(read_current_config)
    echo "$current_config"
else
    # Write mode - check if running as root or with sudo
    if [ "$EUID" -ne 0 ]; then 
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
            #echo "Updating $INPUT_FILE for display type: $DISPLAY_TYPE"
            create_backup
            write_config "$DISPLAY_TYPE" "$INPUT_FILE"
            reboot #trigger reboot to apply new hdmi timings
            #echo "Configuration update completed successfully"
            #echo "A reboot may be required for changes to take effect"
            ;;
        *)
            echo "Error: Invalid display type. Valid types are: 14.6, 15.6, 27, edid"
            usage
            ;;
    esac
fi

exit 0
