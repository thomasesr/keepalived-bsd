#!/usr/local/bin/php
<?php
/**
 * Add or remove an IP from an OPNsense firewall alias.
 * Usage: alias-update.php <add|del> <alias-name> <ip>
 *
 * Creates the alias if it doesn't exist (add). Silently succeeds
 * if IP is already absent (del). Reloads filter rules via shell script.
 */

if ($argc !== 4) {
    fwrite(STDERR, "Usage: alias-update.php <add|del> <alias-name> <ip>\n");
    exit(1);
}

[$_, $action, $alias_name, $ip] = $argv;

if (!in_array($action, ['add', 'del'], true)) { fwrite(STDERR, "bad action\n"); exit(1); }
if (!preg_match('/^[a-zA-Z0-9_]{1,64}$/', $alias_name)) { fwrite(STDERR, "bad alias name\n"); exit(1); }
if (!filter_var($ip, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4)) { fwrite(STDERR, "bad ip\n"); exit(1); }

require_once('config.inc');

global $config;

$found = false;

if (!empty($config['aliases']['alias'])) {
    $count = count($config['aliases']['alias']);
    for ($i = 0; $i < $count; $i++) {
        if ($config['aliases']['alias'][$i]['name'] !== $alias_name) {
            continue;
        }
        $found = true;
        $ips = array_filter(
            preg_split('/\s+/', trim($config['aliases']['alias'][$i]['content'] ?? ''))
        );
        if ($action === 'add') {
            if (!in_array($ip, $ips, true)) {
                $ips[] = $ip;
            }
        } else {
            $ips = array_values(array_filter($ips, function ($v) use ($ip) {
                return $v !== $ip;
            }));
        }
        $config['aliases']['alias'][$i]['content'] = implode(' ', $ips);
        break;
    }
}

if (!$found && $action === 'add') {
    if (!isset($config['aliases'])) {
        $config['aliases'] = [];
    }
    if (!isset($config['aliases']['alias'])) {
        $config['aliases']['alias'] = [];
    }
    $config['aliases']['alias'][] = [
        'name'        => $alias_name,
        'type'        => 'host',
        'proto'       => 'IPv4',
        'content'     => $ip,
        'description' => 'Keepalived VIP (managed automatically)',
    ];
}

write_config("keepalived-bsd: $action $ip in alias $alias_name");
exit(0);
