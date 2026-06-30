#!/usr/local/bin/php
<?php
/**
 * Toggle the OPNsense DHCP server enable flag for one interface.
 * Usage: dhcp-opnsense-toggle.php <enable|disable> <dhcpd|kea> <iface>
 *
 * Backends differ in where OPNsense stores their config:
 *   - dhcpd (legacy ISC): per-interface flag $config['dhcpd'][<iface>]['enable'].
 *     ISC was moved out of core in OPNsense 26.1 (os-isc-dhcp plugin); this path
 *     only works on upgraded systems that still have the plugin installed.
 *   - kea: an MVC model under $config['OPNsense']['Kea']['dhcp4']. Kea has NO
 *     per-interface enable flag — it is a single service bound to a list of
 *     interfaces. There is no clean per-interface failover, so we toggle the
 *     GLOBAL Kea enable flag: MASTER serves DHCP, BACKUP does not. The <iface>
 *     argument is accepted for interface validation but Kea is all-or-nothing.
 *
 * Uses the OPNsense global $config array (config.inc) + write_config(), which is
 * still supported on 26.1. The caller (dhcp-{isc,kea}.sh) regenerates templates
 * and restarts the relevant service afterwards.
 */

if ($argc !== 4) {
    fwrite(STDERR, "Usage: dhcp-opnsense-toggle.php <enable|disable> <dhcpd|kea> <iface>\n");
    exit(1);
}

[$_, $action, $backend, $iface] = $argv;

if (!in_array($action,  ['enable', 'disable'], true)) { fwrite(STDERR, "bad action\n");  exit(1); }
if (!in_array($backend, ['dhcpd',  'kea'],     true)) { fwrite(STDERR, "bad backend\n"); exit(1); }
if (!preg_match('/^[a-z][a-z0-9]{1,14}$/', $iface))  { fwrite(STDERR, "bad iface\n");   exit(1); }

require_once('config.inc');

global $config;

$want = ($action === 'enable') ? '1' : '0';

if ($backend === 'kea') {
    // Walk/create the nested MVC model path and set the global enable flag.
    $node = &$config;
    foreach (['OPNsense', 'Kea', 'dhcp4', 'general'] as $key) {
        if (!isset($node[$key]) || !is_array($node[$key])) {
            $node[$key] = [];
        }
        $node = &$node[$key];
    }
    $node['enabled'] = $want;
    unset($node);
} else { // dhcpd (legacy ISC)
    if (!isset($config[$backend]) || !is_array($config[$backend])) {
        $config[$backend] = [];
    }
    if ($action === 'enable') {
        if (!isset($config[$backend][$iface]) || !is_array($config[$backend][$iface])) {
            $config[$backend][$iface] = [];
        }
        $config[$backend][$iface]['enable'] = 1;
    } else {
        if (isset($config[$backend][$iface]['enable'])) {
            unset($config[$backend][$iface]['enable']);
        }
    }
}

write_config("keepalived-bsd: $action DHCP ($backend) on $iface");
exit(0);
