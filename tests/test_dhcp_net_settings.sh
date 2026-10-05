#!/bin/sh
# shellcheck disable=SC2016 # the checks are sh -c bodies, single-quoted on purpose
# test_dhcp_net_settings.sh - dhcp-net-settings.sh with and without br-wrapper's
# net-ctl.sh, against a fake net-ctl.sh. Nothing on the host is touched: the
# scripts run from a copy, with the OS script replaced by a stub, and with
# fake id (root) and ip (the interface exists) first in PATH.
#   tests/test_dhcp_net_settings.sh       (also run by ctest with -DBUILD_TESTS=ON)
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/usr/bin" "$work/bin" "$work/fakebin"

cp "$here/../scripts/dhcp-net-settings.sh" "$here/../scripts/dhcp-net-settings-netctl.sh" "$work/usr/bin/"
# the old Pi OS path, stubbed: it says it ran
cat > "$work/usr/bin/dhcp-net-settings-pios.sh" <<'EOF'
#!/bin/sh
echo "pios-path mode=$MODE"
echo "RESULT:OK"
EOF
printf '#!/bin/sh\necho 0\n' > "$work/fakebin/id"
printf '#!/bin/sh\nexit 0\n' > "$work/fakebin/ip"

# A fake net-ctl.sh: FAKE_AVAILABLE=0 (no NetworkManager), FAKE_PORT=client|
# static|server|legacy|stopped|none, FAKE_EXIT/FAKE_DETAIL for a failing change
cat > "$work/bin/net-ctl.sh" <<'EOF'
#!/bin/sh
echo "$*" >> "$FAKE_CALLS"
case $1 in
    available)
        [ "${FAKE_AVAILABLE:-1}" = 1 ] && { echo "RESULT kind=available ok=1"; exit 0; }
        echo "RESULT kind=available ok=0 reason=nmcli%20is%20not%20installed"; exit 4 ;;
    status)
        [ "$NET_CTL_INET" = skip ] || echo "inet-not-skipped" >> "$FAKE_CALLS"
        echo "RESULT kind=iface name=wlan0 type=wifi mode=off ip= prefix=24"
        c="profileuuid=u1 cfgprofile=eth0 saved=1 binding=name"
        case ${FAKE_PORT:-client} in
            client) echo "RESULT kind=iface name=eth0 type=ethernet carrier=1 state=connected mode=client ip=192.168.1.170 prefix=24 gateway=192.168.1.1 dns=192.168.1.1,9.9.9.9 $c cfgip= cfgprefix= cfggateway= cfgdns= guard= guardserver=" ;;
            static) echo "RESULT kind=iface name=eth0 type=ethernet carrier=1 state=connected mode=static ip=10.1.2.3 prefix=16 gateway=10.1.0.1 dns=10.1.0.53 $c cfgip=10.1.2.3 cfgprefix=16 cfggateway=10.1.0.1 cfgdns=10.1.0.53 guard= guardserver=" ;;
            server) echo "RESULT kind=iface name=eth0 type=ethernet carrier=1 state=connected mode=server ip=192.168.50.1 prefix=24 gateway= dns= $c cfgip=192.168.50.1 cfgprefix=24 cfggateway= cfgdns= guard= guardserver=" ;;
            legacy) echo "RESULT kind=iface name=eth0 type=ethernet carrier=1 state=connected mode=legacy-server ip=192.168.60.1 prefix=24 gateway= dns= $c cfgip=192.168.60.1 cfgprefix=24 cfggateway= cfgdns= guard= guardserver=" ;;
            stopped) echo "RESULT kind=iface name=eth0 type=ethernet carrier=1 state=disconnected mode=server ip= prefix= gateway= dns= $c cfgip=192.168.50.1 cfgprefix=24 cfggateway= cfgdns= guard=stopped guardserver=192.168.1.1" ;;
            none) ;;
        esac
        echo "RESULT kind=summary internet=yes" ;;
    wired-set)
        if [ -n "${FAKE_EXIT:-}" ]; then
            echo "RESULT kind=wired iface=eth0 ok=0 restored=1 detail=${FAKE_DETAIL:-} reason=activation-failed"; exit "$FAKE_EXIT"
        fi
        echo "RESULT kind=wired iface=eth0 ok=1" ;;
esac
exit 0
EOF
chmod +x "$work"/usr/bin/* "$work"/bin/* "$work"/fakebin/*
export PATH="$work/fakebin:$PATH" FAKE_CALLS="$work/calls"

failures=0
check() { label=$1; shift; if "$@"; then echo "  ok  $label"; else echo "FAIL: $label"; failures=$((failures + 1)); fi; }
has() { printf '%s\n' "$out" | grep -qxF -- "$1"; }
called() { grep -qxF -- "$1" "$work/calls"; }
run() { : > "$work/calls"; out=$(sh "$work/usr/bin/dhcp-net-settings.sh" --os=pios --interface=eth0 --backuppath="$work/bkup" "$@" 2>&1); rc=$?; }

echo "== net-ctl.sh present, NetworkManager running: through net-ctl.sh"
export NET_CTL="$work/bin/net-ctl.sh"
FAKE_PORT=client run
check "read, DHCP client" sh -c 'printf "%s\n" "$1" | grep -qx "mode=dhcp" && printf "%s\n" "$1" | grep -qx "ip=192.168.1.170" && printf "%s\n" "$1" | grep -qx "gateway=192.168.1.1" && printf "%s\n" "$1" | grep -qx "netmask=255.255.255.0" && printf "%s\n" "$1" | grep -qx "RESULT:OK"' _ "$out"
check "  DNS servers as dns1, dns2, an empty dns3" sh -c 'printf "%s\n" "$1" | grep -qx "dns1=192.168.1.1" && printf "%s\n" "$1" | grep -qx "dns2=9.9.9.9" && printf "%s\n" "$1" | grep -qx "dns3="' _ "$out"
check "  status without the internet ping (the menu waits)" sh -c '! grep -q inet-not-skipped "$1"' _ "$work/calls"
check "  the old path not used" sh -c '! printf "%s\n" "$1" | grep -q pios-path' _ "$out"
FAKE_PORT=static run
check "read, fixed address /16" sh -c 'printf "%s\n" "$1" | grep -qx "mode=static" && printf "%s\n" "$1" | grep -qx "netmask=255.255.0.0" && printf "%s\n" "$1" | grep -qx "gateway=10.1.0.1"' _ "$out"
FAKE_PORT=server run
check "read, the app's server: dhcp-server, gateway = its own address" sh -c 'printf "%s\n" "$1" | grep -qx "mode=dhcp-server" && printf "%s\n" "$1" | grep -qx "ip=192.168.50.1" && printf "%s\n" "$1" | grep -qx "gateway=192.168.50.1"' _ "$out"
FAKE_PORT=legacy run
check "read, the menu's old server: dhcp-server" has "mode=dhcp-server"
FAKE_PORT=stopped run
check "read, a server the DHCP guard took down: its configured address, and why" sh -c 'printf "%s\n" "$1" | grep -qx "mode=dhcp-server" && printf "%s\n" "$1" | grep -qx "ip=192.168.50.1" && printf "%s\n" "$1" | grep -q "DHCP serving stopped: another DHCP server (192.168.1.1)"' _ "$out"
FAKE_PORT=none run
check "read, a port net-ctl.sh does not list: RESULT:ERROR" sh -c '[ "$2" != 0 ] && printf "%s\n" "$1" | grep -qx "RESULT:ERROR"' _ "$out" "$rc"

run --mode=dhcp
check "set DHCP: wired-set client" sh -c 'grep -qx "wired-set --iface=eth0 --mode=client" "$1" && printf "%s\n" "$2" | grep -qx "RESULT:OK"' _ "$work/calls" "$out"
FAKE_PORT=static run --mode=static --ip=192.168.001.050 --gateway=192.168.001.001 --netmask=255.255.255.000
check "set static: leading zeros gone, prefix from the netmask, the port's DNS kept" called "wired-set --iface=eth0 --mode=static --ip=192.168.1.50 --prefix=24 --gateway=192.168.1.1 --dns=10.1.0.53"
run --mode=static --ip=192.168.1.50 --gateway=0.0.0.0 --netmask=255.255.255.0 --dns1=1.1.1.1 --dns2=8.8.8.8
check "set static: no gateway for 0.0.0.0; DNS given by the caller wins" called "wired-set --iface=eth0 --mode=static --ip=192.168.1.50 --prefix=24 --dns=1.1.1.1,8.8.8.8"
run --mode=dhcp-server --ip=192.168.50.1 --gateway=192.168.50.1 --netmask=255.255.255.0
check "set server: wired-set server, no gateway passed" called "wired-set --iface=eth0 --mode=server --ip=192.168.50.1 --prefix=24"
run --mode=dhcp-server --ip=192.168.50.1 --gateway=192.168.50.1 --netmask=255.0.255.0
check "a netmask that is none: refused before net-ctl.sh" sh -c '[ "$2" != 0 ] && printf "%s\n" "$1" | grep -qx "RESULT:ERROR" && ! grep -q wired-set "$3"' _ "$out" "$rc" "$work/calls"
FAKE_EXIT=3 FAKE_DETAIL=eth0%20came%20up%20without%20an%20address run --mode=dhcp
check "a failed change: RESULT:ERROR with net-ctl.sh's words" sh -c 'printf "%s\n" "$1" | grep -qx "RESULT:ERROR" && printf "%s\n" "$1" | grep -q "eth0 came up without an address (net-ctl.sh exit 3)"' _ "$out"
run --mode=dhcp --dry-run
check "--dry-run reaches net-ctl.sh" called "wired-set --iface=eth0 --mode=client --dry-run"

echo "== found beside the Qt apps (MICROPANEL_HOME/bin), with no NET_CTL"
unset NET_CTL
run --mode=dhcp
check "net-ctl.sh two levels up from the menu's scripts" sh -c 'grep -qx "wired-set --iface=eth0 --mode=client" "$1"' _ "$work/calls"

echo "== the old path, untouched"
FAKE_AVAILABLE=0 run --mode=dhcp
check "NetworkManager not available: the OS script, as before" sh -c 'printf "%s\n" "$1" | grep -qx "pios-path mode=dhcp" && ! grep -q wired-set "$2"' _ "$out" "$work/calls"
mv "$work/bin/net-ctl.sh" "$work/bin/net-ctl.sh.off"
run --mode=dhcp
check "no net-ctl.sh: the OS script" has "pios-path mode=dhcp"
mv "$work/bin/net-ctl.sh.off" "$work/bin/net-ctl.sh"
: > "$work/calls"
out=$(sh "$work/usr/bin/dhcp-net-settings.sh" --os=debian --interface=eth0 --backuppath="$work/bkup" 2>&1)
check "another --os: never net-ctl.sh" sh -c '! grep -q . "$1"' _ "$work/calls"

if [ "$failures" -gt 0 ]; then echo "dhcp-net-settings: $failures failure(s)"; exit 1; fi
echo "dhcp-net-settings: PASS"
