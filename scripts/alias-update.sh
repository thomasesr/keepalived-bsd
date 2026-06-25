#!/bin/sh
# Update an OPNsense firewall alias with a keepalived VIP address.
# Usage: alias-update.sh <add|del> <alias-name> <ip>
set -e

ACTION=$1
ALIAS=$2
IP=$3

/usr/local/bin/php \
    /usr/local/libexec/keepalived-bsd/alias-update.php \
    "$ACTION" "$ALIAS" "$IP"

/usr/local/sbin/configctl filter reload
