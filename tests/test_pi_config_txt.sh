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
#     loaded drivers match it;
#  5. the kernel command line: every cmdline.txt on the boot partition (root,
#     A/, B/) carries exactly the video= token the type needs - golden
#     tests/golden/pi-config-txt/<type>.cmdline.txt.in, or cmdline.txt.in
#     (unchanged) for a type without one - written by --type and restored by
#     --apply-derived after an A/B update replaced a slot's cmdline.
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
# A/B boot partition cmdlines, as ab-finalize-layout.sh leaves them: the root
# one and slot A's boot MP_ROOT_A, slot B's MP_ROOT_B. $2: the golden to render.
seed_cmdlines() { # $1=boot dir [$2=golden template]
    mkdir -p "$1/A" "$1/B"
    sed 's/@ROOT@/MP_ROOT_A/' "${2:-$golden/cmdline.txt.in}" > "$1/cmdline.txt"
    sed 's/@ROOT@/MP_ROOT_A/' "${2:-$golden/cmdline.txt.in}" > "$1/A/cmdline.txt"
    sed 's/@ROOT@/MP_ROOT_B/' "${2:-$golden/cmdline.txt.in}" > "$1/B/cmdline.txt"
}
cmdline_golden() { # $1=type: the type's golden cmdline template
    if [ -f "$golden/$1.cmdline.txt.in" ]; then echo "$golden/$1.cmdline.txt.in"; else echo "$golden/cmdline.txt.in"; fi
}
check_cmdlines() { # $1=boot dir $2=type $3=what: each file equals the type's golden
    for cl_slot in cmdline.txt:MP_ROOT_A A/cmdline.txt:MP_ROOT_A B/cmdline.txt:MP_ROOT_B; do
        cl_file=${cl_slot%%:*}; cl_label=${cl_slot#*:}
        if ! sed "s/@ROOT@/$cl_label/" "$(cmdline_golden "$2")" | cmp -s - "$1/$cl_file"; then
            fail "$3: $cl_file for $2"
            sed "s/@ROOT@/$cl_label/" "$(cmdline_golden "$2")" | diff - "$1/$cl_file" >&2 || true
        fi
    done
}

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
    seed_cmdlines "$boot"
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
    # Every cmdline.txt: the type's golden, after two writes (one token, not two).
    check_cmdlines "$boot" "$type" "split write"
    [ -z "$(find "$boot" -name '*.new')" ] || fail "temporary file left on the boot partition ($type)"
    unset MICROPANEL_DEFAULTS MICROPANEL_SYSROOT
done
echo "ok  split form: redirect, base + include = single-file lines in order, query, rolling backup, cmdline goldens"

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
# dmesg: once the serializer is loaded, its "DP video back" line when the test
# says the video comes back by itself (STUB_VIDEO_BACK=1), or its DP RX kick
# line (STUB_VIDEO_BACK=kick).
cat > "$stub_bin/dmesg" <<'EOF'
#!/bin/sh
[ -d "$STUB_SYS/hh983_serializer" ] || exit 0
case "$STUB_VIDEO_BACK" in
    1) echo "[   14.8] hh983-serializer 1-0018: DP video back (VP_STS=0x01), restoring 984 video stream" ;;
    kick) echo "[   18.9] hh983-serializer 1-0018: No DP video for 3 polls (VP_STS=0x00, PHY=0x00), kicking the DP RX (1 of 6)" ;;
esac
exit 0
EOF
chmod +x "$stub_bin/dmesg"
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
        MICROPANEL_I2CSET="$stub_bin/i2cset" MICROPANEL_SLEEP=true MICROPANEL_DMESG="$stub_bin/dmesg" \
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
# ...unless the video came back by itself after the reload: a toggle then drops
# a retrained link, and on the OTS-OLED that black-latches the panel.
STUB_VIDEO_BACK=1 derive ots-oled-17 "hh983_serializer himax_mmi"
root="$work/derive-ots-oled-17"
[ "$(cat "$root/stub.log")" = "$(printf '%s\n' 'rmmod himax_mmi' 'rmmod hh983_serializer' 'modprobe hh983-serializer' 'modprobe himax_oled')" ] || {
    fail 'apply-derived: HPD toggle after the video came back by itself'; cat "$root/stub.log" >&2; }
# ...or while the driver is kicking the DP RX: the kick is its own HPD toggle.
STUB_VIDEO_BACK=kick derive ots-oled-17 "hh983_serializer himax_mmi"
root="$work/derive-ots-oled-17"
[ "$(cat "$root/stub.log")" = "$(printf '%s\n' 'rmmod himax_mmi' 'rmmod hh983_serializer' 'modprobe hh983-serializer' 'modprobe himax_oled')" ] || {
    fail 'apply-derived: HPD toggle during a driver DP RX kick'; cat "$root/stub.log" >&2; }
# An A/B image blacklists the drivers' alias autoload, so at boot nothing is
# loaded yet: one load each, in order, no reload and no HPD toggle.
derive ots-oled-17 ""
root="$work/derive-ots-oled-17"
[ "$(cat "$root/stub.log")" = "$(printf '%s\n' 'modprobe hh983-serializer' 'modprobe himax_oled')" ] || {
    fail 'apply-derived: blacklisted autoload - expected one load each and no toggle'; cat "$root/stub.log" >&2; }
[ -d "$root/sys/himax_oled" ] && [ ! -d "$root/sys/himax_mmi" ] || fail 'apply-derived: wrong touch driver loaded for ots-oled-17'
# oled-ib-17: the same HX8530 as the OTS-OLED, but its firmware needs Himax's
# A09.23 driver (himax_oledib); himax_oled, if something loaded it, goes.
derive oled-ib-17 "himax_oled"
root="$work/derive-oled-ib-17"
grep -q 'config_mode=2 ib_touch=1' "$root/etc/modprobe.d/hh983.conf" || fail 'apply-derived: oled-ib-17 options not written'
grep -qx 'softdep himax_oledib pre: hh983-serializer' "$root/etc/modprobe.d/hh983.conf" || fail 'apply-derived: oled-ib-17 softdep'
grep -qx 'blacklist himax_oled' "$root/etc/modprobe.d/blacklist-himax-mmi.conf" || fail 'apply-derived: oled-ib-17 does not blacklist himax_oled'
[ "$(cat "$root/stub.log")" = "$(printf '%s\n' 'rmmod himax_oled' 'modprobe hh983-serializer' 'modprobe himax_oledib')" ] || {
    fail 'apply-derived: oled-ib-17 load sequence'; cat "$root/stub.log" >&2; }
[ -d "$root/sys/himax_oledib" ] && [ ! -d "$root/sys/himax_oled" ] && [ ! -d "$root/sys/himax_mmi" ] || \
    fail 'apply-derived: wrong touch driver loaded for oled-ib-17'
grep -qx 'blacklist himax_oledib' "$work/derive-ots-oled-17/etc/modprobe.d/blacklist-himax-mmi.conf" || \
    fail 'apply-derived: ots-oled-17 does not blacklist himax_oledib'
# 12.3: the defaults are already right; nothing is unloaded.
derive 12.3 "hh983_serializer himax_mmi"
root="$work/derive-12.3"
[ "$(cat "$root/stub.log")" = "$(printf '%s\n' 'modprobe hh983-serializer' 'modprobe himax_mmi')" ] || {
    fail 'apply-derived: a default-options type reloaded drivers'; cat "$root/stub.log" >&2; }
# 3x-qvue: no touch driver at all, serializer in video-only mode.
derive 3x-qvue "hh983_serializer himax_mmi"
root="$work/derive-3x-qvue"
grep -q 'config_mode=2' "$root/etc/modprobe.d/hh983.conf" || fail 'apply-derived: 3x-qvue options'
[ ! -d "$root/sys/himax_mmi" ] && [ ! -d "$root/sys/himax_oled" ] && [ ! -d "$root/sys/himax_oledib" ] || \
    fail 'apply-derived: 3x-qvue left a touch driver loaded'
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

# --- 5. kernel command line (framebuffer depth) ------------------------------------
# The goldens themselves: 3x-qvue adds exactly its 32 bpp mode, nothing else.
[ "$(cat "$golden/3x-qvue.cmdline.txt.in")" = "$(cat "$golden/cmdline.txt.in") video=HDMI-A-1:4500x298-32@60" ] || \
    fail 'cmdline goldens: 3x-qvue is not the plain cmdline plus video=HDMI-A-1:4500x298-32@60'
split_env() { # $1=root: the split-form environment for the calls that follow
    echo "MICROPANEL_BOOT_CONFIG=$1/boot/micropanel-display.txt" > "$1/defaults"
    export MICROPANEL_DEFAULTS="$1/defaults" MICROPANEL_SYSROOT="$1"
}
write_type() { sh "$script" --configspath="$configs" --input=/boot/firmware/config.txt --type="$1" --no-reboot; }
apply_derived() {
    STUB_LOG="$MICROPANEL_SYSROOT/stub.log" STUB_SYS="$MICROPANEL_SYSROOT/sys" \
        MICROPANEL_MODPROBE="$stub_bin/modprobe" MICROPANEL_RMMOD="$stub_bin/rmmod" \
        MICROPANEL_SYS_MODULE_DIR="$MICROPANEL_SYSROOT/sys" MICROPANEL_I2CSET="$stub_bin/i2cset" \
        MICROPANEL_SLEEP=true MICROPANEL_DMESG="$stub_bin/dmesg" \
        sh "$script" --configspath="$configs" --apply-derived
}

# Switching away from 3x-qvue takes the token out again.
root="$work/cmdline-switch"; new_sysroot "$root"; mkdir -p "$root/boot" "$root/sys"; seed_cmdlines "$root/boot"
split_env "$root"
write_type 3x-qvue >/dev/null
check_cmdlines "$root/boot" 3x-qvue "switch: 3x-qvue"
write_type 12.3 >/dev/null
check_cmdlines "$root/boot" 12.3 "switch: 3x-qvue -> 12.3"
write_type 3x-qvue >/dev/null
check_cmdlines "$root/boot" 3x-qvue "switch: 12.3 -> 3x-qvue"
# An unchanged cmdline is not rewritten (--apply-derived runs on every boot).
out=$(write_type 3x-qvue)
case "$out" in *"Kernel command line updated"*) fail 'an unchanged cmdline was rewritten' ;; esac
out=$(apply_derived)
case "$out" in *"Kernel command line updated"*) fail 'apply-derived rewrote an unchanged cmdline' ;; esac
check_cmdlines "$root/boot" 3x-qvue "apply-derived, nothing to change"

# An A/B update renders the target slot's cmdline from the release template,
# without the token: --apply-derived on the next boot puts it back, in that
# slot only, and says the change waits for the next boot.
sed 's/@ROOT@/MP_ROOT_B/' "$golden/cmdline.txt.in" > "$root/boot/B/cmdline.txt"
out=$(apply_derived)
check_cmdlines "$root/boot" 3x-qvue "apply-derived after an update"
case "$out" in *"next boot: $root/boot/B/cmdline.txt"*) ;; *) fail "apply-derived did not report the restored slot (got: $out)" ;; esac
case "$out" in *"A/cmdline.txt"*) fail 'apply-derived reported an unchanged slot' ;; esac
# ...and a type that needs none loses a token the release template carried.
write_type 12.3 >/dev/null
sed 's/@ROOT@/MP_ROOT_A/' "$golden/3x-qvue.cmdline.txt.in" > "$root/boot/A/cmdline.txt"
apply_derived >/dev/null
check_cmdlines "$root/boot" 12.3 "apply-derived, token from a release template"
unset MICROPANEL_DEFAULTS MICROPANEL_SYSROOT

# Only the owned token changes: another HDMI-A-1 mode is replaced, a token for
# the second connector and every other token stay, in order.
root="$work/cmdline-foreign"; new_sysroot "$root"; mkdir -p "$root/boot"
printf '%s\n' 'console=tty1 video=HDMI-A-1:1920x1080@60 root=LABEL=MP_ROOT_A video=HDMI-A-2:1280x720@60 quiet' > "$root/boot/cmdline.txt"
split_env "$root"
write_type 3x-qvue >/dev/null
[ "$(cat "$root/boot/cmdline.txt")" = 'console=tty1 root=LABEL=MP_ROOT_A video=HDMI-A-2:1280x720@60 quiet video=HDMI-A-1:4500x298-32@60' ] || \
    fail "foreign tokens: got '$(cat "$root/boot/cmdline.txt")'"
write_type edid >/dev/null
[ "$(cat "$root/boot/cmdline.txt")" = 'console=tty1 root=LABEL=MP_ROOT_A video=HDMI-A-2:1280x720@60 quiet' ] || \
    fail "foreign tokens after edid: got '$(cat "$root/boot/cmdline.txt")'"
# Missing slot directories are not created.
[ ! -e "$root/boot/A" ] && [ ! -e "$root/boot/B" ] || fail 'a cmdline was created for a slot that does not exist'
unset MICROPANEL_DEFAULTS MICROPANEL_SYSROOT

# Single-file images: the cmdline.txt beside config.txt.
root="$work/cmdline-single"; new_sysroot "$root"
: > "$root/config.txt"
sed 's/@ROOT@/MP_ROOT_A/' "$golden/cmdline.txt.in" > "$root/cmdline.txt"
MICROPANEL_DEFAULTS="$work/absent" MICROPANEL_SYSROOT="$root" \
    sh "$script" --configspath="$configs" --input="$root/config.txt" --type=3x-qvue --no-reboot >/dev/null
sed 's/@ROOT@/MP_ROOT_A/' "$golden/3x-qvue.cmdline.txt.in" | cmp -s - "$root/cmdline.txt" || fail 'single-file: cmdline.txt beside config.txt'
cmp -s "$root/config.txt" "$golden/3x-qvue.config.txt" || fail 'single-file: config.txt changed by the cmdline step'
echo "ok  kernel cmdline: 32 bpp token for 3x-qvue only, every slot, switch back, restored after an update, foreign tokens kept"

if [ "$failures" -gt 0 ]; then
    echo "pi-config-txt: $failures FAILURE(S)" >&2
    exit 1
fi
echo "pi-config-txt: all checks passed"
