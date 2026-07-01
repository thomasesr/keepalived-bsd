# keepalived-bsd {{RELEASE_TAG}}

Real **VRRPv3 (RFC 5798)** high-availability daemon for OPNsense. Runs N
independent per-VRID instances that elect MASTER/BACKUP via unicast VRRPv3
advertisements (IP protocol 112) and manage virtual IPs, gratuitous ARP and
DHCP on transition. Interoperates natively with a stock keepalived peer.

> **Breaking change:** this release replaces the old custom `KALV` UDP protocol
> and `[global]`+`[iface]` config with real VRRPv3 and `[vrrp_instance NAME]`
> config. Old configs are rejected with a migration error — see README
> "Migrating". VRRPv3 has no in-packet auth; protect the proto-112 link with
> transport-mode IPsec (ESP/PSK). There is no automatic migration.

## Target platform

| | |
|---|---|
| **OPNsense** | {{OPNSENSE_VER}} |
| **FreeBSD**  | {{FREEBSD_VER}} |
| **Arch**     | amd64 |

## What's in this release

- `install.sh` — one-shot installer (fetches and places all files)
- `keepalived-bsd` — daemon binary (FreeBSD amd64 ELF)
- `keepalived-bsd.conf.example` — annotated config template
- `keepalived_bsd` — FreeBSD rc.d startup script
- `scripts.tar.gz` — helper scripts: DHCP backends (dnsmasq / Kea / ISC) + firewall-alias update
- `opnsense-plugin.tar.gz` — OPNsense MVC plugin (models, controllers, views, configd actions)

## Install

```sh
fetch https://github.com/thomasesr/keepalived-bsd/releases/download/{{RELEASE_TAG}}/install.sh
sh install.sh
```

Then:

```sh
# Edit config
vi /usr/local/etc/keepalived-bsd.conf

# Enable + start
sysrc keepalived_bsd_enable=YES
service keepalived_bsd start
```

## Changes

Since **v0.2.0**, newest first:

- `c0e91db` fix(carp): unload kernel CARP so inbound VRRPv3 adverts reach the daemon. CARP and VRRP both use IP protocol 112; on FreeBSD `carp.ko` owns the proto-112 handler and silently drops every inbound advert (receive stuck at 0, both nodes go MASTER). The rc.d prestart unloads `carp.ko` before start, and the daemon re-checks every 10 s so a mid-run reload can't re-break receive. **Incompatible with OPNsense CARP virtual IPs** — the UI warns on enable.
- `2bf033d` feat(ui): add a **Clone** button to the VRRP instances grid to duplicate an existing instance's settings into the add/edit modal.

## Checksums

```
{{CHECKSUMS}}
```
