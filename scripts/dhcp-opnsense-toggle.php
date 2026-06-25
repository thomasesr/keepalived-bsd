#!/usr/local/bin/php
<?php
/**
 * Toggle the per-interface DHCP enable flag in OPNsense config.xml.
 * Usage: dhcp-opnsense-toggle.php <enable|disable> <dhcpd|kea> <iface>
 *
 * Shared by dhcp-isc.sh and dhcp-kea.sh. Each backend stores its
 * per-interface enable flag at config.xml://<backend>/<iface>/enable.
 */

if ($argc !== 4) {
    fwrite(STDERR, "Usage: dhcp-opnsense-toggle.php <enable|disable> <backend> <iface>\n");
    exit(1);
}

[$_, $action, $backend, $iface] = $argv;

if (!in_array($action,  ['enable', 'disable'], true)) { fwrite(STDERR, "bad action\n");  exit(1); }
if (!in_array($backend, ['dhcpd',  'kea'],     true)) { fwrite(STDERR, "bad backend\n"); exit(1); }
if (!preg_match('/^[a-z][a-z0-9]{1,14}$/', $iface))  { fwrite(STDERR, "bad iface\n");   exit(1); }

require_once('config.inc');

$path = "$backend/$iface/enable";

if ($action === 'enable') {
    config_set_path($path, true);
} else {
    config_del_path($path);
}

write_config("keepalived-bsd: $action DHCP ($backend) on $iface");
exit(0);
