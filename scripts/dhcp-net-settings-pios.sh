#!/bin/sh
# dhcp-net-settings-pios.sh - Raspberry Pi OS specific network configuration
# This script is called from the main dhcp-net-settings.sh script
# Supports both dhcpcd and NetworkManager based configurations

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

# Define file paths
DHCPCD_CONF="/etc/dhcpcd.conf"
NM_CONN_DIR="/etc/NetworkManager/system-connections"

# Detect network management system
detect_network_system() {
    # Check if NetworkManager is installed and running
    if command -v nmcli >/dev/null 2>&1 && systemctl is-active NetworkManager >/dev/null 2>&1; then
        log_verbose "NetworkManager is active on this system"
        NETWORK_SYSTEM="networkmanager"
        return
    fi

    # Check if dhcpcd is installed and running
    if [ -f "$DHCPCD_CONF" ] && (systemctl is-active dhcpcd >/dev/null 2>&1 || 
                                ps aux | grep -q "[d]hcpcd"); then
        log_verbose "dhcpcd is active on this system"
        NETWORK_SYSTEM="dhcpcd"
        return
    fi

    # Default to dhcpcd as fallback (for older systems)
    log_verbose "Defaulting to dhcpcd configuration"
    NETWORK_SYSTEM="dhcpcd"
}

# Get current settings when using dhcpcd
get_dhcpcd_settings() {
    if grep -q "^interface $INTERFACE$" "$DHCPCD_CONF" 2>/dev/null && \
       grep -A10 "^interface $INTERFACE$" "$DHCPCD_CONF" 2>/dev/null | grep -q "static ip_address"; then
        echo "mode=static"

        # Extract IP information
        IP=$(grep -A10 "^interface $INTERFACE$" "$DHCPCD_CONF" 2>/dev/null | grep "static ip_address" | cut -d'=' -f2 | cut -d'/' -f1)
        GATEWAY=$(grep -A10 "^interface $INTERFACE$" "$DHCPCD_CONF" 2>/dev/null | grep "static routers" | cut -d'=' -f2)

        # Extract netmask from CIDR if present
        CIDR=$(grep -A10 "^interface $INTERFACE$" "$DHCPCD_CONF" 2>/dev/null | grep "static ip_address" | cut -d'=' -f2 | grep -o '/.*' | tr -d '/')
        if [ -n "$CIDR" ]; then
            # Convert CIDR to netmask
            case "$CIDR" in
                8) NETMASK="255.0.0.0" ;;
                16) NETMASK="255.255.0.0" ;;
                24) NETMASK="255.255.255.0" ;;
                *)  NETMASK="255.255.255.0" ;; # Default
            esac
        else
            NETMASK="255.255.255.0" # Default
        fi

        echo "ip=$IP"
        echo "gateway=$GATEWAY"
        echo "netmask=$NETMASK"

        # Extract DNS information
        DNS=$(grep -A10 "^interface $INTERFACE$" "$DHCPCD_CONF" 2>/dev/null | grep "static domain_name_servers" | cut -d'=' -f2)
        DNS1=$(echo "$DNS" | cut -d' ' -f1)
        DNS2=$(echo "$DNS" | cut -d' ' -f2)
        DNS3=$(echo "$DNS" | cut -d' ' -f3)

        [ -n "$DNS1" ] && echo "dns1=$DNS1"
        [ -n "$DNS2" ] && echo "dns2=$DNS2"
        [ -n "$DNS3" ] && echo "dns3=$DNS3"
    else
        # Check if interface has an IP
        CURRENT_IP=$(ip -4 addr show dev "$INTERFACE" 2>/dev/null | grep -o 'inet [0-9.]*' | cut -d' ' -f2)

        if [ -n "$CURRENT_IP" ]; then
            # Check if DHCP is running for this interface
            if ps aux | grep -q "[d]hcpcd.*$INTERFACE" || [ -f "/var/lib/dhcpcd/dhcpcd-$INTERFACE.lease" ]; then
                echo "mode=dhcp"
            else
                # Could be static but not configured in dhcpcd.conf
                echo "mode=static"
                echo "ip=$CURRENT_IP"

                # Get gateway
                GATEWAY=$(ip route | grep "default.*$INTERFACE" | cut -d' ' -f3)
                [ -n "$GATEWAY" ] && echo "gateway=$GATEWAY"

                # Get netmask (might be in CIDR format)
                CIDR=$(ip -4 addr show dev "$INTERFACE" | grep -o 'inet [0-9.]*\/[0-9]*' | cut -d'/' -f2)
                if [ -n "$CIDR" ]; then
                    # Convert CIDR to netmask
                    case "$CIDR" in
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
}

# Get current settings when using NetworkManager
get_nm_settings() {
    # Check if the interface has a connection profile
    CONNECTION=$(nmcli -t -f NAME,DEVICE connection show --active | grep ":$INTERFACE" | cut -d':' -f1)
    
    if [ -z "$CONNECTION" ]; then
        # No active connection for this interface
        echo "mode=dhcp"  # Default to DHCP
        return
    fi
    
    # Get connection details
    IP_METHOD=$(nmcli -t -f ipv4.method connection show "$CONNECTION" 2>/dev/null | cut -d':' -f2)
    
    if [ "$IP_METHOD" = "auto" ]; then
        echo "mode=dhcp"
    else
        echo "mode=static"
        
        # Get IP address
        IP=$(nmcli -t -f ipv4.addresses connection show "$CONNECTION" 2>/dev/null | cut -d':' -f2 | cut -d'/' -f1)
        echo "ip=$IP"
        
        # Get gateway
        GATEWAY=$(nmcli -t -f ipv4.gateway connection show "$CONNECTION" 2>/dev/null | cut -d':' -f2)
        [ -n "$GATEWAY" ] && echo "gateway=$GATEWAY"
        
        # Get netmask from CIDR
        CIDR=$(nmcli -t -f ipv4.addresses connection show "$CONNECTION" 2>/dev/null | cut -d':' -f2 | grep -o '/.*' | tr -d '/')
        if [ -n "$CIDR" ]; then
            # Convert CIDR to netmask
            case "$CIDR" in
                8) NETMASK="255.0.0.0" ;;
                16) NETMASK="255.255.0.0" ;;
                24) NETMASK="255.255.255.0" ;;
                *)  NETMASK="255.255.255.0" ;; # Default
            esac
            echo "netmask=$NETMASK"
        fi
        
        # Get DNS servers
        DNS=$(nmcli -t -f ipv4.dns connection show "$CONNECTION" 2>/dev/null | cut -d':' -f2)
        DNS1=$(echo "$DNS" | awk '{print $1}')
        DNS2=$(echo "$DNS" | awk '{print $2}')
        DNS3=$(echo "$DNS" | awk '{print $3}')
        
        [ -n "$DNS1" ] && echo "dns1=$DNS1"
        [ -n "$DNS2" ] && echo "dns2=$DNS2"
        [ -n "$DNS3" ] && echo "dns3=$DNS3"
    fi
}

# Configure dhcpcd for DHCP
set_dhcpcd_dhcp() {
    if [ "$DRY_RUN" -eq 0 ]; then
        # Remove any static configuration for this interface
        sed -i "/^interface $INTERFACE$/,/^[^[:space:]]/d" "$DHCPCD_CONF"
        
        # Restart dhcpcd
        if systemctl is-active dhcpcd >/dev/null 2>&1; then
            systemctl restart dhcpcd
        else
            service dhcpcd restart 2>/dev/null || /etc/init.d/dhcpcd restart 2>/dev/null
        fi
    else
        log_verbose "[DRY-RUN] Would remove static configuration for $INTERFACE"
        log_verbose "[DRY-RUN] Would restart dhcpcd service"
    fi
}

# Configure dhcpcd for static IP
set_dhcpcd_static() {
    # Convert netmask to CIDR notation
    CIDR=24  # Default to /24 if we can't calculate
    case "$NETMASK" in
        255.0.0.0) CIDR=8 ;;
        255.255.0.0) CIDR=16 ;;
        255.255.255.0) CIDR=24 ;;
    esac

    if [ "$DRY_RUN" -eq 0 ]; then
        # Remove any existing configuration for this interface
        sed -i "/^interface $INTERFACE$/,/^[^[:space:]]/d" "$DHCPCD_CONF"

        # Add new static configuration
        echo "" >> "$DHCPCD_CONF"
        echo "interface $INTERFACE" >> "$DHCPCD_CONF"
        echo "static ip_address=$IP/$CIDR" >> "$DHCPCD_CONF"
        echo "static routers=$GATEWAY" >> "$DHCPCD_CONF"

        # Add DNS if provided
        if [ -n "$DNS1" ] || [ -n "$DNS2" ] || [ -n "$DNS3" ]; then
            DNS_LIST="static domain_name_servers="
            [ -n "$DNS1" ] && DNS_LIST="$DNS_LIST$DNS1"
            [ -n "$DNS2" ] && DNS_LIST="$DNS_LIST $DNS2"
            [ -n "$DNS3" ] && DNS_LIST="$DNS_LIST $DNS3"
            echo "$DNS_LIST" >> "$DHCPCD_CONF"
        fi

        # Restart dhcpcd
        if systemctl is-active dhcpcd >/dev/null 2>&1; then
            systemctl restart dhcpcd
        else
            service dhcpcd restart 2>/dev/null || /etc/init.d/dhcpcd restart 2>/dev/null
        fi
    else
        log_verbose "[DRY-RUN] Would configure $INTERFACE for static IP"
        log_verbose "[DRY-RUN]   IP: $IP/$CIDR"
        log_verbose "[DRY-RUN]   Gateway: $GATEWAY"
        log_verbose "[DRY-RUN]   Netmask: $NETMASK (as CIDR: /$CIDR)"
        [ -n "$DNS1" ] && log_verbose "[DRY-RUN]   DNS1: $DNS1"
        [ -n "$DNS2" ] && log_verbose "[DRY-RUN]   DNS2: $DNS2"
        [ -n "$DNS3" ] && log_verbose "[DRY-RUN]   DNS3: $DNS3"
        log_verbose "[DRY-RUN] Would restart dhcpcd service"
    fi
}

# Configure NetworkManager for DHCP
set_nm_dhcp() {
    # Check if connection exists for interface
    CONNECTION=$(nmcli -t -f NAME,DEVICE connection show --active | grep ":$INTERFACE" | cut -d':' -f1)
    
    if [ "$DRY_RUN" -eq 0 ]; then
        if [ -n "$CONNECTION" ]; then
            # Modify existing connection
            nmcli connection modify "$CONNECTION" ipv4.method auto
            
            # Remove any static IP configuration
            nmcli connection modify "$CONNECTION" ipv4.addresses "" ipv4.gateway "" ipv4.dns ""
            
            # Activate the connection
            nmcli connection up "$CONNECTION"
        else
            # Create new DHCP connection
            nmcli connection add type ethernet con-name "$INTERFACE" ifname "$INTERFACE" ipv4.method auto
            nmcli connection up "$INTERFACE"
        fi
    else
        log_verbose "[DRY-RUN] Would configure NetworkManager connection for $INTERFACE to use DHCP"
    fi
}

# Configure NetworkManager for static IP
set_nm_static() {
    # Convert netmask to CIDR notation
    CIDR=24  # Default to /24 if we can't calculate
    case "$NETMASK" in
        255.0.0.0) CIDR=8 ;;
        255.255.0.0) CIDR=16 ;;
        255.255.255.0) CIDR=24 ;;
    esac

    # Check if connection exists for interface
    CONNECTION=$(nmcli -t -f NAME,DEVICE connection show --active | grep ":$INTERFACE" | cut -d':' -f1)
    
    # Remove leading zeros from IP address components
    # NetworkManager doesn't accept IPs with leading zeros like 192.168.001.001
    # Split IP address by dots, remove leading zeros from each part, then rejoin
    CLEAN_IP=$(echo "$IP" | awk -F. '{printf "%d.%d.%d.%d", $1, $2, $3, $4}')
    CLEAN_GATEWAY=$(echo "$GATEWAY" | awk -F. '{printf "%d.%d.%d.%d", $1, $2, $3, $4}')
    
    log_verbose "Cleaned IP address: $CLEAN_IP (was $IP)"
    log_verbose "Cleaned Gateway: $CLEAN_GATEWAY (was $GATEWAY)"
    
    if [ "$DRY_RUN" -eq 0 ]; then
        # Prepare DNS servers string
        DNS_SERVERS=""
        [ -n "$DNS1" ] && DNS_SERVERS="$DNS1"
        [ -n "$DNS2" ] && DNS_SERVERS="$DNS_SERVERS,$DNS2"
        [ -n "$DNS3" ] && DNS_SERVERS="$DNS_SERVERS,$DNS3"
        
        if [ -n "$CONNECTION" ]; then
            # Modify existing connection
            nmcli connection modify "$CONNECTION" ipv4.method manual ipv4.addresses "$CLEAN_IP/$CIDR" ipv4.gateway "$CLEAN_GATEWAY" 
            
            # Set DNS servers if provided
            if [ -n "$DNS_SERVERS" ]; then
                nmcli connection modify "$CONNECTION" ipv4.dns "$DNS_SERVERS"
            fi
            
            # Activate the connection
            nmcli connection up "$CONNECTION"
        else
            # Create new static IP connection
            nmcli connection add type ethernet con-name "$INTERFACE" ifname "$INTERFACE" \
                ipv4.method manual ipv4.addresses "$CLEAN_IP/$CIDR" ipv4.gateway "$CLEAN_GATEWAY"
            
            # Set DNS servers if provided
            if [ -n "$DNS_SERVERS" ]; then
                nmcli connection modify "$INTERFACE" ipv4.dns "$DNS_SERVERS"
            fi
            
            # Activate the connection
            nmcli connection up "$INTERFACE"
        fi
    else
        log_verbose "[DRY-RUN] Would configure NetworkManager connection for $INTERFACE with static IP"
        log_verbose "[DRY-RUN]   IP: $IP/$CIDR"
        log_verbose "[DRY-RUN]   Gateway: $GATEWAY"
        log_verbose "[DRY-RUN]   Netmask: $NETMASK (as CIDR: /$CIDR)"
        [ -n "$DNS1" ] && log_verbose "[DRY-RUN]   DNS1: $DNS1"
        [ -n "$DNS2" ] && log_verbose "[DRY-RUN]   DNS2: $DNS2"
        [ -n "$DNS3" ] && log_verbose "[DRY-RUN]   DNS3: $DNS3"
    fi
}

# Backup current network settings
backup_network_settings() {
    # Create backup directory if needed
    if [ ! -d "$BACKUP_PATH" ]; then
        mkdir -p "$BACKUP_PATH" || error_exit "Failed to create backup directory"
    fi
    
    # Backup dhcpcd.conf if it exists
    if [ -f "$DHCPCD_CONF" ]; then
        if [ "$DRY_RUN" -eq 0 ]; then
            cp "$DHCPCD_CONF" "$BACKUP_PATH/dhcpcd.conf.$(date +%Y%m%d%H%M%S)" || \
                error_exit "Failed to backup dhcpcd.conf"
        else
            log_verbose "[DRY-RUN] Would backup dhcpcd.conf"
        fi
    fi
    
    # Backup NetworkManager connection if it exists
    if [ "$NETWORK_SYSTEM" = "networkmanager" ]; then
        CONNECTION=$(nmcli -t -f NAME,DEVICE connection show --active | grep ":$INTERFACE" | cut -d':' -f1)
        if [ -n "$CONNECTION" ]; then
            CONN_FILE=$(find "$NM_CONN_DIR" -name "*.nmconnection" -exec grep -l "id=$CONNECTION" {} \;)
            if [ -n "$CONN_FILE" ] && [ -f "$CONN_FILE" ]; then
                if [ "$DRY_RUN" -eq 0 ]; then
                    cp "$CONN_FILE" "$BACKUP_PATH/$(basename "$CONN_FILE").$(date +%Y%m%d%H%M%S)" || \
                        log_verbose "Failed to backup NetworkManager connection file"
                else
                    log_verbose "[DRY-RUN] Would backup NetworkManager connection file"
                fi
            fi
        fi
    fi
}

# DHCP Server configuration file managed by micropanel
DNSMASQ_CONF="/etc/dnsmasq.d/micropanel-dhcp-server.conf"

# Check if system is currently in dhcp-server mode
is_dhcp_server_active() {
    # Check if our dnsmasq config exists AND dnsmasq is running
    [ -f "$DNSMASQ_CONF" ] && systemctl is-active --quiet dnsmasq 2>/dev/null
}

# Read current settings when in dhcp-server mode
get_dhcp_server_settings() {
    echo "mode=dhcp-server"

    # Read IP from the interface
    local current_ip=$(ip -4 addr show "$INTERFACE" 2>/dev/null | grep -oP 'inet \K[0-9.]+' | head -1)
    if [ -n "$current_ip" ]; then
        echo "ip=$current_ip"
        echo "gateway=$current_ip"
    else
        echo "ip=192.168.1.1"
        echo "gateway=192.168.1.1"
    fi

    # Read netmask from the interface
    local cidr=$(ip -4 addr show "$INTERFACE" 2>/dev/null | grep -oP 'inet [0-9.]+/\K[0-9]+' | head -1)
    if [ -n "$cidr" ]; then
        # Convert CIDR to dotted netmask
        local mask=""
        local full_octets=$((cidr / 8))
        local partial_bits=$((cidr % 8))
        for i in 1 2 3 4; do
            if [ "$i" -le "$full_octets" ]; then
                mask="${mask}255"
            elif [ "$i" -eq $((full_octets + 1)) ] && [ "$partial_bits" -gt 0 ]; then
                mask="${mask}$((256 - (1 << (8 - partial_bits))))"
            else
                mask="${mask}0"
            fi
            [ "$i" -lt 4 ] && mask="${mask}."
        done
        echo "netmask=$mask"
    else
        echo "netmask=255.255.255.0"
    fi

    echo "dns1="
    echo "dns2="
    echo "dns3="
}

# Stop DHCP server and clean up configuration
stop_dhcp_server() {
    log_verbose "Stopping DHCP server..."
    systemctl stop dnsmasq 2>/dev/null || true
    rm -f "$DNSMASQ_CONF"
    log_verbose "DHCP server stopped and config removed"
}

# Convert dotted netmask to CIDR prefix length
netmask_to_cidr() {
    local netmask="$1"
    local cidr=0
    for octet in $(echo "$netmask" | tr '.' ' '); do
        case "$octet" in
            255) cidr=$((cidr + 8)) ;;
            254) cidr=$((cidr + 7)) ;;
            252) cidr=$((cidr + 6)) ;;
            248) cidr=$((cidr + 5)) ;;
            240) cidr=$((cidr + 4)) ;;
            224) cidr=$((cidr + 3)) ;;
            192) cidr=$((cidr + 2)) ;;
            128) cidr=$((cidr + 1)) ;;
            0)   ;;
        esac
    done
    echo "$cidr"
}

# Configure and start DHCP server
set_dhcp_server() {
    log_verbose "Configuring DHCP server on $INTERFACE..."

    local cidr=$(netmask_to_cidr "$NETMASK")

    # Derive DHCP range from the server IP (use .100-.200 in the same subnet)
    local ip_prefix=$(echo "$IP" | cut -d. -f1-3)
    local dhcp_start="${ip_prefix}.100"
    local dhcp_end="${ip_prefix}.200"

    # Stop any existing DHCP client services for this interface
    systemctl stop NetworkManager 2>/dev/null || true
    systemctl stop dhcpcd 2>/dev/null || true
    dhclient -r "$INTERFACE" 2>/dev/null || true
    pkill -f "dhclient.*$INTERFACE" 2>/dev/null || true

    # Configure static IP on the interface
    ip addr flush dev "$INTERFACE" 2>/dev/null || true
    ip addr add "${IP}/${cidr}" dev "$INTERFACE"
    ip link set "$INTERFACE" up
    sleep 1

    # Write dnsmasq configuration
    mkdir -p /etc/dnsmasq.d
    cat > "$DNSMASQ_CONF" << EOF
# Micropanel DHCP Server Configuration
# Managed by dhcp-net-settings-pios.sh — do not edit manually
interface=${INTERFACE}
bind-interfaces
dhcp-range=${dhcp_start},${dhcp_end},12h
dhcp-option=3,${IP}
dhcp-authoritative
no-resolv
no-hosts
EOF

    # Start dnsmasq
    systemctl start dnsmasq
    if systemctl is-active --quiet dnsmasq; then
        log_verbose "DHCP server started: ${IP}/${cidr}, range ${dhcp_start}-${dhcp_end}"
    else
        log_verbose "WARNING: dnsmasq failed to start"
        return 1
    fi
}

# Main script execution starts here
# Detect which network management system is being used
detect_network_system

# Backup current settings
backup_network_settings

# Get current settings if no mode specified
if [ -z "$MODE" ]; then
    # Check for dhcp-server mode first (takes priority over static/dhcp detection)
    if is_dhcp_server_active; then
        get_dhcp_server_settings
    elif [ "$NETWORK_SYSTEM" = "networkmanager" ]; then
        get_nm_settings
    else
        get_dhcpcd_settings
    fi

    echo "RESULT:OK"
    exit 0
fi

# Stop DHCP server if switching away from dhcp-server mode
if [ "$MODE" != "dhcp-server" ] && is_dhcp_server_active; then
    stop_dhcp_server
fi

# Set new settings
if [ "$MODE" = "dhcp" ]; then
    if [ "$NETWORK_SYSTEM" = "networkmanager" ]; then
        set_nm_dhcp
    else
        set_dhcpcd_dhcp
    fi
elif [ "$MODE" = "static" ]; then
    if [ -z "$IP" ] || [ -z "$GATEWAY" ] || [ -z "$NETMASK" ]; then
        error_exit "Static mode requires IP, gateway, and netmask parameters"
    fi

    if [ "$NETWORK_SYSTEM" = "networkmanager" ]; then
        set_nm_static
    else
        set_dhcpcd_static
    fi
elif [ "$MODE" = "dhcp-server" ]; then
    if [ -z "$IP" ] || [ -z "$GATEWAY" ] || [ -z "$NETMASK" ]; then
        error_exit "DHCP server mode requires IP, gateway, and netmask parameters"
    fi

    set_dhcp_server
else
    error_exit "Invalid mode: $MODE. Must be 'dhcp', 'static', or 'dhcp-server'"
fi

echo "RESULT:OK"
exit 0
