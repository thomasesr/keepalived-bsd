# keepalived-bsd

**Real VRRPv3 (RFC 5798)** high-availability daemon for OPNSense (FreeBSD). Runs N independent per-VRID instances that elect MASTER/BACKUP via unicast VRRPv3 advertisements, then enforce the active/standby config on interfaces. Interoperates natively with a stock **keepalived** peer. Ships with an OPNSense web UI plugin.

## What it does

- Sends/receives real VRRPv3 adverts (IP protocol 112, unicast, TTL 255) per VRID
- Runs each `vrrp_instance` as an independent FSM → load sharing (MASTER on one VRID, BACKUP on another)
- On becoming MASTER: adds the instance's virtual IPs, sends gratuitous ARP, enables DHCP
- On becoming BACKUP: removes virtual IPs, disables DHCP (prevents split-brain)
- On shutdown: sends a priority-0 resign advert so the peer takes over immediately

## Target environment

| Node | Platform | Role |
|------|----------|------|
| Primary | OPNSense (FreeBSD) | MASTER (higher priority) |
| Secondary | OpenWRT (Linux) | BACKUP (stock keepalived) |

The daemon runs on OPNSense and speaks standard VRRPv3, so the peer is just **stock keepalived** configured `version 3`, unicast, matching VRIDs and `advert_int`. VRRPv3 has no in-packet authentication — see [Security](#security).

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

The config is INI: an optional `[global]` block (fallback defaults) plus one `[vrrp_instance NAME]` block per VRRP group. Keys mirror keepalived's `vrrp_instance`:

| Key | Description |
|-----|-------------|
| `state` | Initial state `BACKUP` or `MASTER` (default `BACKUP`) |
| `interface` | Advert interface — where VRRP packets are sent/received (FreeBSD name) |
| `unicast_src_ip` | This host's source IP for adverts |
| `unicast_peer` | The peer's IP |
| `virtual_router_id` | VRID (1–255) — **must match the peer's VRID** for this group |
| `priority` | 1–254 (255 = address owner); higher wins MASTER; omit → `[global] priority` |
| `advert_int` | Advert interval in seconds — **must match the peer** |
| `preempt` | `yes`/`no` — reclaim MASTER when a higher-priority instance returns |
| `vip` | Virtual IP, **repeatable**: `vip = ADDR/prefix [dev IF] [label L]` (dev defaults to `interface`) |
| `alias` | Optional OPNsense firewall alias to update with the VIP |
| `dhcp_backend` | Optional DHCP backend for this instance (see below) |

`[global]` accepts `priority` and `dhcp_backend` as fallbacks for instances that omit them.

```ini
[global]
priority     = 100
dhcp_backend = dnsmasq

[vrrp_instance master]
  interface         = igb0
  unicast_src_ip    = 192.168.1.1
  unicast_peer      = 192.168.1.3
  virtual_router_id = 10
  priority          = 110
  advert_int        = 5
  vip               = 192.165.1.2/24 dev igb0
```

**DHCP backends** — served on MASTER, stopped on BACKUP. Pick whichever is installed:

| Value | Daemon | Prerequisite |
|-------|--------|--------------|
| `dnsmasq` | dnsmasq | OPNSense 26.1 default DHCP |
| `kea` | Kea DHCPv4 | Kea enabled (whole-service toggle) |
| `isc` | ISC dhcpd | legacy `os-isc-dhcp` plugin |
| `none` | — | Manual DHCP control (default, fail-closed) |

See `keepalived-bsd.conf.example` for the full 4-instance example matching a real peer.

> **Migrating from a pre-VRRPv3 (KALV) install:** the old `[global]`+`[iface X]` config
> and the KALV wire protocol are **gone**. Old `[iface]` blocks are rejected with a
> migration error. Rewrite your config into `[vrrp_instance NAME]` blocks (set matching
> VRIDs and `advert_int` on both peers), and configure the peer as stock keepalived
> `version 3` unicast. There is no automatic migration.

## Security

VRRPv3 carries **no in-packet authentication** (RFC 5798 §9). Protect the advertisement link (IP protocol 112) between the two hosts with an **OS transport-mode IPsec SA** (ESP + PSK). The daemon sends/receives plaintext VRRP on its raw socket; the kernel IPsec layer encrypts/authenticates on the wire — transparent to the daemon.

- Transport mode (host-to-host), **not** tunnel — preserves the original IP header so TTL=255 survives.
- Selector: local ↔ remote, protocol `112`. Use IPsec policy **`require`** (not `use`) so unprotected VRRP is dropped (no auth bypass).
- On OPNSense: strongSwan (`swanctl.conf`); on OpenWRT: matching strongSwan/racoon transport SA, same PSK, mirrored selectors. See `PLAN.md` §6 for an example.

## OPNSense UI

After installing the plugin, the UI appears under **Services → Keepalived HA**. It provides:

- Service status badge + Start / Stop / Restart buttons
- General settings (enable, fallback priority, default DHCP backend)
- **VRRP instance grid** with add/edit modal (state, interface, unicast src/peer, VRID, priority, advert interval, preempt, VIP list, alias, DHCP backend)
- **VRRP Status table** — live per-instance state (active/initial state, priority, probes sent/received, last transition), polled every 2 s with a stale-data flag

## State machine (per VRID, RFC 5798)

```
                Master_Down_Timer fires
   Backup  ───────────────────────────────►  Master
     ▲                                          │
     │   recv advert, higher priority           │
     │   (or equal + higher source IP)          │
     └──────────────────────────────────────────┘
```

Each instance starts in **Initialize** → **Master** if it owns the address (`priority 255`), else **Backup** with `Master_Down_Timer` armed (`3×advert_int + Skew_Time`). Backup promotes to Master when the timer fires: adds VIPs, sends gratuitous ARP, enables DHCP. A Master receiving a higher-priority advert yields to Backup and releases. On shutdown a Master sends a priority-0 resign advert so the peer takes over at once.

## Uninstall

```sh
make uninstall              # remove binary + rc.d script
make uninstall-opnsense     # remove OPNSense plugin files
```
