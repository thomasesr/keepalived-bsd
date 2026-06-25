#!/bin/sh
# Toggle Kea DHCPv4 DHCP for one interface.
# Writes enable/disable flag into OPNsense config.xml (os-kea path), then
# restarts Kea. Kea does not support hot-reload of subnet config via SIGHUP.
set -e

ACTION=$1   # enable | disable
IFACE=$2

/usr/local/bin/php \
    /usr/local/libexec/keepalived-bsd/dhcp-opnsense-toggle.php \
    "$ACTION" kea "$IFACE"

/usr/local/sbin/configctl kea restart
