#!/bin/sh
# Toggle Kea DHCPv4 for failover.
#
# Kea on OPNsense is an MVC model ($config['OPNsense']['Kea']['dhcp4']) with a
# single GLOBAL enable flag and an interface bind list — there is no per-interface
# enable. So this toggles the whole Kea service: MASTER serves DHCP, BACKUP does
# not. The <iface> argument is validated but Kea is all-or-nothing.
set -e

ACTION=$1   # enable | disable
IFACE=$2

# Flip the global Kea enable flag in config.xml.
/usr/local/bin/php \
    /usr/local/libexec/keepalived-bsd/dhcp-opnsense-toggle.php \
    "$ACTION" kea "$IFACE"

# Regenerate /usr/local/etc/kea/*.conf from the model, then start or stop the
# service to match. (A bare "kea restart" would not stop a now-disabled service.)
/usr/local/sbin/configctl template reload OPNsense/Kea
if [ "$ACTION" = "enable" ]; then
    /usr/local/sbin/configctl kea start
else
    /usr/local/sbin/configctl kea stop
fi
