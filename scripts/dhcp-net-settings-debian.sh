#!/bin/sh
# dhcp-net-settings-debian.sh - Debian/Ubuntu specific network configuration
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

# Define variables
INTERFACES_FILE="/etc/network/interfaces"
NETPLAN_DIR="/etc/netplan"
IS_NETPLAN=0

# Check if system uses netplan
if [ -d "$NETPLAN_DIR" ] && command -v netplan > /dev/null 2>&1; then
    IS_NETPLAN=1
    log_verbose "Detected Netplan configuration system"
else
    log_verbose "Using legacy interfaces configuration"
fi

# Backup current settings
if [ "$DRY_RUN" -eq 0 ]; then
    if [ "$IS_NETPLAN" -eq 1 ]; then
        for file in "$NETPLAN_DIR"/*.yaml; do
            if [ -f "$file" ]; then
                cp "$file" "$BACKUP_PATH/$(basename "$file").$(date +%Y%m%d%H%M%S)" || \
                    error_exit "Failed to backup Netplan configuration"
            fi
        done
    else
        cp "$INTERFACES_FILE" "$BACKUP_PATH/interfaces.$(date +%Y%m%d%H%M%S)" || \
            error_exit "Failed to backup interfaces file"
    fi
else
    log_verbose "[DRY-RUN] Would backup network configuration files"
fi

# Get current settings
if [ -z "$MODE" ]; then
    if [ "$IS_NETPLAN" -eq 1 ]; then
        # For netplan, we need to parse YAML which is complex in pure shell
        # This is a simplified approach
        if grep -q "$INTERFACE:" "$NETPLAN_DIR"/*.yaml 2>/dev/null; then
            if grep -q "dhcp4: true" "$NETPLAN_DIR"/*.yaml 2>/dev/null; then
                echo "mode=dhcp"
            else
                echo "mode=static"
                # Extract IP information (simplified)
                IP=$(grep -A5 "$INTERFACE:" "$NETPLAN_DIR"/*.yaml 2>/dev/null | grep -o "addresses:.*" | cut -d':' -f2 | tr -d '[]" ' | cut -d'/' -f1)
                GATEWAY=$(grep -A10 "$INTERFACE:" "$NETPLAN_DIR"/*.yaml 2>/dev/null | grep -o "gateway4:.*" | cut -d':' -f2 | tr -d ' ')
                # Netmask is part of CIDR notation in netplan

                echo "ip=$IP"
                echo "gateway=$GATEWAY"
                # We can't easily extract netmask from CIDR notation in shell

                # Extract DNS information (simplified)
                DNS=$(grep -A15 "$INTERFACE:" "$NETPLAN_DIR"/*.yaml 2>/dev/null | grep -A5 "nameservers:" | grep -o "addresses:.*" | cut -d':' -f2 | tr -d '[]" ')
                DNS1=$(echo "$DNS" | cut -d' ' -f1)
                DNS2=$(echo "$DNS" | cut -d' ' -f2)
                DNS3=$(echo "$DNS" | cut -d' ' -f3)

                [ -n "$DNS1" ] && echo "dns1=$DNS1"
                [ -n "$DNS2" ] && echo "dns2=$DNS2"
                [ -n "$DNS3" ] && echo "dns3=$DNS3"
            fi
        else
            # Interface not found in Netplan, try to determine current mode from system
            # Check if the interface has an IP address
            CURRENT_IP=$(ip -4 addr show dev "$INTERFACE" 2>/dev/null | grep -o 'inet [0-9.]*' | cut -d' ' -f2)

            if [ -n "$CURRENT_IP" ]; then
                # Look for DHCP leases to determine if using DHCP
                if grep -q "$INTERFACE" /var/lib/dhcp/dhclient*.leases 2>/dev/null || \
                   ps aux | grep -q "dhclient.*$INTERFACE" || \
                   [ -f "/var/lib/NetworkManager/dhclient-$INTERFACE.conf" ]; then
                    echo "mode=dhcp"
                else
                    echo "mode=static"
                    echo "ip=$CURRENT_IP"

                    # Get gateway
                    GATEWAY=$(ip route | grep "default.*$INTERFACE" | cut -d' ' -f3)
                    echo "gateway=$GATEWAY"

                    # Get netmask (might be in CIDR format)
                    NETMASK=$(ip -4 addr show dev "$INTERFACE" | grep -o 'inet [0-9.]*\/[0-9]*' | cut -d'/' -f2)
                    if [ -n "$NETMASK" ]; then
                        # Convert CIDR to netmask (simplified)
                        case "$NETMASK" in
                            8) NETMASK="255.0.0.0" ;;
                            16) NETMASK="255.255.0.0" ;;
                            24) NETMASK="255.255.255.0" ;;
                            *)  NETMASK="255.255.255.0" ;; # Default
                        esac
                        echo "netmask=$NETMASK"
                    fi

                    # Get DNS from resolv.conf
                    if [ -f "/etc/resolv.conf" ]; then
                        DNS1=$(grep "nameserver" "/etc/resolv.conf" | head -1 | awk '{print $2}')
                        DNS2=$(grep "nameserver" "/etc/resolv.conf" | head -2 | tail -1 | awk '{print $2}')
                        DNS3=$(grep "nameserver" "/etc/resolv.conf" | head -3 | tail -1 | awk '{print $2}')

                        [ -n "$DNS1" ] && echo "dns1=$DNS1"
                        [ -n "$DNS2" ] && echo "dns2=$DNS2"
                        [ -n "$DNS3" ] && echo "dns3=$DNS3"
                    fi
                fi
            else
                echo "mode=dhcp"  # Default to DHCP if no IP is configured
            fi
        fi
    else
        # For legacy interfaces file
        if grep -q "iface $INTERFACE" "$INTERFACES_FILE" 2>/dev/null; then
            if grep -q "iface $INTERFACE inet dhcp" "$INTERFACES_FILE" 2>/dev/null; then
                echo "mode=dhcp"
            else
                echo "mode=static"
                # Extract IP information
                IP=$(grep -A10 "iface $INTERFACE inet static" "$INTERFACES_FILE" 2>/dev/null | grep -o "address.*" | cut -d' ' -f2)
                GATEWAY=$(grep -A10 "iface $INTERFACE inet static" "$INTERFACES_FILE" 2>/dev/null | grep -o "gateway.*" | cut -d' ' -f2)
                NETMASK=$(grep -A10 "iface $INTERFACE inet static" "$INTERFACES_FILE" 2>/dev/null | grep -o "netmask.*" | cut -d' ' -f2)

                echo "ip=$IP"
                echo "gateway=$GATEWAY"
                echo "netmask=$NETMASK"

                # Extract DNS information
                DNS=$(grep -A10 "iface $INTERFACE inet static" "$INTERFACES_FILE" 2>/dev/null | grep -o "dns-nameservers.*" | cut -d' ' -f2-)
                DNS1=$(echo "$DNS" | cut -d' ' -f1)
                DNS2=$(echo "$DNS" | cut -d' ' -f2)
                DNS3=$(echo "$DNS" | cut -d' ' -f3)

                [ -n "$DNS1" ] && echo "dns1=$DNS1"
                [ -n "$DNS2" ] && echo "dns2=$DNS2"
                [ -n "$DNS3" ] && echo "dns3=$DNS3"
            fi
        else
            error_exit "Interface $INTERFACE not found in interfaces configuration"
        fi
    fi

    echo "RESULT:OK"
    exit 0
fi

# Set new settings
if [ "$IS_NETPLAN" -eq 1 ]; then
    # Create new netplan config
    NETPLAN_FILE="$NETPLAN_DIR/99-custom-$INTERFACE.yaml"

    if [ "$MODE" = "dhcp" ]; then
        if [ "$DRY_RUN" -eq 0 ]; then
            cat > "$NETPLAN_FILE" << EOF
network:
  version: 2
  ethernets:
    $INTERFACE:
      dhcp4: true
EOF
            netplan apply
        else
            log_verbose "[DRY-RUN] Would create Netplan config for DHCP on $INTERFACE"
            log_verbose "[DRY-RUN] Would apply netplan configuration"
        fi
    elif [ "$MODE" = "static" ]; then
        if [ -z "$IP" ] || [ -z "$GATEWAY" ] || [ -z "$NETMASK" ]; then
            error_exit "Static mode requires IP, gateway, and netmask parameters"
        fi

        # Convert netmask to CIDR notation (simplified)
        CIDR=24  # Default to /24 if we can't calculate
        case "$NETMASK" in
            255.0.0.0) CIDR=8 ;;
            255.255.0.0) CIDR=16 ;;
            255.255.255.0) CIDR=24 ;;
        esac

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
            # Prepare DNS section if needed
            DNS_YAML=""
            if [ -n "$DNS1" ]; then
                DNS_LIST="      nameservers:\n        addresses: ["
                DNS_LIST="$DNS_LIST$CLEAN_DNS1"
                [ -n "$DNS2" ] && DNS_LIST="$DNS_LIST, $CLEAN_DNS2"
                [ -n "$DNS3" ] && DNS_LIST="$DNS_LIST, $CLEAN_DNS3"
                DNS_LIST="$DNS_LIST]\n"
                DNS_YAML=$DNS_LIST
            fi

            cat > "$NETPLAN_FILE" << EOF
network:
  version: 2
  ethernets:
    $INTERFACE:
      dhcp4: false
      addresses: [$CLEAN_IP/$CIDR]
      gateway4: $CLEAN_GATEWAY
$(echo -e "$DNS_YAML")
EOF
            netplan apply
        else
            log_verbose "[DRY-RUN] Would create Netplan config for static IP on $INTERFACE"
            log_verbose "[DRY-RUN]   IP: $CLEAN_IP/$CIDR"
            log_verbose "[DRY-RUN]   Gateway: $CLEAN_GATEWAY"
            [ -n "$DNS1" ] && log_verbose "[DRY-RUN]   DNS1: $CLEAN_DNS1"
            [ -n "$DNS2" ] && log_verbose "[DRY-RUN]   DNS2: $CLEAN_DNS2"
            [ -n "$DNS3" ] && log_verbose "[DRY-RUN]   DNS3: $CLEAN_DNS3"
            log_verbose "[DRY-RUN] Would apply netplan configuration"
        fi
    else
        error_exit "Invalid mode: $MODE. Must be 'dhcp' or 'static'"
    fi
else
    # Legacy interfaces file
    if [ "$MODE" = "dhcp" ]; then
        if [ "$DRY_RUN" -eq 0 ]; then
            # First remove any existing configuration for this interface
            sed -i "/iface $INTERFACE inet/,/^[^[:space:]]/d" "$INTERFACES_FILE"
            # Add new DHCP configuration
            echo "" >> "$INTERFACES_FILE"
            echo "auto $INTERFACE" >> "$INTERFACES_FILE"
            echo "iface $INTERFACE inet dhcp" >> "$INTERFACES_FILE"

            # Restart networking
            if systemctl is-active networking > /dev/null 2>&1; then
                systemctl restart networking
            elif [ -f /etc/init.d/networking ]; then
                /etc/init.d/networking restart
            fi
        else
            log_verbose "[DRY-RUN] Would configure $INTERFACE for DHCP in interfaces file"
            log_verbose "[DRY-RUN] Would restart networking service"
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
            # First remove any existing configuration for this interface
            sed -i "/iface $INTERFACE inet/,/^[^[:space:]]/d" "$INTERFACES_FILE"

            # Add new static configuration
            echo "" >> "$INTERFACES_FILE"
            echo "auto $INTERFACE" >> "$INTERFACES_FILE"
            echo "iface $INTERFACE inet static" >> "$INTERFACES_FILE"
            echo "    address $CLEAN_IP" >> "$INTERFACES_FILE"
            echo "    netmask $NETMASK" >> "$INTERFACES_FILE"
            echo "    gateway $CLEAN_GATEWAY" >> "$INTERFACES_FILE"

            # Add DNS if provided
            if [ -n "$DNS1" ] || [ -n "$DNS2" ] || [ -n "$DNS3" ]; then
                DNS_LIST="    dns-nameservers"
                [ -n "$DNS1" ] && DNS_LIST="$DNS_LIST $CLEAN_DNS1"
                [ -n "$DNS2" ] && DNS_LIST="$DNS_LIST $CLEAN_DNS2"
                [ -n "$DNS3" ] && DNS_LIST="$DNS_LIST $CLEAN_DNS3"
                echo "$DNS_LIST" >> "$INTERFACES_FILE"
            fi

            # Restart networking
            if systemctl is-active networking > /dev/null 2>&1; then
                systemctl restart networking
            elif [ -f /etc/init.d/networking ]; then
                /etc/init.d/networking restart
            fi
        else
            log_verbose "[DRY-RUN] Would configure $INTERFACE for static IP in interfaces file"
            log_verbose "[DRY-RUN]   IP: $CLEAN_IP"
            log_verbose "[DRY-RUN]   Gateway: $CLEAN_GATEWAY"
            log_verbose "[DRY-RUN]   Netmask: $NETMASK"
            [ -n "$DNS1" ] && log_verbose "[DRY-RUN]   DNS1: $CLEAN_DNS1"
            [ -n "$DNS2" ] && log_verbose "[DRY-RUN]   DNS2: $CLEAN_DNS2"
            [ -n "$DNS3" ] && log_verbose "[DRY-RUN]   DNS3: $CLEAN_DNS3"
            log_verbose "[DRY-RUN] Would restart networking service"
        fi
    else
        error_exit "Invalid mode: $MODE. Must be 'dhcp' or 'static'"
    fi
fi

echo "RESULT:OK"
exit 0
