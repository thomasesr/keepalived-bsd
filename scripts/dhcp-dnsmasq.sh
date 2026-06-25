#!/bin/sh
# Toggle dnsmasq DHCP for one interface using no-dhcp-interface drop-in.
# OPNsense dnsmasq includes /var/etc/dnsmasq.d/ — files there are hot-reloaded
# on SIGHUP without dropping active leases.

ACTION=$1   # enable | disable
IFACE=$2

DROPDIR="/var/etc/dnsmasq.d"
DROPFILE="$DROPDIR/keepalived-bsd-nodhcp-$IFACE.conf"

mkdir -p "$DROPDIR"

if [ "$ACTION" = "enable" ]; then
    # Remove the suppression file — dnsmasq will serve DHCP on this interface again
    rm -f "$DROPFILE"
else
    # Write suppression — dnsmasq ignores DHCP requests on this interface
    printf 'no-dhcp-interface=%s\n' "$IFACE" > "$DROPFILE"
fi

# SIGHUP reloads config and leases files without restarting the daemon
PIDFILE=/var/run/dnsmasq.pid
if [ -f "$PIDFILE" ]; then
    kill -HUP "$(cat "$PIDFILE")" 2>/dev/null || true
fi
