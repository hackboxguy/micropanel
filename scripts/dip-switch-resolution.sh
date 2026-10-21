#!/bin/sh
# Auto-detect display resolution from DIP switch (PCF8574 at 0x20 on i2c-3)
# and reconfigure HDMI timing if it doesn't match current config.
#
# DIP switch mapping:
#   0xf7  -> 12.3
#   0x7a  -> 12.3-nq1
#   0x6d  -> 12.3-nq1v1.1 (DIP setting 01001001, active-low)
#   0xfb  -> 14.6-fhd
#   0xfd  -> 14.6-2k5
#   0xfe  -> 15.6-2k5
#   0xdf  -> 17.3-3k
#   0x79  -> ots-oled-17 (DIP setting 01100001, active-low)
#   0x39  -> ots-oled-17 on a DS90UH983 CS2.0 983HH (DIP setting 01100011)
#   0x75  -> 3x-qvue (DIP setting 01010001, active-low)
#   0x35  -> 3x-qvue on a DS90UH983 CS2.0 983HH (DIP setting 01010011)
#   0x5d  -> oled-ib-17 (DIP setting 01000101, active-low)
#   0x1d  -> oled-ib-17 on a DS90UH983 CS2.0 983HH (DIP setting 01000111)
#
# DIP6 selects the 983HH's CS2.0 init sequence (983_manager profiles
# ots_oled_17_cs2, qvue3_cs2_6g75 and oled_ib_17_cs2); the panel, and so the Pi's display
# type, is the same as without it.
#   0xef  -> 27
#   0xff  -> soft DIP (every switch off): see below
#
# Soft DIP: with every switch off the 983HH's RH850 (983_manager v01.13+) runs
# no profile and waits. The display type chosen on the Pi (Stream Deck HDMI
# timing buttons -> pi-config-txt.sh, persistent on the SD card) is turned back
# into the DIP on-mask that type would have, written to the RH850's SOFT_DIP
# register (0x67 reg 0x0316) and run with a cold re-init (0x0300 = 0x11), on
# every boot. The RH850 keeps nothing; the Pi's config is the setting.
#
# 14.6" FHD variant: the direct-drive (14.6-fhd) and scan-mode EJ (14.6-fhd-ej)
# panels share one DIP position, so the switch cannot tell them apart. Their
# FPGA bitstreams can: reg 0x10 (FPGA Chip ID) reads 00 00 01 46 on the
# direct-drive build and 01 00 01 46 on the EJ build (byte 0 = 0x01 =
# scan backlight, pixelpipe-fpga configs/autoone_14p6_xc7s50_linglong_scan).
# Once the DIP or soft-DIP type is settled, a 14.6-fhd* type is switched to
# the variant the FPGA reports. The two types differ only in their marker line
# and als-dimmer config, so this needs no reboot.
#
# Reboot loop guard: at most one auto-reboot per mismatch.
# Flag file is created before rebooting; if it exists on next boot
# and there's still a mismatch, no reboot is triggered.
#
# Usage:
#   dip-switch-resolution.sh [--configspath=DIR] [--input=FILE] [-v]

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
I2C_BUS=3
DIP_ADDR=0x20
DIP_WAIT_S=15          # how long to wait for i2c-3 and the PCF8574 at boot
REBOOT_FLAG="/var/lib/micropanel/dip-reboot-pending"
VERBOSE=0

# Default paths (overridable via args)
CONFIGS_PATH=""
INPUT_FILE=""

# Parse arguments
for arg in "$@"; do
    case "$arg" in
        --configspath=*) CONFIGS_PATH="${arg#*=}" ;;
        --input=*) INPUT_FILE="${arg#*=}" ;;
        -v|--verbose) VERBOSE=1 ;;
    esac
done

# Build pi-config-txt.sh argument string
build_config_args() {
    local args=""
    if [ -n "$CONFIGS_PATH" ]; then
        args="$args --configspath=$CONFIGS_PATH"
    fi
    if [ -n "$INPUT_FILE" ]; then
        args="$args --input=$INPUT_FILE"
    fi
    echo "$args"
}

log() {
    if [ $VERBOSE -eq 1 ]; then
        echo "[dip-switch] $1"
    fi
}

# --- Driver handling around the post-reconfigure RH850 reset -------------------
#
# The reset below (GPIO 22) restarts the 983HH RH850, which re-runs its whole
# bit-bang I2C init on the same bus the Pi is using. Leaving the serializer
# driver loaded across that means two masters on one bus; it killed the RH850
# init outright once (0x67 never answered afterwards). It also overwrites the
# driver's touch routing, so the touch driver has to come back afterwards too.
# Which modules those are is whatever this display type configured, not a
# hard-coded list: a video-only type loads the serializer alone.
MODULES_CONF="/etc/modules-load.d/custom-drivers.conf"

# lsmod prints module names with underscores; the conf file may use hyphens.
norm_mod() {
    echo "$1" | tr '-' '_'
}

module_is_loaded() {
    lsmod 2>/dev/null | awk '{print $1}' | grep -qx "$(norm_mod "$1")"
}

# Modules this display type wants, in load order.
configured_modules() {
    if [ -f "$MODULES_CONF" ]; then
        grep -v '^[[:space:]]*#' "$MODULES_CONF" 2>/dev/null | grep -v '^[[:space:]]*$'
    fi
}

# Unload in reverse order: touch first, then the serializer it depends on.
stop_drivers() {
    _rev=""
    for _m in $(configured_modules); do
        _rev="$_m $_rev"
    done
    for _m in $_rev; do
        if module_is_loaded "$_m"; then
            log "  unloading $_m"
            rmmod "$(norm_mod "$_m")" 2>/dev/null || log "  WARNING: rmmod $_m failed, continuing"
        fi
    done
}

# How many times the serializer driver has logged DP video coming back or a DP
# RX kick; see hpd_toggle.
dp_driver_event_count() {
    dmesg 2>/dev/null | grep -cE "hh983-serializer.*(DP video back|kicking the DP RX)"
}

start_drivers() {
    _video_back_before=$(dp_driver_event_count)
    for _m in $(configured_modules); do
        log "  loading $_m"
        modprobe "$_m" 2>/dev/null || log "  WARNING: modprobe $_m failed"
        sleep 1
    done
}

# The RH850 takes a few seconds to run its profile after a reset; 0x67 only
# appears once it has finished and handed the bus to its status slave.
wait_for_rh850() {
    _t=0
    while [ "$_t" -lt 20 ]; do
        if i2ctransfer -y -f 1 w2@0x67 0x00 0x00 r8@0x67 >/dev/null 2>&1; then
            log "  RH850 answered at 0x67 after ${_t}s"
            return 0
        fi
        sleep 1
        _t=$((_t + 1))
    done
    log "  WARNING: RH850 did not answer at 0x67 within ${_t}s"
    return 1
}

# 983 APB LINK_ENABLE (0x000) through the indirect window.
apb_link_enable() {
    i2cset -y -f 1 0x18 0x48 0x01
    i2cset -y -f 1 0x18 0x49 0x00
    i2cset -y -f 1 0x18 0x4a 0x00
    i2cset -y -f 1 0x18 0x4b "$1"
    i2cset -y -f 1 0x18 0x4c 0x00
    i2cset -y -f 1 0x18 0x4d 0x00
    i2cset -y -f 1 0x18 0x4e 0x00
}

# Read 983 APB LINK_ENABLE (0x000) back, for the log.
apb_link_enabled() {
    i2cset -y -f 1 0x18 0x49 0x00
    i2cset -y -f 1 0x18 0x4a 0x00
    i2cset -y -f 1 0x18 0x48 0x03
    _le=$(i2cget -y -f 1 0x18 0x4b 2>/dev/null)
    i2cset -y -f 1 0x18 0x48 0x01
    echo "${_le:-unreadable}"
}

# 983 VP0 synced to its DP input: VP page (indirect 0x40 = 0x31), register 0x30,
# bit 0 TIMING_GEN_STS. Only while hh983-serializer is unloaded: the driver
# uses the same indirect window.
vp0_synced() {
    i2cset -y -f 1 0x18 0x40 0x31
    i2cset -y -f 1 0x18 0x41 0x30
    _vp=$(i2cget -y -f 1 0x18 0x42 2>/dev/null)
    [ -n "$_vp" ] && [ $(( _vp & 1 )) -eq 1 ]
}

# oled-ib-17: wait for the 983's VP0 to sync once the DP source has been let
# back in, polling VP0 alone (one indirect read a second) and loading nothing
# until it syncs or DP_SYNC_WAIT_S has passed. The sync time is logged so a
# boot that went wrong can be told from one that did not.
await_dp_sync() {
    _t=0
    while [ "$_t" -lt "$DP_SYNC_WAIT_S" ]; do
        if vp0_synced; then
            log "  DP video synced (VP0 $_vp) ${_t}s after the source came back"
            return 0
        fi
        sleep 1
        _t=$((_t + 1))
    done
    log "  WARNING: no DP video sync ${DP_SYNC_WAIT_S}s after the source came back (VP0 ${_vp:-unreadable})"
    return 1
}

# A fresh probe deliberately does no HPD toggle (it would tear down a DP link
# that is already up at boot), so after a reload the DP input stays down until
# something asks the source to retrain.
# pi-config-txt.sh carries a copy of apb_link_enable/hpd_toggle (for
# --apply-derived after a serializer reload); keep the two identical.
#
# Only when the video does not come back by itself: on the OTS-OLED the Pi's DP
# link retrains on its own about 2 s after the reload, the driver restores the
# 984 stream, and a toggle then drops the link under it. The 984 DTG wedges
# (measured H total 0), two digital resets do not clear it, and the driver's
# DTG pulse black-latches the panel until a power cycle - every boot on .153
# with image 2.15. So wait a few seconds for the driver to report the video.
# Nor while the driver is kicking the DP RX (CS2.0; a kick is its own HPD
# toggle): on the 15.6" 2.5K a toggle 4 s after the kick dropped the link just
# as the video came up, and the 984 DTG wedged the same way.
hpd_toggle() {
    _w=0
    while [ "$_w" -lt 4 ]; do
        if [ "$(dp_driver_event_count)" -gt "${_video_back_before:-0}" ]; then
            log "  DP video back by itself or the driver is kicking the DP RX, no HPD toggle"
            return 0
        fi
        sleep 1
        _w=$((_w + 1))
    done
    log "  HPD toggle (983 APB LINK_ENABLE 0 -> 1)"
    apb_link_enable 0x00
    sleep 1
    apb_link_enable 0x01
    sleep 2
}

# Map DIP switch value to display type
map_dip_to_type() {
    case "$1" in
        0xf7) echo "12.3" ;;
        0x7a) echo "12.3-nq1" ;;
        0x6d) echo "12.3-nq1v1.1" ;;
        0xfb) echo "14.6-fhd" ;;
        0xfd) echo "14.6-2k5" ;;
        0xfe) echo "15.6-2k5" ;;
        0xdf) echo "17.3-3k" ;;
        0x79|0x39) echo "ots-oled-17" ;;
        0x75|0x35) echo "3x-qvue" ;;
        0x5d|0x1d) echo "oled-ib-17" ;;
        0xef) echo "27" ;;
        *)    echo "" ;;
    esac
}

# A display type that shares its DIP position with another: same panel size,
# timing and RH850 profile, told apart only by the type pi-config-txt.sh
# recorded. The DIP switch cannot select it, so it satisfies its base type's
# position instead of being "corrected" back to the base type at every boot.
dip_base_type() {
    case "$1" in
        14.6-fhd-ej) echo "14.6-fhd" ;;   # scan-mode EJ; DIP2 like direct-drive
        *)           echo "$1" ;;
    esac
}

# Display type -> PCF8574 value, the reverse of map_dip_to_type. The first value
# that maps to the type wins, so the CS1.0 positions are used for ots-oled-17,
# 3x-qvue and oled-ib-17: the RH850 picks the CS2.0 sequence from the silicon itself.
type_to_dip() {
    _base=$(dip_base_type "$1")
    for _v in 0xf7 0x7a 0x6d 0xfb 0xfd 0xfe 0xdf 0x79 0x75 0x5d 0xef; do
        if [ "$(map_dip_to_type "$_v")" = "$_base" ]; then
            echo "$_v"
            return 0
        fi
    done
    return 1
}

# FPGA Chip ID (reg 0x10) on the FPGA's page-addressed slave 0x1E, page 0.
FPGA_ADDR=0x1e
FPGA_WAIT_S=10         # how long to wait for the FPGA behind the FPD-Link

# Set FPGA_VARIANT to the 14.6" FHD type the FPGA bitstream identifies; fail
# when the FPGA does not answer or is not a 14.6" FHD Spartan-7 build. Bytes
# 1..3 must read 00 01 46 (1920x1080, spartan7, 14.6"); byte 0 is the variant.
# Not called in a subshell, so its log lines keep their place in the output.
fpga_14_6_variant() {
    FPGA_VARIANT=""
    _t=0
    while [ "$_t" -lt "$FPGA_WAIT_S" ]; do
        _id=$(i2ctransfer -y -f 1 w1@"$FPGA_ADDR" 0x10 r4@"$FPGA_ADDR" 2>/dev/null)
        [ -n "$_id" ] && break
        sleep 1
        _t=$((_t + 1))
    done
    if [ -z "$_id" ]; then
        log "FPGA ID: no answer at $FPGA_ADDR after ${_t}s"
        return 1
    fi
    log "FPGA ID: $_id"
    set -- $_id
    [ "$2 $3 $4" = "0x00 0x01 0x46" ] || return 1
    case "$1" in
        0x00) FPGA_VARIANT="14.6-fhd" ;;
        0x01) FPGA_VARIANT="14.6-fhd-ej" ;;
        *)    return 1 ;;
    esac
}

# Switch a 14.6-fhd* display type to the variant the FPGA reports. Anything
# else, or an FPGA that cannot be read, keeps the type it has.
apply_fpga_variant() {
    _cur=$("$SCRIPT_DIR/pi-config-txt.sh" $(build_config_args) 2>/dev/null)
    [ "$(dip_base_type "$_cur")" = "14.6-fhd" ] || return 0
    fpga_14_6_variant || {
        log "14.6\" variant: FPGA did not identify it, keeping $_cur"
        return 0
    }
    _var=$FPGA_VARIANT
    if [ "$_var" = "$_cur" ]; then
        log "14.6\" variant: FPGA reports $_var, matches"
        return 0
    fi
    echo "[dip-switch] 14.6\" variant: FPGA reports $_var, config was $_cur; switching (no reboot)"
    "$SCRIPT_DIR/pi-config-txt.sh" $(build_config_args) --type="$_var" --no-reboot >/dev/null 2>&1 || {
        echo "[dip-switch] 14.6\" variant: pi-config-txt.sh --type=$_var failed"
        return 1
    }
    # als-dimmer picks its config when it starts; if it already has, restart it
    # onto the new one.
    systemctl try-restart als-dimmer.service 2>/dev/null || true
}

# 983_manager status slave (0x67 on i2c-1): 16-bit register, big-endian.
RH850_BUS=1
RH850_ADDR=0x67
# Seconds the bus is left alone after a soft-DIP cold re-init (see apply_soft_dip).
SOFT_DIP_QUIET_S=6
# oled-ib-17: longest wait for the 983's VP0 to sync after the DP source comes
# back (3-7 s measured, never later) before the re-init is retried, and how
# many re-inits are run before the drivers are loaded anyway.
DP_SYNC_WAIT_S=12
SOFT_DIP_TRIES=3

rh850_read() {
    i2ctransfer -y -f "$RH850_BUS" w2@"$RH850_ADDR" "$1" "$2" r1@"$RH850_ADDR" 2>/dev/null
}

rh850_write() {
    i2ctransfer -y -f "$RH850_BUS" w3@"$RH850_ADDR" "$1" "$2" "$3" 2>/dev/null
}

# All switches off: have the RH850 run the profile of the Pi's display type.
apply_soft_dip() {
    _type=$("$SCRIPT_DIR/pi-config-txt.sh" $(build_config_args) 2>/dev/null)
    _raw=$(type_to_dip "$_type") || {
        log "Soft DIP: display type '${_type:-unknown}' has no 983HH profile, skipping"
        return 0
    }
    _mask=$(printf '0x%02x' $(( _raw ^ 0xff )))

    if ! wait_for_rh850; then
        return 1
    fi
    _hw=$(rh850_read 0x03 0x15)
    if [ -z "$_hw" ]; then
        log "Soft DIP: no SOFT_DIP support on the RH850 (983_manager older than v01.13?), skipping"
        return 1
    fi
    if [ "$_hw" != "0x00" ]; then
        log "Soft DIP: RH850 latched DIP on-mask $_hw at boot, not soft mode, skipping"
        return 0
    fi
    if [ "$(rh850_read 0x03 0x17)" = "0x01" ] && [ "$(rh850_read 0x03 0x16)" = "$_mask" ]; then
        log "Soft DIP: $_type ($_mask) already running"
        return 0
    fi

    # The 17" OLED-IB's CS2.0 sequence must not run while the DP source is
    # sending: with the Pi's video live during the re-init, the 983 synced VP0
    # but no stream reached the 988 (3 of 3 cold boots, black). At a
    # DIP-switch power-up the profile runs before the Pi's HDMI output exists,
    # and it works every time. So the re-init runs with the source out of the
    # way, and the source comes back once the profile is up (2026-10-20/21):
    #
    #  - Pi HDMI output off -> re-init -> HDMI on: VP0 synced 7 s after HDMI on
    #    in 10 of 10 back-to-back runs, HPD never moved. The HDMI-to-DP
    #    converter starts fresh against a configured 983, as at power-up.
    #  - The first fix held HPD low across the re-init instead: 13 of 20 cold
    #    boots. In the rest the converter trained the PHY but sent no stream,
    #    and every later HPD edge (toggle, 983 kick, re-init) only stopped it
    #    training at all until a power cycle -- so HPD is left alone now, and
    #    the HPD hold is kept only for an image without toggle-pi-hdmi.sh.
    #
    # Nothing else talks over the link meanwhile: the drivers are out and
    # als-dimmer is parked until VP0 has synced.
    #
    # Even so, about 1 cold boot in 3 ends the re-init with the 983 not taking
    # the source (LINK_ENABLE 0, PHY untrained, VP0 never syncs; 6 of 20 with
    # the HDMI-off sequence, 7 of 20 with the HPD hold). The same sequence run
    # again on such a boot brought the video up 4 s after HDMI on, so the
    # re-init is retried, up to SOFT_DIP_TRIES times. A bare 983 kick (digital
    # reset + HPD pulse) does not do it: 6 kicks left the PHY dead.
    # With the retry, 42 of 45 cold boots came up right (one needed a second
    # re-init). Still open: 2 came up dim -- VP0 synced, then lost it after the
    # drivers loaded (VP0 0x02, 6-8 cd/m2 instead of 13) -- and 1 black.
    _quiet=0
    _hdmi=""
    if [ "$_type" = "oled-ib-17" ]; then
        _quiet=1
        for _c in "$SCRIPT_DIR/../../bin/toggle-pi-hdmi.sh" "$SCRIPT_DIR/toggle-pi-hdmi.sh" \
                  /usr/bin/toggle-pi-hdmi.sh; do
            [ -x "$_c" ] && { _hdmi=$_c; break; }
        done
    fi

    log "Soft DIP: display type $_type -> DIP on-mask $_mask, cold re-init"
    stop_drivers
    _als_was=0
    if [ "$_quiet" -eq 1 ]; then
        # als-dimmer talks to the display MCU at 0x66 through the same link;
        # it is not ordered after this unit, so park it until the video is up.
        if systemctl is-active --quiet als-dimmer 2>/dev/null; then
            _als_was=1
        fi
        systemctl stop als-dimmer 2>/dev/null && log "  als-dimmer stopped for the re-init"
    fi
    _try=1
    while :; do
        if [ "$_quiet" -eq 1 ]; then
            if [ -n "$_hdmi" ]; then
                log "  Pi HDMI output off across the re-init ($_hdmi)"
                "$_hdmi" off >/dev/null 2>&1
                sleep 1
            else
                log "  HPD low across the re-init (983 APB LINK_ENABLE 0; toggle-pi-hdmi.sh not found)"
                apb_link_enable 0x00
            fi
        fi
        rh850_write 0x03 0x16 "$_mask"
        rh850_write 0x03 0x00 0x11
        # The RH850 drops its slave while it runs the profile; poll DBG_STATUS,
        # but only after the profile has had time to finish. The RH850 writes the
        # serializer with a bit-banged master that has no multi-master
        # arbitration, on the bus we poll it on. A poll that lands on one of its
        # writes makes that write fail. A CS2.0 profile that loads its EDID over
        # APB (OTS-OLED, 3x QVue) runs for about 4 s, and a poll at 1 s ended its
        # bring-up with LAST_ERROR 0x04 on every boot (983_manager v01.14 also
        # retries such a write).
        sleep "$SOFT_DIP_QUIET_S"
        _t=0
        _st=""
        while [ "$_t" -lt 30 ]; do
            _st=$(rh850_read 0x03 0x01)
            [ "$_st" = "0x02" ] || [ "$_st" = "0xff" ] && break
            sleep 1
            _t=$((_t + 1))
        done
        _err=$(rh850_read 0x03 0x04)
        _prof=$(rh850_read 0x03 0x07)
        if [ "$_st" = "0x02" ]; then
            log "Soft DIP: profile $_prof up after ~$((_t + SOFT_DIP_QUIET_S))s"
        else
            echo "[dip-switch] Soft DIP: re-init failed (DBG_STATUS=${_st:-none} LAST_ERROR=${_err:-none} profile=${_prof:-none})"
        fi
        [ "$_quiet" -eq 1 ] || break
        # The source's cue to train; a toggle after the driver reload would only
        # take the link down again under it.
        if [ -n "$_hdmi" ]; then
            log "  983 LINK_ENABLE after the re-init: $(apb_link_enabled); Pi HDMI output on, the source trains now"
            "$_hdmi" on >/dev/null 2>&1
        else
            log "  HPD high (983 APB LINK_ENABLE 1), the source trains now"
            apb_link_enable 0x01
        fi
        await_dp_sync && break
        if [ "$_try" -ge "$SOFT_DIP_TRIES" ]; then
            log "  WARNING: no DP video after $_try re-inits; loading the drivers anyway"
            break
        fi
        _try=$((_try + 1))
        log "  re-init again (try $_try of $SOFT_DIP_TRIES)"
    done
    start_drivers
    [ "$_quiet" -eq 1 ] || hpd_toggle
    if [ "$_quiet" -eq 1 ]; then
        # Restart it whether or not it was running yet: it may have failed its
        # first start while the link was down, and systemd's retry would land
        # in the window this function keeps quiet.
        systemctl start --no-block als-dimmer 2>/dev/null && log "  als-dimmer started (was ${_als_was})"
    fi
    [ "$_st" = "0x02" ]
}

# Read the DIP switch, waiting for the bus if it is not up yet.
#
# This unit starts After=local-fs.target, which can be before i2c-3 is
# registered. A single probe then fails and the type change is silently not
# applied - seen twice, once leaving a 12.3" panel running a 14.6" timing until
# someone noticed. So wait for the device node and retry the read, and say how
# long it took, rather than treating "not there yet" as "not fitted".
#
# The probe is a direct read rather than `i2cdetect | grep "20"`: it is the same
# read the script needs anyway, and a grep for "20" also matches the address
# column and any other device whose address ends in 0x20 on that row.
read_dip() {
    _t=0
    while [ "$_t" -lt "$DIP_WAIT_S" ]; do
        if [ -e "/dev/i2c-$I2C_BUS" ]; then
            _v=$(i2cget -y "$I2C_BUS" "$DIP_ADDR" 2>/dev/null)
            if [ -n "$_v" ]; then
                # log() writes to stdout and this function's stdout is the
                # value, so send the note to stderr or it lands in dip_value.
                [ "$_t" -gt 0 ] && log "DIP switch answered after ${_t}s" >&2
                echo "$_v"
                return 0
            fi
        fi
        sleep 1
        _t=$((_t + 1))
    done
    log "No DIP switch (PCF8574) found at $DIP_ADDR on i2c-$I2C_BUS after ${_t}s, skipping" >&2
    return 1
}

dip_value=$(read_dip) || exit 0

log "DIP switch value: $dip_value"

# Map to display type
if [ "$dip_value" = "0xff" ]; then
    log "Every DIP switch off: soft DIP mode"
    apply_soft_dip
    apply_fpga_variant
    exit 0
fi

expected_type=$(map_dip_to_type "$dip_value")
if [ -z "$expected_type" ]; then
    log "Unknown DIP switch value $dip_value, skipping"
    exit 0
fi

log "DIP switch selects display type: $expected_type"

# Read current config
config_args=$(build_config_args)
current_type=$("$SCRIPT_DIR/pi-config-txt.sh" $config_args 2>/dev/null)
if [ -z "$current_type" ]; then
    log "Failed to read current config, skipping"
    exit 0
fi

log "Current config type: $current_type"

# Compare. A variant type (dip_base_type) matches its base type's position.
if [ "$current_type" = "$expected_type" ] || [ "$(dip_base_type "$current_type")" = "$expected_type" ]; then
    log "Config matches DIP switch, no action needed"
    # After a DIP-switch-triggered reboot, power-cycle the display to restore sync
    if [ -f "$REBOOT_FLAG" ]; then
        prev_type=$(cat "$REBOOT_FLAG" 2>/dev/null)
        if [ "$prev_type" != "edid-hdmi" ]; then
            log "Post-reconfig reboot detected, power-cycling display..."
            DISPTOOL="/home/pi/micropanel/bin/disptool"
            if [ -x "$DISPTOOL" ]; then
                # Get the Pi off the bus before the RH850 reset: it re-runs its
                # bit-bang init on the same wires, and a second master there has
                # killed that init. This also means the touch routing the driver
                # installed is about to be overwritten, hence the reload after.
                log "Stopping drivers before the RH850 reset"
                stop_drivers
                "$DISPTOOL" --device=983 --command=disppower --value=off
                sleep 1
                "$DISPTOOL" --device=983 --command=disppower --value=on
                sleep 1
                gpioset 0 22=0
                sleep 2
                gpioset 0 22=1
                log "RH850 reset released, waiting for it to finish its profile"
                wait_for_rh850
                log "Restarting drivers"
                start_drivers
                hpd_toggle
                log "Display power-cycle complete"
            else
                log "Warning: disptool not found at $DISPTOOL, skipping power-cycle"
            fi
        else
            log "edid-hdmi config, skipping display power-cycle"
        fi
        rm -f "$REBOOT_FLAG"
        log "Cleared reboot flag"
    fi
    apply_fpga_variant
    exit 0
fi

# Mismatch detected
log "Mismatch: config=$current_type, DIP=$expected_type"

# Reboot loop guard
if [ -f "$REBOOT_FLAG" ]; then
    echo "[dip-switch] WARNING: Mismatch persists after reboot (config=$current_type, DIP=$expected_type). Skipping to prevent reboot loop."
    exit 1
fi

# Create reboot flag directory and file
mkdir -p "$(dirname "$REBOOT_FLAG")"
echo "$expected_type" > "$REBOOT_FLAG"

echo "[dip-switch] Reconfiguring HDMI timing to $expected_type (was $current_type)"

# Call pi-config-txt.sh with the correct type (this triggers a reboot)
"$SCRIPT_DIR/pi-config-txt.sh" $config_args --type="$expected_type"
