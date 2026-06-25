#!/usr/local/bin/php
<?php
/**
 * Toggle the per-interface DHCP enable flag in OPNsense config.xml.
 * Usage: dhcp-opnsense-toggle.php <enable|disable> <dhcpd|kea> <iface>
 *
 * Uses the OPNsense global $config array (config.inc) — not pfSense-only
 * functions like config_set_path() which do not exist in OPNsense.
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

global $config;

if ($action === 'enable') {
    if (!isset($config[$backend][$iface])) {
        $config[$backend][$iface] = [];
    }
    $config[$backend][$iface]['enable'] = 1;
} else {
    if (isset($config[$backend][$iface]['enable'])) {
        unset($config[$backend][$iface]['enable']);
    }
}

write_config("keepalived-bsd: $action DHCP ($backend) on $iface");
exit(0);
