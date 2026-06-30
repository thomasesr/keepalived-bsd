#!/bin/sh
# Toggle dnsmasq DHCP for one interface using a no-dhcp-interface drop-in.
#
# OPNsense 26.1 generates dnsmasq.conf with "conf-dir=/usr/local/etc/dnsmasq.conf.d,*.conf",
# so drop-ins MUST live in /usr/local/etc/dnsmasq.conf.d/ (not /var/etc/dnsmasq.d/, which
# dnsmasq never reads). SIGHUP does NOT re-read conf-dir files, so a real reload is required;
# OPNsense applies dnsmasq changes via a stop/start that preserves existing leases.
set -e

ACTION=$1   # enable | disable
IFACE=$2

if [ "$ACTION" != "enable" ] && [ "$ACTION" != "disable" ]; then
    echo "usage: $0 <enable|disable> <iface>" >&2
    exit 1
fi
case "$IFACE" in
    ''|*[!a-zA-Z0-9]*) echo "bad iface: $IFACE" >&2; exit 1 ;;
esac

DROPDIR="/usr/local/etc/dnsmasq.conf.d"
DROPFILE="$DROPDIR/keepalived-bsd-nodhcp-$IFACE.conf"

mkdir -p "$DROPDIR"

if [ "$ACTION" = "enable" ]; then
    # Remove the suppression file — dnsmasq serves DHCP on this interface again.
    rm -f "$DROPFILE"
else
    # Suppress — dnsmasq ignores DHCP requests on this interface.
    printf 'no-dhcp-interface=%s\n' "$IFACE" > "$DROPFILE"
fi

# Apply the change. configctl dnsmasq restart regenerates the config and
# restarts the daemon; modern dnsmasq re-reads its leases file on start, so
# active leases are preserved. SIGHUP would NOT pick up the conf-dir change.
/usr/local/sbin/configctl dnsmasq restart
