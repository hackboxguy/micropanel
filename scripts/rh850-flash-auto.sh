#!/bin/sh
# Select the Bluebox transport when it is attached; otherwise preserve the
# established flashrh850.sh GPIO/UART workflow without changing its arguments.
set -eu

PROGRAM_NAME=${0##*/}
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

BLUEBOX_VENDOR_ID=${MICROPANEL_BLUEBOX_VENDOR_ID:-0403}
BLUEBOX_PRODUCT_ID=${MICROPANEL_BLUEBOX_PRODUCT_ID:-a9a0}
TTY_SYS_CLASS=${MICROPANEL_SYS_CLASS_TTY:-/sys/class/tty}
DEV_ROOT=${MICROPANEL_DEV_ROOT:-/dev}

LEGACY_FLASHER=${MICROPANEL_LEGACY_FLASHER:-$SCRIPT_DIR/flashrh850.sh}
RH850_TOOL=${MICROPANEL_RH850_TOOL:-$SCRIPT_DIR/rh850-tool}
BACKUP_ROOT=${MICROPANEL_RH850_BACKUP_ROOT:-$SCRIPT_DIR/../share/sp6bins/bkups}

BOARD=
TARGET_PROFILE=
LEGACY_BACKUP_NAME=
BLUEBOX_PROGRAM_PORT=
BLUEBOX_CONSOLE_PORT=
SELECTED_TRANSPORT=

die()
{
    printf '[ERROR] %s\n' "$*" >&2
    exit 2
}

info()
{
    printf '[INFO] %s\n' "$*"
}

usage()
{
    cat <<EOF
Usage: $PROGRAM_NAME --board=983hh|lattice45-9090|spartan7-9090 <flashrh850.sh arguments>

The board selector must be the first argument. When an EEHB Bluebox
(USB ID ${BLUEBOX_VENDOR_ID}:${BLUEBOX_PRODUCT_ID}) exposes both serial interfaces,
the matching rh850-tool target profile is used. If no Bluebox is attached, all
remaining arguments are passed unchanged to flashrh850.sh for the legacy
Pi GPIO/local-UART path.
EOF
}

require_executable()
{
    case "$1" in
        */*) [ -x "$1" ] || die "required executable not found: $1" ;;
        *) command -v "$1" >/dev/null 2>&1 || die "required executable not found in PATH: $1" ;;
    esac
}

option_value()
{
    wanted=$1
    shift
    for argument in "$@"; do
        case "$argument" in
            "$wanted"=*) printf '%s\n' "${argument#*=}"; return 0 ;;
        esac
    done
    return 1
}

has_option()
{
    wanted=$1
    shift
    for argument in "$@"; do
        [ "$argument" = "$wanted" ] && return 0
    done
    return 1
}

# Find interface 01 (Bluebox programming) and interface 00 (Bluebox console)
# on the *same* FT2232D device. This avoids binding to unrelated ttyUSB ports.
find_bluebox_ports()
{
    bluebox_usb_dir=
    BLUEBOX_PROGRAM_PORT=
    BLUEBOX_CONSOLE_PORT=

    for tty_entry in "$TTY_SYS_CLASS"/ttyUSB*; do
        [ -e "$tty_entry/device" ] || continue
        device_path=$(readlink -f "$tty_entry/device")
        interface_dir=$(dirname "$device_path")
        usb_dir=$(dirname "$interface_dir")
        vendor=$(cat "$usb_dir/idVendor" 2>/dev/null || true)
        product=$(cat "$usb_dir/idProduct" 2>/dev/null || true)
        interface=$(cat "$interface_dir/bInterfaceNumber" 2>/dev/null || true)

        [ "$vendor" = "$BLUEBOX_VENDOR_ID" ] || continue
        [ "$product" = "$BLUEBOX_PRODUCT_ID" ] || continue
        [ "$interface" = "01" ] || continue

        bluebox_usb_dir=$usb_dir
        BLUEBOX_PROGRAM_PORT=$DEV_ROOT/${tty_entry##*/}
        break
    done

    [ -n "$bluebox_usb_dir" ] || return 1
    [ -c "$BLUEBOX_PROGRAM_PORT" ] || return 1

    for tty_entry in "$TTY_SYS_CLASS"/ttyUSB*; do
        [ -e "$tty_entry/device" ] || continue
        device_path=$(readlink -f "$tty_entry/device")
        interface_dir=$(dirname "$device_path")
        usb_dir=$(dirname "$interface_dir")
        interface=$(cat "$interface_dir/bInterfaceNumber" 2>/dev/null || true)

        [ "$usb_dir" = "$bluebox_usb_dir" ] || continue
        [ "$interface" = "00" ] || continue

        BLUEBOX_CONSOLE_PORT=$DEV_ROOT/${tty_entry##*/}
        break
    done

    [ -n "$BLUEBOX_CONSOLE_PORT" ] && [ -c "$BLUEBOX_CONSOLE_PORT" ]
}

select_board()
{
    case "$BOARD" in
        983hh)
            TARGET_PROFILE=r7f7016863
            LEGACY_BACKUP_NAME=983HH
            ;;
        lattice45-9090)
            TARGET_PROFILE=r7f7016863
            LEGACY_BACKUP_NAME=LAT-ECP5
            ;;
        spartan7-9090)
            TARGET_PROFILE=r7f7015873
            LEGACY_BACKUP_NAME=Spartan7_9090
            ;;
        *) die "unsupported board: $BOARD" ;;
    esac
}

run_transport()
{
    require_executable "$RH850_TOOL"
    if [ "$SELECTED_TRANSPORT" = bluebox ]; then
        "$RH850_TOOL" "$@" --target="$TARGET_PROFILE" --transport=bluebox \
            --port="$BLUEBOX_PROGRAM_PORT" --console-port="$BLUEBOX_CONSOLE_PORT"
    else
        "$RH850_TOOL" "$@" --target="$TARGET_PROFILE" --transport=pi-gpio
    fi
}

transport_description()
{
    if [ "$SELECTED_TRANSPORT" = bluebox ]; then
        printf 'Bluebox on %s (console: %s)' "$BLUEBOX_PROGRAM_PORT" "$BLUEBOX_CONSOLE_PORT"
    else
        printf 'Pi GPIO/local-UART transport'
    fi
}

flash_image()
{
    image=$1
    [ -r "$image" ] || die "firmware image is not readable: $image"
    info "Using $(transport_description)"
    info "Flashing $BOARD with $image"
    run_transport flash --image="$image" --yes
}

backup_target()
{
    backup_dir=$BACKUP_ROOT/$BOARD
    mkdir -p "$backup_dir"
    info "Using $(transport_description)"
    info "Backing up $BOARD to $backup_dir"
    run_transport backup --output="$backup_dir"
    printf '[SUCCESS] MCU backup completed successfully\n'
}

latest_bluebox_backup()
{
    backup_dir=$BACKUP_ROOT/$BOARD
    latest=
    for candidate in "$backup_dir"/*-codeflash-*.bin; do
        [ -f "$candidate" ] || continue
        if [ -z "$latest" ] || [ "$candidate" -nt "$latest" ]; then
            latest=$candidate
        fi
    done
    [ -n "$latest" ] && printf '%s\n' "$latest"
}

recover_target()
{
    image=$(latest_bluebox_backup || true)
    if [ -z "$image" ]; then
        legacy_image=$BACKUP_ROOT/$LEGACY_BACKUP_NAME-bkup.bin
        [ -r "$legacy_image" ] && image=$legacy_image
    fi
    [ -n "$image" ] || die "no backup found for $BOARD under $BACKUP_ROOT"

    info "Using $(transport_description)"
    info "Recovering $BOARD from $image"
    run_transport recover --image="$image" --yes
}

readback_target()
{
    output=$(option_value --output "$@" || true)
    [ -n "$output" ] || die "--readback requires --output=FILE"
    info "Using $(transport_description)"
    run_transport read --output="$output"
}

[ "$#" -gt 0 ] || { usage >&2; exit 2; }
case "$1" in
    --board=*) BOARD=${1#--board=}; shift ;;
    --help|-h) usage; exit 0 ;;
    *) die "--board=... must be the first argument" ;;
esac
[ -n "$BOARD" ] || die "--board requires a value"
[ "$#" -gt 0 ] || die "missing flash action"
select_board

if ! find_bluebox_ports; then
    if [ "$BOARD" = spartan7-9090 ]; then
        SELECTED_TRANSPORT=pi-gpio
        require_executable "$RH850_TOOL"
        info "Bluebox not detected; using Pi GPIO/local-UART transport for S7-9090"
    else
        require_executable "$LEGACY_FLASHER"
        info "Bluebox not detected; using legacy Pi GPIO/local-UART transport"
        exec "$LEGACY_FLASHER" "$@"
    fi
else
    SELECTED_TRANSPORT=bluebox
fi

if has_option --info "$@"; then
    info "Using $(transport_description)"
    run_transport probe
    printf '[SUCCESS] MCU probe completed successfully\n'
elif has_option --backup "$@"; then
    backup_target
elif has_option --recover "$@"; then
    recover_target
elif has_option --readback "$@"; then
    readback_target "$@"
elif image=$(option_value --bios-autorun "$@" || true); then
    flash_image "$image"
else
    bios=$(option_value --bios "$@" || true)
    autorun=$(option_value --autorun "$@" || option_value --script "$@" || true)
    if [ -n "$bios" ] && [ -n "$autorun" ]; then
        [ -r "$bios" ] || die "BIOS image is not readable: $bios"
        [ -r "$autorun" ] || die "autorun script is not readable: $autorun"
        combined_image=$(mktemp /tmp/rh850-flash-auto.XXXXXX.bin)
        trap 'rm -f "$combined_image"' EXIT HUP INT TERM
        cat "$bios" "$autorun" > "$combined_image"
        flash_image "$combined_image"
    else
        die "Bluebox is attached, but this flashrh850.sh action is unsupported"
    fi
fi
