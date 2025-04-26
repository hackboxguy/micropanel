#!/bin/sh
# dhcp-net-settings.sh - Main script to manage network address settings
# Usage:
#   Get current settings:  ./dhcp-net-settings.sh --interface=eth0 --os=debian
#   Set to DHCP:          ./dhcp-net-settings.sh --interface=eth0 --os=debian --mode=dhcp
#   Set to static:        ./dhcp-net-settings.sh --interface=eth0 --os=debian --mode=static --ip=192.168.1.2 --gateway=192.168.1.1 --netmask=255.255.255.0

# Default values
SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"
BACKUP_PATH="/tmp/net-settings-bkup"
VERBOSE=0
DRY_RUN=0
RESULT="ERROR"
MESSAGE=""

# Function to output messages when verbose mode is enabled
log_verbose() {
    if [ "$VERBOSE" -eq 1 ]; then
        echo "[INFO] $1"
    fi
}

# Function to output error messages and exit
error_exit() {
    echo "[ERROR] $1"
    echo "RESULT:$RESULT"
    exit 1
}

# Function to validate IP address format
validate_ip() {
    ip=$1
    # Check if IP has the correct format (simple regex for IPv4)
    if ! echo "$ip" | grep -E '^([0-9]{1,3}\.){3}[0-9]{1,3}$' > /dev/null; then
        return 1
    fi

    # Check if each octet is between 0 and 255
    for octet in $(echo "$ip" | tr '.' ' '); do
        if [ "$octet" -lt 0 ] || [ "$octet" -gt 255 ]; then
            return 1
        fi
    done

    return 0
}

# Function to validate netmask
validate_netmask() {
    netmask=$1
    # First validate as an IP address
    if ! validate_ip "$netmask"; then
        return 1
    fi

    # This is a simplified check and might not catch all invalid netmasks
    return 0
}

# Function to check if interface exists
check_interface() {
    if ! ip link show "$INTERFACE" > /dev/null 2>&1; then
        error_exit "Interface $INTERFACE does not exist"
    fi
}

# Function to check if we're running as root
check_root() {
    if [ "$(id -u)" -ne 0 ]; then
        error_exit "This script must be run as root"
    fi
}

# Function to create backup directory
create_backup() {
    if [ ! -d "$BACKUP_PATH" ]; then
        log_verbose "Creating backup directory: $BACKUP_PATH"
        if [ "$DRY_RUN" -eq 0 ]; then
            mkdir -p "$BACKUP_PATH" || error_exit "Failed to create backup directory"
        else
            log_verbose "[DRY-RUN] Would create directory: $BACKUP_PATH"
        fi
    fi
}

# Parse command line arguments
parse_args() {
    # Default values
    OS=""
    INTERFACE=""
    MODE=""
    IP=""
    GATEWAY=""
    NETMASK=""
    DNS1=""
    DNS2=""
    DNS3=""

    for arg in "$@"; do
        case $arg in
            --os=*)
                OS="${arg#*=}"
                ;;
            --interface=*)
                INTERFACE="${arg#*=}"
                ;;
            --mode=*)
                MODE="${arg#*=}"
                ;;
            --ip=*)
                IP="${arg#*=}"
                ;;
            --gateway=*)
                GATEWAY="${arg#*=}"
                ;;
            --netmask=*)
                NETMASK="${arg#*=}"
                ;;
            --dns1=*)
                DNS1="${arg#*=}"
                ;;
            --dns2=*)
                DNS2="${arg#*=}"
                ;;
            --dns3=*)
                DNS3="${arg#*=}"
                ;;
            --backuppath=*)
                BACKUP_PATH="${arg#*=}"
                ;;
            --verbose)
                VERBOSE=1
                ;;
            --dry-run)
                DRY_RUN=1
                ;;
            *)
                error_exit "Unknown argument: $arg"
                ;;
        esac
    done

    # Validate required arguments
    if [ -z "$OS" ]; then
        error_exit "OS parameter is required (--os=openwrt|debian|pios|buildroot)"
    fi

    if [ -z "$INTERFACE" ]; then
        error_exit "Interface parameter is required (--interface=eth0)"
    fi

    # Validate OS parameter
    case "$OS" in
        openwrt|debian|pios|buildroot)
            log_verbose "OS: $OS"
            ;;
        *)
            error_exit "Unsupported OS: $OS. Must be one of: openwrt, debian, pios, buildroot"
            ;;
    esac

    # Validate mode parameter if provided
    if [ -n "$MODE" ]; then
        case "$MODE" in
            static|dhcp)
                log_verbose "Mode: $MODE"
                ;;
            *)
                error_exit "Unsupported mode: $MODE. Must be 'static' or 'dhcp'"
                ;;
        esac
    fi

    # Validate IP parameters if mode is static
    if [ "$MODE" = "static" ]; then
        if [ -z "$IP" ] || [ -z "$GATEWAY" ] || [ -z "$NETMASK" ]; then
            error_exit "Static mode requires IP, gateway, and netmask parameters"
        fi

        # Validate IP format
        if ! validate_ip "$IP"; then
            error_exit "Invalid IP address format: $IP"
        fi

        # Validate gateway format
        if ! validate_ip "$GATEWAY"; then
            error_exit "Invalid gateway address format: $GATEWAY"
        fi

        # Validate netmask format
        if ! validate_netmask "$NETMASK"; then
            error_exit "Invalid netmask format: $NETMASK"
        fi

        # Validate DNS if provided
        if [ -n "$DNS1" ]; then
            if ! validate_ip "$DNS1"; then
                error_exit "Invalid DNS1 address format: $DNS1"
            fi
        fi

        if [ -n "$DNS2" ]; then
            if ! validate_ip "$DNS2"; then
                error_exit "Invalid DNS2 address format: $DNS2"
            fi
        fi

        if [ -n "$DNS3" ]; then
            if ! validate_ip "$DNS3"; then
                error_exit "Invalid DNS3 address format: $DNS3"
            fi
        fi
    fi
}

# Main function
main() {
    # Parse command line arguments
    parse_args "$@"

    # Check if running as root
    check_root

    # Check if interface exists
    check_interface

    # Create backup directory
    create_backup

    # Determine OS-specific script
    OS_SCRIPT="$SCRIPT_DIR/dhcp-net-settings-$OS.sh"

    # Check if OS-specific script exists and is executable
    if [ ! -f "$OS_SCRIPT" ]; then
        error_exit "OS-specific script not found: $OS_SCRIPT"
    fi

    if [ ! -x "$OS_SCRIPT" ]; then
        error_exit "OS-specific script not executable: $OS_SCRIPT"
    fi

    # Export all relevant variables for the OS-specific script
    export INTERFACE
    export MODE
    export IP
    export GATEWAY
    export NETMASK
    export DNS1
    export DNS2
    export DNS3
    export BACKUP_PATH
    export VERBOSE
    export DRY_RUN

    # Call the OS-specific script
    "$OS_SCRIPT"

    # Exit with the exit code from the OS-specific script
    exit $?
}

# Execute main function with all arguments
main "$@"
