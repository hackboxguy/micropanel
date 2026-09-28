#!/bin/sh
# pi-config-txt.sh: single-file and split (A/B) boot configuration.
#
# No root and no hardware: MICROPANEL_SYSROOT redirects the files a display
# type derives (modprobe/modules-load/als-dimmer), MICROPANEL_DEFAULTS the
# /etc/default/micropanel switch, and stub commands stand in for modprobe and
# rmmod. Run: sh tests/test_pi_config_txt.sh   (also a CTest with BUILD_TESTS)
#
# What it proves, for every display type in configs/display-configs.conf:
#  1. without /etc/default/micropanel the written config.txt is byte-identical
#     to tests/golden/pi-config-txt/<type>.config.txt (the output before the
#     split existed);
#  2. with it, --input=/boot/firmware/config.txt writes the display file
#     instead, and base config + included display file give exactly the
#     single-file config's lines, in the same order;
#  3. the split query reads the display file back as the same type;
#  4. --apply-derived writes the type's module configuration and makes the
#     loaded drivers match it.
set -eu

repo=$(cd "$(dirname "$0")/.." && pwd)
script="$repo/scripts/pi-config-txt.sh"
configs="$repo/configs"
golden="$repo/tests/golden/pi-config-txt"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
failures=0
fail() { echo "FAIL: $*" >&2; failures=$((failures + 1)); }

types=$(grep -v '^#' "$configs/display-configs.conf" | grep -v '^$' | cut -d: -f1)
[ -n "$types" ] || { echo "no display types found" >&2; exit 1; }

new_sysroot() { # $1=dir: the image's default derived files
    mkdir -p "$1/etc/modprobe.d" "$1/etc/modules-load.d" "$1/home/pi/als-dimmer/etc/als-dimmer"
    echo "options hh983-serializer config_mode=1" > "$1/etc/modprobe.d/hh983.conf"
    printf '%s\n' '# default' hh983-serializer himax_mmi > "$1/etc/modules-load.d/custom-drivers.conf"
}
config_lines() { grep -v '^#' "$1" | sed '/^[[:space:]]*$/d'; }

# --- 1. single-file output unchanged ------------------------------------------
for type in $types; do
    root="$work/legacy-$type"; new_sysroot "$root"
    : > "$root/config.txt"
    MICROPANEL_DEFAULTS="$work/absent" MICROPANEL_SYSROOT="$root" \
        sh "$script" --configspath="$configs" --input="$root/config.txt" --type="$type" --no-reboot >/dev/null
    cmp -s "$root/config.txt" "$golden/$type.config.txt" || fail "single-file output changed for $type"
done
echo "ok  single-file config.txt byte-identical to the golden output for every type"

# The config.txt the image build installs (micropanel-hook.sh copies it to the
# boot partition) is the edid rendering, so a fresh image queries as a known type.
cmp -s "$configs/config.txt" "$golden/edid.config.txt" || fail 'configs/config.txt is not the edid rendering of the template'
root="$work/shipped"; new_sysroot "$root"
shipped_type=$(MICROPANEL_DEFAULTS="$work/absent" MICROPANEL_SYSROOT="$root" \
    sh "$script" --configspath="$configs" --input="$configs/config.txt" --query-config)
[ "$shipped_type" = edid ] || fail "configs/config.txt queries as '$shipped_type', not edid"
# No overlay that exists nowhere: GPIO22 (the RH850 reset line) is pulled up
# with the firmware's own gpio= directive.
grep -q 'gpio-pullup' "$configs/config-base.txt.in" && fail 'config-base.txt.in names the nonexistent gpio-pullup overlay'
grep -qx 'gpio=22=ip,pu' "$configs/config-base.txt.in" || fail 'config-base.txt.in lacks the GPIO22 pull-up'
echo "ok  shipped configs/config.txt is the edid rendering and queries as edid"

# --- 2./3. split form ---------------------------------------------------------
for type in $types; do
    root="$work/split-$type"; new_sysroot "$root"
    boot="$root/boot"; mkdir -p "$boot"
    defaults="$root/default-micropanel"
    echo "MICROPANEL_BOOT_CONFIG=$boot/micropanel-display.txt" > "$defaults"
    export MICROPANEL_DEFAULTS="$defaults" MICROPANEL_SYSROOT="$root"
    sh "$script" --configspath="$configs" --emit-base="$boot/config.txt"
    # The callers' literal path is redirected to the display file.
    sh "$script" --configspath="$configs" --input=/boot/firmware/config.txt --type="$type" --no-reboot >/dev/null
    [ -f "$boot/micropanel-display.txt" ] || { fail "split write did not create the display file for $type"; continue; }
    grep -q "^# micropanel-display-type: $type\$" "$boot/micropanel-display.txt" || fail "no type marker for $type"
    grep -q '^hdmi_timings=' "$boot/config.txt" && fail "base config.txt carries hdmi_timings ($type)"
    grep -q '^dtoverlay=himax-touch' "$boot/config.txt" && fail "base config.txt carries the touch overlay ($type)"
    [ "$(grep -c '^include ' "$boot/config.txt")" = 1 ] || fail "base config.txt must include exactly once ($type)"
    grep -qx 'include micropanel-display.txt' "$boot/config.txt" || fail "base config.txt does not include micropanel-display.txt"
    # Expand the include and compare, in order, with the single-file config.
    expanded="$root/expanded.txt"
    while IFS= read -r line; do
        if [ "$line" = 'include micropanel-display.txt' ]; then cat "$boot/micropanel-display.txt"; else printf '%s\n' "$line"; fi
    done < "$boot/config.txt" > "$expanded"
    if [ "$(config_lines "$expanded")" != "$(config_lines "$golden/$type.config.txt")" ]; then
        fail "base + display file differ from the single-file config for $type"
        diff "$golden/$type.config.txt" "$expanded" >&2 || true
    fi
    # Read back through the callers' path.
    queried=$(sh "$script" --configspath="$configs" --input=/boot/firmware/config.txt --query-config)
    [ "$queried" = "$type" ] || fail "split query for $type answered '$queried'"
    # A second write keeps one rolling backup and never a timestamped copy.
    sh "$script" --configspath="$configs" --input=/boot/firmware/config.txt --type="$type" --no-reboot >/dev/null
    [ -f "$boot/micropanel-display.txt.bak" ] || fail "no rolling backup for $type"
    [ -z "$(find "$boot" -name '*.backup.*')" ] || fail "timestamped backup on the boot partition ($type)"
    unset MICROPANEL_DEFAULTS MICROPANEL_SYSROOT
done
echo "ok  split form: redirect, base + include = single-file lines in order, query, rolling backup"

# --- 4. --apply-derived ----------------------------------------------------------
stub_bin="$work/bin"; mkdir -p "$stub_bin"
for command in modprobe rmmod i2cset; do
    cat > "$stub_bin/$command" <<EOF
#!/bin/sh
echo "$command \$*" >> "\$STUB_LOG"
module=\$(echo "\$1" | tr '-' '_')
case "$command" in
    modprobe) mkdir -p "\$STUB_SYS/\$module" ;;
    rmmod) rm -rf "\$STUB_SYS/\$module" ;;
esac
exit 0
EOF
    chmod +x "$stub_bin/$command"
done
derive() { # $1=type $2=modules loaded before (by udev / modules-load at boot)
    root="$work/derive-$1"; rm -rf "$root"; new_sysroot "$root"; mkdir -p "$root/boot" "$root/sys"
    for module in $2; do mkdir -p "$root/sys/$module"; done
    echo "MICROPANEL_BOOT_CONFIG=$root/boot/micropanel-display.txt" > "$root/defaults"
    MICROPANEL_DEFAULTS="$root/defaults" MICROPANEL_SYSROOT="$root" \
        sh "$script" --configspath="$configs" --input=/boot/firmware/config.txt --type="$1" --no-reboot >/dev/null
    new_sysroot "$root"   # the volatile root is back to the image defaults
    : > "$root/stub.log"
    STUB_LOG="$root/stub.log" STUB_SYS="$root/sys" MICROPANEL_DEFAULTS="$root/defaults" MICROPANEL_SYSROOT="$root" \
        MICROPANEL_MODPROBE="$stub_bin/modprobe" MICROPANEL_RMMOD="$stub_bin/rmmod" MICROPANEL_SYS_MODULE_DIR="$root/sys" \
        MICROPANEL_I2CSET="$stub_bin/i2cset" MICROPANEL_SLEEP=true \
        sh "$script" --configspath="$configs" --apply-derived >/dev/null
}
# ots-oled-17: udev loaded the serializer with the default options and
# modules-load the multi-chip touch driver; both are wrong for this panel.
derive ots-oled-17 "hh983_serializer himax_mmi"
root="$work/derive-ots-oled-17"
grep -q 'config_mode=0 ots_touch=1' "$root/etc/modprobe.d/hh983.conf" || fail 'apply-derived: ots-oled-17 options not written'
grep -qx 'blacklist himax_mmi' "$root/etc/modprobe.d/blacklist-himax-mmi.conf" || fail 'apply-derived: himax_mmi not blacklisted'
# ...and after that reload the DP source must be asked to retrain: the HPD
# toggle (LINK_ENABLE 0, then 1) follows the loads.
hpd_sequence=$(for value in 0x00 0x01; do
    printf '%s\n' "i2cset -y -f 1 0x18 0x48 0x01" "i2cset -y -f 1 0x18 0x49 0x00" "i2cset -y -f 1 0x18 0x4a 0x00" \
        "i2cset -y -f 1 0x18 0x4b $value" "i2cset -y -f 1 0x18 0x4c 0x00" "i2cset -y -f 1 0x18 0x4d 0x00" "i2cset -y -f 1 0x18 0x4e 0x00"
done)
[ "$(cat "$root/stub.log")" = "$(printf '%s\n' 'rmmod himax_mmi' 'rmmod hh983_serializer' 'modprobe hh983-serializer' 'modprobe himax_oled' "$hpd_sequence")" ] || {
    fail 'apply-derived: ots-oled-17 reload sequence (with the HPD toggle after it)'; cat "$root/stub.log" >&2; }
# An A/B image blacklists the drivers' alias autoload, so at boot nothing is
# loaded yet: one load each, in order, no reload and no HPD toggle.
derive ots-oled-17 ""
root="$work/derive-ots-oled-17"
[ "$(cat "$root/stub.log")" = "$(printf '%s\n' 'modprobe hh983-serializer' 'modprobe himax_oled')" ] || {
    fail 'apply-derived: blacklisted autoload - expected one load each and no toggle'; cat "$root/stub.log" >&2; }
[ -d "$root/sys/himax_oled" ] && [ ! -d "$root/sys/himax_mmi" ] || fail 'apply-derived: wrong touch driver loaded for ots-oled-17'
# 12.3: the defaults are already right; nothing is unloaded.
derive 12.3 "hh983_serializer himax_mmi"
root="$work/derive-12.3"
[ "$(cat "$root/stub.log")" = "$(printf '%s\n' 'modprobe hh983-serializer' 'modprobe himax_mmi')" ] || {
    fail 'apply-derived: a default-options type reloaded drivers'; cat "$root/stub.log" >&2; }
# 3x-qvue: no touch driver at all, serializer in video-only mode.
derive 3x-qvue "hh983_serializer himax_mmi"
root="$work/derive-3x-qvue"
grep -q 'config_mode=2' "$root/etc/modprobe.d/hh983.conf" || fail 'apply-derived: 3x-qvue options'
[ ! -d "$root/sys/himax_mmi" ] && [ ! -d "$root/sys/himax_oled" ] || fail 'apply-derived: 3x-qvue left a touch driver loaded'
# A display file without a known type changes nothing and says so.
root="$work/derive-unknown"; new_sysroot "$root"; mkdir -p "$root/boot"
echo "MICROPANEL_BOOT_CONFIG=$root/boot/micropanel-display.txt" > "$root/defaults"
echo 'hdmi_group=2' > "$root/boot/micropanel-display.txt"
if MICROPANEL_DEFAULTS="$root/defaults" MICROPANEL_SYSROOT="$root" \
    sh "$script" --configspath="$configs" --apply-derived >/dev/null 2>&1; then
    fail 'apply-derived accepted a display file with no type'
fi
grep -q 'config_mode=1' "$root/etc/modprobe.d/hh983.conf" || fail 'apply-derived changed the defaults without a type'
echo "ok  --apply-derived: options written, wrong drivers unloaded, serializer reloaded only when needed"

if [ "$failures" -gt 0 ]; then
    echo "pi-config-txt: $failures FAILURE(S)" >&2
    exit 1
fi
echo "pi-config-txt: all checks passed"
