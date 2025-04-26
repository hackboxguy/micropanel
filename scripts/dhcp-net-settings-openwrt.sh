#!/bin/sh
# dhcp-net-settings-openwrt.sh - OpenWrt specific network configuration
# This script is called from the main dhcp-net-settings.sh script

# Check for required environment variables
if [ -z "$INTERFACE" ]; then
    echo "[ERROR] INTERFACE environment variable not set"
    echo "RESULT:ERROR"
    exit 1
fi

# Function to output messages when verbose mode is enabled
log_verbose() {
    if [ "$VERBOSE" -eq 1 ]; then
        echo "[INFO] $1"
    fi
}

# Function to output error messages and exit
error_exit() {
    echo "[ERROR] $1"
    echo "RESULT:ERROR"
    exit 1
}

# Function to clean IP addresses (remove leading zeros)
clean_ip() {
    echo "$1" | awk -F. '{printf "%d.%d.%d.%d", $1, $2, $3, $4}'
}

# Backup current settings
if [ "$DRY_RUN" -eq 0 ]; then
    uci export network > "$BACKUP_PATH/network.$(date +%Y%m%d%H%M%S).uci" || \
        error_exit "Failed to backup OpenWrt network configuration"
else
    log_verbose "[DRY-RUN] Would backup OpenWrt network configuration"
fi

# Get current settings
if [ -z "$MODE" ]; then
    # Check if interface exists in UCI config
    if ! uci get network."$INTERFACE" > /dev/null 2>&1; then
        error_exit "Interface $INTERFACE not found in UCI configuration"
    fi

    CURRENT_MODE=$(uci get network."$INTERFACE".proto 2>/dev/null)
    echo "mode=$CURRENT_MODE"

    if [ "$CURRENT_MODE" = "static" ]; then
        IP=$(uci get network."$INTERFACE".ipaddr 2>/dev/null)
        GATEWAY=$(uci get network."$INTERFACE".gateway 2>/dev/null)
        NETMASK=$(uci get network."$INTERFACE".netmask 2>/dev/null)

        echo "ip=$IP"
        echo "gateway=$GATEWAY"
        echo "netmask=$NETMASK"

        # Get DNS settings if available
        DNS1=$(uci get network."$INTERFACE".dns 2>/dev/null | cut -d ' ' -f1)
        DNS2=$(uci get network."$INTERFACE".dns 2>/dev/null | cut -d ' ' -f2)
        DNS3=$(uci get network."$INTERFACE".dns 2>/dev/null | cut -d ' ' -f3)

        [ -n "$DNS1" ] && echo "dns1=$DNS1"
        [ -n "$DNS2" ] && echo "dns2=$DNS2"
        [ -n "$DNS3" ] && echo "dns3=$DNS3"
    fi

    echo "RESULT:OK"
    exit 0
fi

# Set new settings
if [ "$MODE" = "dhcp" ]; then
    if [ "$DRY_RUN" -eq 0 ]; then
        uci set network."$INTERFACE".proto=dhcp
        uci commit network
        /etc/init.d/network restart
    else
        log_verbose "[DRY-RUN] Would set $INTERFACE to DHCP mode"
        log_verbose "[DRY-RUN] Would commit changes and restart network"
    fi
elif [ "$MODE" = "static" ]; then
    if [ -z "$IP" ] || [ -z "$GATEWAY" ] || [ -z "$NETMASK" ]; then
        error_exit "Static mode requires IP, gateway, and netmask parameters"
    fi

    # Clean IP addresses to remove leading zeros
    CLEAN_IP=$(clean_ip "$IP")
    CLEAN_GATEWAY=$(clean_ip "$GATEWAY")
    
    # Clean DNS addresses if provided
    if [ -n "$DNS1" ]; then
        CLEAN_DNS1=$(clean_ip "$DNS1")
    fi
    if [ -n "$DNS2" ]; then
        CLEAN_DNS2=$(clean_ip "$DNS2")
    fi
    if [ -n "$DNS3" ]; then
        CLEAN_DNS3=$(clean_ip "$DNS3")
    fi

    log_verbose "Cleaned IP address: $CLEAN_IP (was $IP)"
    log_verbose "Cleaned Gateway: $CLEAN_GATEWAY (was $GATEWAY)"
    [ -n "$DNS1" ] && log_verbose "Cleaned DNS1: $CLEAN_DNS1 (was $DNS1)"
    [ -n "$DNS2" ] && log_verbose "Cleaned DNS2: $CLEAN_DNS2 (was $DNS2)"
    [ -n "$DNS3" ] && log_verbose "Cleaned DNS3: $CLEAN_DNS3 (was $DNS3)"

    if [ "$DRY_RUN" -eq 0 ]; then
        uci set network."$INTERFACE".proto=static
        uci set network."$INTERFACE".ipaddr="$CLEAN_IP"
        uci set network."$INTERFACE".gateway="$CLEAN_GATEWAY"
        uci set network."$INTERFACE".netmask="$NETMASK"

        # Set DNS if provided
        DNS_SERVERS=""
        [ -n "$DNS1" ] && DNS_SERVERS="$CLEAN_DNS1"
        [ -n "$DNS2" ] && DNS_SERVERS="$DNS_SERVERS $CLEAN_DNS2"
        [ -n "$DNS3" ] && DNS_SERVERS="$DNS_SERVERS $CLEAN_DNS3"
        
        if [ -n "$DNS_SERVERS" ]; then
            uci set network."$INTERFACE".dns="$DNS_SERVERS"
        fi

        uci commit network
        /etc/init.d/network restart
    else
        log_verbose "[DRY-RUN] Would set $INTERFACE to static mode with:"
        log_verbose "[DRY-RUN]   IP: $CLEAN_IP"
        log_verbose "[DRY-RUN]   Gateway: $CLEAN_GATEWAY"
        log_verbose "[DRY-RUN]   Netmask: $NETMASK"
        [ -n "$DNS1" ] && log_verbose "[DRY-RUN]   DNS1: $CLEAN_DNS1"
        [ -n "$DNS2" ] && log_verbose "[DRY-RUN]   DNS2: $CLEAN_DNS2"
        [ -n "$DNS3" ] && log_verbose "[DRY-RUN]   DNS3: $CLEAN_DNS3"
        log_verbose "[DRY-RUN] Would commit changes and restart network"
    fi
else
    error_exit "Invalid mode: $MODE. Must be 'dhcp' or 'static'"
fi

echo "RESULT:OK"
exit 0
