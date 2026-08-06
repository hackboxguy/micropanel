#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TOOL="$ROOT_DIR/scripts/rh850-flash-auto.sh"
WORK_DIR=$(mktemp -d)
trap 'rm -rf "$WORK_DIR"' EXIT HUP INT TERM

mkdir -p "$WORK_DIR/bin" "$WORK_DIR/dev" "$WORK_DIR/sys/class/tty"

cat > "$WORK_DIR/bin/flashrh850.sh" <<'EOF'
#!/bin/sh
printf 'legacy:'
printf '%s:' "$@"
printf '\n'
EOF
chmod +x "$WORK_DIR/bin/flashrh850.sh"

cat > "$WORK_DIR/bin/rh850-tool" <<'EOF'
#!/bin/sh
printf 'rh850-tool:'
printf '%s:' "$@"
printf '\n'
EOF
chmod +x "$WORK_DIR/bin/rh850-tool"

run_tool()
{
    MICROPANEL_SYS_CLASS_TTY="$WORK_DIR/sys/class/tty" \
    MICROPANEL_DEV_ROOT="$WORK_DIR/dev" \
    MICROPANEL_LEGACY_FLASHER="$WORK_DIR/bin/flashrh850.sh" \
    MICROPANEL_RH850_TOOL="$WORK_DIR/bin/rh850-tool" \
    MICROPANEL_RH850_BACKUP_ROOT="$WORK_DIR/backups" \
    "$TOOL" "$@"
}

fallback_output=$(run_tool --board=983hh --npj=/tmp/983HH.npj --info)
printf '%s\n' "$fallback_output" | grep -q 'Bluebox not detected'
printf '%s\n' "$fallback_output" | grep -q 'legacy:--npj=/tmp/983HH.npj:--info:'

s4_fallback_output=$(run_tool --board=spartan7-9090 --info)
printf '%s\n' "$s4_fallback_output" | grep -q 'Pi GPIO/local-UART transport for S7-9090'
printf '%s\n' "$s4_fallback_output" | grep -q 'rh850-tool:probe:--target=r7f7015873:--transport=pi-gpio:'

usb_dir="$WORK_DIR/sys/usb/1-1"
for interface in 0 1; do
    tty=ttyUSB$interface
    interface_dir="$usb_dir/1-1:1.$interface"
    mkdir -p "$interface_dir/$tty"
    mkdir -p "$WORK_DIR/sys/class/tty/$tty"
    printf '%02d\n' "$interface" > "$interface_dir/bInterfaceNumber"
    ln -s "$interface_dir/$tty" "$WORK_DIR/sys/class/tty/$tty/device"
    ln -s /dev/null "$WORK_DIR/dev/$tty"
done
printf '0403\n' > "$usb_dir/idVendor"
printf 'a9a0\n' > "$usb_dir/idProduct"

probe_output=$(run_tool --board=983hh --npj=/tmp/983HH.npj --info)
printf '%s\n' "$probe_output" | grep -q 'MCU probe completed successfully'
printf '%s\n' "$probe_output" | grep -Fq '[SUCCESS]'
printf '%s\n' "$probe_output" | grep -q 'rh850-tool:probe:--target=r7f7016863:--transport=bluebox:--port='

backup_output=$(run_tool --board=983hh --backup)
printf '%s\n' "$backup_output" | grep -q 'rh850-tool:backup:--output='
printf '%s\n' "$backup_output" | grep -Fq '[SUCCESS] MCU backup completed successfully'

mkdir -p "$WORK_DIR/backups/983hh"
printf 'backup' > "$WORK_DIR/backups/983hh/r7f7016863-codeflash-20260806-000000.bin"
recover_output=$(run_tool --board=983hh --recover)
printf '%s\n' "$recover_output" | grep -q 'rh850-tool:recover:--image='
printf '%s\n' "$recover_output" | grep -q 'r7f7016863-codeflash-20260806-000000.bin'

printf 'firmware' > "$WORK_DIR/firmware.bin"
flash_output=$(run_tool --board=spartan7-9090 --bios-autorun="$WORK_DIR/firmware.bin")
printf '%s\n' "$flash_output" | grep -q 'rh850-tool:flash:--image='
printf '%s\n' "$flash_output" | grep -q -- '--target=r7f7015873:'

printf '%s\n' 'rh850-flash-auto offline tests passed'
