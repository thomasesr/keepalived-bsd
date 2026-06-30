#!/bin/sh
# Toggle ISC dhcpd DHCP for one interface (LEGACY).
#
# ISC dhcpd was moved out of OPNsense core in 26.1 into the os-isc-dhcp plugin and
# is NOT present on fresh 26.1 installs. This backend only works on systems upgraded
# from <= 25.7 that still have the plugin. If ISC is absent we log and exit 0 so a
# missing-backend does NOT abort the failover transition (use kea or dnsmasq on 26.1).
set -e

ACTION=$1   # enable | disable
IFACE=$2

# Bail out cleanly if the ISC plugin is not installed. configd returns exit 0
# even for unknown actions, so probe the registered action list instead of
# trying to run a dhcpd action.
if ! /usr/local/sbin/configctl configd actions 2>/dev/null | grep -q '^dhcpd restart'; then
    echo "keepalived-bsd: ISC dhcpd not available on this system (os-isc-dhcp missing); skipping $ACTION on $IFACE" >&2
    exit 0
fi

/usr/local/bin/php \
    /usr/local/libexec/keepalived-bsd/dhcp-opnsense-toggle.php \
    "$ACTION" dhcpd "$IFACE"

/usr/local/sbin/configctl dhcpd restart
