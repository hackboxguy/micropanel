#!/bin/sh
#./setup.sh -t pios
USAGE="usage:$0 [-v<verbose> -h<help>] -t<type>[debian|pios|] [-c<config-file>]"
PRINTHELP=0
VERBOSE=0
TYPE="none"
CONFIG_FILE=""

# Function to update or add a sysctl parameter
update_sysctl() {
    local param=$1
    local value=$2
    local config_file="/etc/sysctl.conf"
    # Check if parameter exists with correct value
    if grep -q "^$param=$value" "$config_file"; then
        echo "Parameter $param already set to $value"
    elif grep -q "^$param=" "$config_file"; then
        # Parameter exists but with different value - update it
        echo "Updating $param to $value"
        sed -i "s|^$param=.*|$param=$value|" "$config_file"
    else
        # Parameter doesn't exist - add it
        echo "Adding $param=$value"
        echo "$param=$value" >> "$config_file"
    fi
}

# Function to print logs based on verbosity
log() {
    if [ $VERBOSE -eq 1 ] || [ "$2" = "force" ]; then
        echo "$1"
    fi
}

while getopts hvt:c: f
do
    case $f in
    h) PRINTHELP=1 ;;
    v) VERBOSE=1 ;;
    t) TYPE=$OPTARG ;;
    c) CONFIG_FILE=$OPTARG ;;
    esac
done

if [ $PRINTHELP -eq 1 ]; then
    echo $USAGE
    echo "  -h: Show this help message"
    echo "  -v: Verbose mode"
    echo "  -t: Type of configuration (debian, pios)"
    echo "  -c: Custom config file (optional, defaults to config-TYPE.json)"
    exit 0
fi

if [ $# -lt 1 ]; then
    echo $USAGE
    exit 1
fi

[ "$TYPE" = "none" ] && echo "Missing type -t arg!" && exit 1

if [ $(id -u) -ne 0 ]; then
    echo "Please run setup as root ==> sudo ./setup.sh -t $TYPE"
    exit 1
fi

# Note down our current path, use this path in unit file of micropanel(micropanel.service)
CURRENT_PATH=$(pwd)
log "Current path: $CURRENT_PATH" force

# Create service configuration file path based on type or use custom config
if [ -z "$CONFIG_FILE" ]; then
    # No custom config provided, use default
    CONFIG_FILE="$CURRENT_PATH/screens/config-$TYPE.json"
else
    # Custom config provided, handle different path formats
    case "$CONFIG_FILE" in
        /*) # Already absolute path - use as-is
            ;;
        */*) # Relative path with directory - make it relative to current directory
            CONFIG_FILE="$CURRENT_PATH/$CONFIG_FILE"
            ;;
        *) # Just filename - look in screens/ directory
            CONFIG_FILE="$CURRENT_PATH/screens/$CONFIG_FILE"
            ;;
    esac
fi
if [ ! -f "$CONFIG_FILE" ]; then
    log "Error: Config file $CONFIG_FILE does not exist." force
    echo "Error: Required config file $CONFIG_FILE not found"
    exit 1
fi

log "Using config file: $CONFIG_FILE" force

# Install dependencies
printf "Installing dependencies ................................ "
if [ $VERBOSE -eq 1 ]; then
    DEBIAN_FRONTEND=noninteractive apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y fxload libi2c-dev i2c-tools cmake libudev-dev nlohmann-json3-dev iperf3 libcurl4-openssl-dev avahi-daemon avahi-utils fbi mpv libftdi1 libhidapi-libusb0 dnsmasq
else
    DEBIAN_FRONTEND=noninteractive apt-get update < /dev/null > /dev/null
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq fxload libi2c-dev i2c-tools cmake libudev-dev nlohmann-json3-dev iperf3 libcurl4-openssl-dev avahi-daemon avahi-utils fbi mpv libftdi1 libhidapi-libusb0 dnsmasq < /dev/null > /dev/null
fi
test 0 -eq $? && echo "[OK]" || { echo "[FAIL]"; exit 1; }

# Build micropanel
printf "Building micropanel..................................... "
# Check if build directory exists
if [ -d "build" ]; then
    log "Build directory exists, cleaning it first"
    rm -rf build
fi

mkdir -p build
cd build
if [ $VERBOSE -eq 1 ]; then
    cmake ..
    make -j$(nproc)
else
    cmake .. > /dev/null
    make -j$(nproc) > /dev/null
fi
test 0 -eq $? && echo "[OK]" || { echo "[FAIL]"; exit 1; }

# Go back to original directory
cd "$CURRENT_PATH"

printf "Building utils.......................................... "
gcc utils/patch-generator.c -o scripts/patch-generator 1> /dev/null 2>/dev/null
test 0 -eq $? && echo "[OK]" || { echo "[FAIL]"; exit 1; }

# adjust default absolute path of json config as per this installation path
printf "Fixing paths in json config files....................... "
# Create backup of original config
cp "$CONFIG_FILE" "$CONFIG_FILE.original"
# Update config in place (with temporary file)
$CURRENT_PATH/screens/update-config-path.sh --input=$CONFIG_FILE --output=$CONFIG_FILE.tmp --path=$CURRENT_PATH
# Replace original with updated version
mv "$CONFIG_FILE.tmp" "$CONFIG_FILE"
test 0 -eq $? && echo "[OK]" || { echo "[FAIL]"; exit 1; }

# Modify service file in-place
printf "Configuring micropanel service.......................... "
# Get paths for binary and config
BINARY_PATH="$CURRENT_PATH/build/micropanel"

#for pios, use local gpio-keys and i2c for driving ssd1306
if [ "$TYPE" = "pios" ]; then
    PIOS_ARGS="-a -i gpio -s /dev/i2c-3 "
fi

# Update the service file with correct paths
SERVICE_FILE="$CURRENT_PATH/micropanel.service"
if [ -f "$SERVICE_FILE" ]; then
    # Create backup of original service file
    cp "$SERVICE_FILE" "$SERVICE_FILE.bak"

    # Create the updated service file
    cat > "$SERVICE_FILE" << EOF
[Unit]
Description=MicroPanel OLED Menu System
After=network.target

[Service]
Type=simple
User=root
ExecStart=/bin/sh -c '$BINARY_PATH $PIOS_ARGS -a -c $CONFIG_FILE >/tmp/micropanel.log 2>&1'
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF
    chmod 644 "$SERVICE_FILE"
else
    echo "[FAIL] - Service file not found"
    exit 1
fi

test 0 -eq $? && echo "[OK]" || { echo "[FAIL]"; exit 1; }

#following netsocket buffer increase is necessary for iperf3 based udp test
printf "Updating sysctl file.................................... "
update_sysctl "net.core.rmem_max" "26214400"
update_sysctl "net.core.wmem_max" "26214400"
update_sysctl "net.core.rmem_default" "1310720"
update_sysctl "net.core.wmem_default" "1310720"
# Apply changes
sysctl -p
test 0 -eq $? && echo "[OK]" || { echo "[FAIL]"; exit 1; }

# Reload systemd, enable and start service
printf "Starting micropanel..................................... "
systemctl daemon-reload
systemctl enable "$SERVICE_FILE"
systemctl start micropanel.service
test 0 -eq $? && echo "[OK]" || { echo "[FAIL]"; exit 1; }

#update our default config.txt for proper operation of media players
cp configs/config.txt /boot/firmware/
#enable high-speed-uart for mcu programming
sed -i 's/^console=serial0,115200 //' /boot/firmware/cmdline.txt
#enable i2c module loading so that /dev/i2c* shows up
echo 'i2c-dev' | sudo tee /etc/modules-load.d/i2c.conf

sync
printf "Installation complete, reboot the system................ \n"
log "Micropanel configured with:" force
log "- Binary path: $BINARY_PATH" force
log "- Config file: $CONFIG_FILE" force
log "Run 'systemctl status micropanel' to check service status" force
