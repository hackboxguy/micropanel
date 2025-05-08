#!/bin/sh
#./setup.sh -t pios
USAGE="usage:$0 [-v<verbose> -h<help>] -t<type>[debian|pios|]"
PRINTHELP=0
VERBOSE=0
TYPE="none"

# Function to print logs based on verbosity
log() {
    if [ $VERBOSE -eq 1 ] || [ "$2" = "force" ]; then
        echo "$1"
    fi
}

while getopts hvt: f
do
    case $f in
    h) PRINTHELP=1 ;;
    v) VERBOSE=1 ;;
    t) TYPE=$OPTARG ;;
    esac
done

if [ $PRINTHELP -eq 1 ]; then
    echo $USAGE
    echo "  -h: Show this help message"
    echo "  -v: Verbose mode"
    echo "  -t: Type of configuration (debian, pios)"
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

# Create service configuration file path based on type
CONFIG_FILE="$CURRENT_PATH/screens/config-$TYPE.json"
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
    DEBIAN_FRONTEND=noninteractive apt-get install -y cmake libudev-dev nlohmann-json3-dev iperf3 libcurl4-openssl-dev avahi-daemon avahi-utils libraspberrypi-bin
else
    DEBIAN_FRONTEND=noninteractive apt-get update < /dev/null > /dev/null
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq cmake libudev-dev nlohmann-json3-dev iperf3 libcurl4-openssl-dev avahi-daemon avahi-utils libraspberrypi-bin < /dev/null > /dev/null
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
    make
else
    cmake .. > /dev/null
    make > /dev/null
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
ExecStart=$BINARY_PATH -c $CONFIG_FILE 1>/tmp/micropanel.log 2>/tmp/micropanel.log
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

# Reload systemd, enable and start service
printf "Starting micropanel..................................... "
systemctl daemon-reload
systemctl enable "$SERVICE_FILE"
systemctl start micropanel.service
test 0 -eq $? && echo "[OK]" || { echo "[FAIL]"; exit 1; }

sync
printf "Installation complete, reboot the system................ \n"
log "Micropanel configured with:" force
log "- Binary path: $BINARY_PATH" force
log "- Config file: $CONFIG_FILE" force
log "Run 'systemctl status micropanel' to check service status" force
