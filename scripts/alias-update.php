#!/usr/local/bin/php
<?php
/**
 * Add or remove an IP from an OPNsense firewall alias.
 * Usage: alias-update.php <add|del> <alias-name> <ip>
 *
 * On current OPNsense (incl. 26.1) firewall aliases are owned by the MVC model
 * mounted at //OPNsense/Firewall/Alias and stored under
 *   $config['OPNsense']['Firewall']['Alias']['aliases']['alias']
 * as UUID-keyed array items. The legacy top-level $config['aliases']['alias']
 * tree is NOT what the firewall rule compiler reads, so editing it has no effect.
 *
 * Creates the alias (host type) if it does not exist on 'add'. Silently succeeds
 * if the IP is already absent on 'del'. The wrapper reloads filter rules after.
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
require_once('util.inc');

global $config;

function uuid_v4(): string
{
    $d = random_bytes(16);
    $d[6] = chr((ord($d[6]) & 0x0f) | 0x40);
    $d[8] = chr((ord($d[8]) & 0x3f) | 0x80);
    return vsprintf('%s%s-%s-%s-%s-%s%s%s', str_split(bin2hex($d), 4));
}

// Walk/create the MVC model container.
$node = &$config;
foreach (['OPNsense', 'Firewall', 'Alias', 'aliases'] as $key) {
    if (!isset($node[$key]) || !is_array($node[$key])) {
        $node[$key] = [];
    }
    $node = &$node[$key];
}
if (!isset($node['alias']) || !is_array($node['alias'])) {
    $node['alias'] = [];
}
$aliases = &$node['alias'];

// Normalise to a list we can iterate: a single item is an assoc array (has
// 'name'); multiple items are a numeric array of such assoc arrays.
$keys = isset($aliases['name']) ? [null] : array_keys($aliases);

$found = false;
foreach ($keys as $k) {
    if ($k === null) {
        $entry = &$aliases;
    } else {
        $entry = &$aliases[$k];
    }
    if (($entry['name'] ?? '') !== $alias_name) {
        unset($entry);
        continue;
    }
    $found = true;
    $ips = array_values(array_filter(preg_split('/\s+/', trim((string)($entry['content'] ?? '')))));
    if ($action === 'add') {
        if (!in_array($ip, $ips, true)) {
            $ips[] = $ip;
        }
    } else {
        $ips = array_values(array_filter($ips, fn ($v) => $v !== $ip));
    }
    $entry['content'] = implode("\n", $ips);
    unset($entry);
    break;
}

if (!$found) {
    if ($action !== 'add') {
        exit(0); // nothing to remove
    }
    // Create a new host alias. OPNsense's array->XML serializer stores an MVC
    // ArrayField item's UUID in the '@attributes' map (emitting <alias uuid="…">),
    // NOT as the array key — using the UUID as a key would emit a bogus element
    // named after the UUID and corrupt config.xml. Normalise the single-item
    // flat-assoc shape to a numeric list first, then append.
    if (isset($aliases['name'])) {
        $aliases = [$aliases];
    }
    $aliases[] = [
        '@attributes' => ['uuid' => uuid_v4()],
        'enabled'     => '1',
        'name'        => $alias_name,
        'type'        => 'host',
        'proto'       => '',
        'content'     => $ip,
        'description' => 'Keepalived VIP (managed automatically)',
    ];
}

write_config("keepalived-bsd: $action $ip in alias $alias_name");
exit(0);
