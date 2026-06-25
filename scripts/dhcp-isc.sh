#!/bin/sh
# Toggle ISC dhcpd DHCP for one interface.
# Writes enable/disable flag into OPNsense config.xml, then restarts dhcpd.
# dhcpd does not support per-interface SIGHUP; a fast restart is required.
set -e

ACTION=$1   # enable | disable
IFACE=$2

/usr/local/bin/php \
    /usr/local/libexec/keepalived-bsd/dhcp-opnsense-toggle.php \
    "$ACTION" dhcpd "$IFACE"

/usr/local/sbin/configctl dhcpd restart
