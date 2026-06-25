# keepalived-bsd

VRRP-like high-availability daemon for OPNSense (FreeBSD). Coordinates MASTER/BACKUP state between gateways by exchanging heartbeat packets, then enforces the active/standby config on interfaces. Ships with an OPNSense web UI plugin.

## What it does

- Sends periodic heartbeat UDP packets to peer gateway(s)
- Listens for heartbeats from the current MASTER
- Transitions between MASTER and BACKUP state on timeout or explicit preemption
- On MASTER: assigns virtual IPs to configured interfaces, enables DHCP server
- On BACKUP: removes virtual IPs, disables DHCP server (prevents split-brain)

## Target environment

| Node | Platform | Role |
|------|----------|------|
| Primary | OPNSense (FreeBSD) | MASTER (preferred) |
| Secondary | OpenWRT (Linux) | BACKUP |

The daemon runs on OPNSense. The OpenWRT peer must run a compatible counterpart that speaks the same heartbeat protocol (UDP, magic `"KALV"`, versioned packet format).

## Build

```sh
make
```

Requires a FreeBSD build environment with a standard C99 toolchain. No external library dependencies.

## Install

```sh
make install-all        # daemon + rc.d + OPNSense plugin
```

Or individually:

```sh
make install            # /usr/local/sbin/keepalived-bsd + default conf
make install-rc         # /usr/local/etc/rc.d/keepalived_bsd
make install-opnsense   # OPNSense MVC plugin (models, controllers, views, configd)
```

After `install-opnsense`, restart configd so it picks up the new action definitions:

```sh
service configd restart
```

Then register the plugin via `+PLUGIN.php` to get the menu entry and ACL.

## Configuration

Copy the example and edit before starting:

```sh
cp keepalived-bsd.conf.example /usr/local/etc/keepalived-bsd.conf
```

Key settings (`[global]` section):

| Key | Default | Description |
|-----|---------|-------------|
| `peer` | — | Peer gateway IPv4 address (required) |
| `port` | 5405 | UDP heartbeat port |
| `priority` | 100 | Election priority — higher wins MASTER |
| `heartbeat` | 1 | Seconds between heartbeat sends |
| `timeout` | 3 | Seconds of peer silence before failover |
| `preempt` | yes | Yield MASTER when higher-priority peer returns |
| `dhcp_backend` | `isc` | DHCP daemon to start/stop on transition (see below) |

**DHCP backends** — the daemon is started on MASTER, stopped on BACKUP. Pick whichever is installed:

| Value | Daemon | Prerequisite |
|-------|--------|--------------|
| `isc` | ISC dhcpd | OPNSense built-in (default) |
| `kea` | Kea DHCPv4 | `os-kea` plugin installed |
| `dnsmasq` | dnsmasq | dnsmasq DHCP mode enabled |
| `none` | — | Manual DHCP control |

Add one `[iface <name>]` section per interface:

```ini
[iface em0]
vip = 10.0.0.1/24
```

See `keepalived-bsd.conf.example` for a full example.

## OPNSense UI

After installing the plugin, the UI appears under **Services → Keepalived HA**. It provides:

- Service status badge + Start / Stop / Restart buttons
- General settings form (peer, port, priority, heartbeat, timeout, preempt, DHCP backend)
- Interface table with add / remove rows

## State machine

```
         timeout / peer gone
BACKUP  ─────────────────────► MASTER
  ▲                               │
  │  peer returns (higher prio)   │
  └───────────────────────────────┘
```

MASTER sends heartbeats. BACKUP listens. Silence ≥ `timeout` seconds → BACKUP promotes to MASTER, adds VIPs, enables DHCP. Higher-priority MASTER reappears → current MASTER yields, removes VIPs, disables DHCP.

## Uninstall

```sh
make uninstall              # remove binary + rc.d script
make uninstall-opnsense     # remove OPNSense plugin files
```
