#!/bin/sh
# dhcp-net-settings-netctl.sh - the IP Settings menu through br-wrapper's
# net-ctl.sh (the Network app's helper), on Pi OS with NetworkManager.
#
# Called by dhcp-net-settings.sh instead of dhcp-net-settings-pios.sh when
# net-ctl.sh is installed and says NetworkManager is available; it gets the
# same exported variables (INTERFACE, MODE, IP, GATEWAY, NETMASK, DNS1..3,
# VERBOSE, DRY_RUN) and NET_CTL (the helper's path). It translates:
#
#   menu                    net-ctl.sh
#   (no --mode): read       status          mode=dhcp|static|dhcp-server, ip=,
#                                           gateway=, netmask=, dns1..3=
#   --mode=dhcp             wired-set --mode=client
#   --mode=static           wired-set --mode=static --ip= --prefix= --gateway=
#                           (the port's DNS servers kept: the menu sends none)
#   --mode=dhcp-server      wired-set --mode=server --ip= --prefix=
#   RESULT:OK|ERROR         exit 0 | anything else
#
# So the menu and the app agree on every port: a server set here is
# NetworkManager's shared mode, as the app's - it survives a reboot,
# announces no gateway and no DNS (the image's drop-in), and is checked by
# the DHCP guard whenever the port comes up. The gateway the menu sends for
# a server is not used; its netmask is. A port in the menu's old server mode
# (the system dnsmasq) is taken over by wired-set, as the app does.
#
# POSIX sh, busybox-compatible (no grep -o/-P, no bash).

log_verbose() { [ "${VERBOSE:-0}" -eq 1 ] && echo "[INFO] $1"; return 0; }
error_exit() { echo "[ERROR] $1"; echo "RESULT:ERROR"; exit 1; }

[ -n "$INTERFACE" ] || error_exit "INTERFACE environment variable not set"
[ -x "${NET_CTL:-}" ] || error_exit "net-ctl.sh not found"

# Undo net-ctl.sh's percent-encoding of a value (addresses and names here)
pct_decode() {
    printf '%s\n' "$1" | awk '
        function hex(h,   i, v, c) { v = 0
            for (i = 1; i <= 2; i++) { c = index("0123456789abcdef", tolower(substr(h, i, 1))) - 1; v = v * 16 + c }
            return v }
        { s = $0; out = ""
          while (match(s, /%[0-9A-Fa-f][0-9A-Fa-f]/)) {
              out = out substr(s, 1, RSTART - 1) sprintf("%c", hex(substr(s, RSTART + 1, 2))); s = substr(s, RSTART + 3) }
          print out s }'
}

# one key=value of a RESULT line
field() { printf '%s\n' "$1" | tr ' ' '\n' | sed -n "s/^$2=//p" | head -n 1; }

prefix_to_mask() {
    p=$1 mask='' i=1
    while [ $i -le 4 ]; do
        if [ "$p" -ge 8 ]; then o=255; p=$((p - 8))
        elif [ "$p" -gt 0 ]; then o=$((256 - (1 << (8 - p)))); p=0
        else o=0; fi
        mask="$mask${mask:+.}$o"
        i=$((i + 1))
    done
    echo "$mask"
}

# a dotted netmask's prefix length; empty if it is not a netmask
mask_to_prefix() {
    bits=0 seen_zero=0
    for o in $(echo "$1" | tr '.' ' '); do
        case $o in
            255) [ $seen_zero = 0 ] || { echo; return; }; bits=$((bits + 8)) ;;
            254|252|248|240|224|192|128)
                [ $seen_zero = 0 ] || { echo; return; }
                case $o in 254) bits=$((bits + 7)) ;; 252) bits=$((bits + 6)) ;; 248) bits=$((bits + 5)) ;;
                           240) bits=$((bits + 4)) ;; 224) bits=$((bits + 3)) ;; 192) bits=$((bits + 2)) ;;
                           128) bits=$((bits + 1)) ;; esac
                seen_zero=1 ;;
            0) seen_zero=1 ;;
            *) echo; return ;;
        esac
    done
    echo "$bits"
}

# the port's line of net-ctl.sh status (no internet ping: the menu waits)
port_status() {
    NET_CTL_INET=skip "$NET_CTL" status 2>/dev/null | while IFS= read -r l; do
        case $l in "RESULT kind=iface name=$INTERFACE "*) printf '%s\n' "$l"; break ;; esac
    done
}

# Clean "192.168.001.050" (the menu's IP selector) to "192.168.1.50" - as
# text: busybox awk reads "050" as octal
clean_ip() { echo "$1" | sed 's/^0*\([0-9]\)/\1/; s/\.0*\([0-9]\)/.\1/g'; }

# net-ctl.sh's exit and last RESULT line -> the menu's RESULT
finish() { # <exit code> <output>
    log_verbose "net-ctl.sh: $(printf '%s\n' "$2" | grep '^RESULT' | tail -n 1)"
    if [ "$1" -eq 0 ]; then echo "RESULT:OK"; exit 0; fi
    last=$(printf '%s\n' "$2" | grep '^RESULT' | tail -n 1)
    detail=$(pct_decode "$(field "$last" detail)")
    echo "[ERROR] ${detail:-$(field "$last" reason)} (net-ctl.sh exit $1)"
    echo "RESULT:ERROR"
    exit 1
}

if [ -z "$MODE" ]; then
    line=$(port_status)
    [ -n "$line" ] || error_exit "net-ctl.sh does not list $INTERFACE as a wired port"
    case $(field "$line" mode) in
        static) mode=static ;;
        server|legacy-server) mode=dhcp-server ;;
        *) mode=dhcp ;;
    esac
    ip=$(field "$line" ip) prefix=$(field "$line" prefix) gw=$(field "$line" gateway)
    dns=$(field "$line" dns)
    # no address now (no cable, or the DHCP guard took a serving port down):
    # the configured ones, so the menu edits what the port will use
    if [ -z "$ip" ]; then ip=$(field "$line" cfgip); prefix=$(field "$line" cfgprefix); gw=$(field "$line" cfggateway); fi
    [ -n "$dns" ] || dns=$(field "$line" cfgdns)
    # a server announces no gateway; the menu has a gateway field: its own address
    [ "$mode" = dhcp-server ] && gw=$ip
    echo "mode=$mode"
    echo "ip=${ip:-0.0.0.0}"
    echo "gateway=${gw:-0.0.0.0}"
    echo "netmask=$(prefix_to_mask "${prefix:-24}")"
    i=1
    for d in $(echo "$dns" | tr ',' ' '); do
        [ $i -le 3 ] || break
        echo "dns$i=$d"
        i=$((i + 1))
    done
    while [ $i -le 3 ]; do echo "dns$i="; i=$((i + 1)); done
    [ "$(field "$line" guard)" = stopped ] && \
        echo "[INFO] DHCP serving stopped: another DHCP server ($(field "$line" guardserver)) answered on $INTERFACE"
    echo "RESULT:OK"
    exit 0
fi

set -- wired-set --iface="$INTERFACE"
case $MODE in
    dhcp) set -- "$@" --mode=client ;;
    static|dhcp-server)
        ip=$(clean_ip "$IP")
        prefix=$(mask_to_prefix "$(clean_ip "$NETMASK")")
        [ -n "$prefix" ] || error_exit "Invalid netmask: $NETMASK"
        if [ "$MODE" = static ]; then
            gw=$(clean_ip "$GATEWAY")
            set -- "$@" --mode=static --ip="$ip" --prefix="$prefix"
            # 0.0.0.0, or the address itself: no gateway
            [ "$gw" = 0.0.0.0 ] || [ "$gw" = "$ip" ] || set -- "$@" --gateway="$gw"
            dns=''
            for d in "$DNS1" "$DNS2" "$DNS3"; do [ -n "$d" ] && dns="$dns${dns:+,}$(clean_ip "$d")"; done
            # the menu sends no DNS: keep the port's
            [ -n "$dns" ] || dns=$(field "$(port_status)" cfgdns)
            [ -n "$dns" ] && set -- "$@" --dns="$dns"
        else
            # NetworkManager's shared mode: no gateway announced, whatever the menu says
            set -- "$@" --mode=server --ip="$ip" --prefix="$prefix"
        fi ;;
    *) error_exit "Unsupported mode: $MODE" ;;
esac
[ "${DRY_RUN:-0}" -eq 1 ] && set -- "$@" --dry-run
log_verbose "$NET_CTL $*"
out=$("$NET_CTL" "$@" 2>&1)
finish $? "$out"
